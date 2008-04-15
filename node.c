
#include <execinfo.h>
#include <ucontext.h>
#include <stdlib.h>
#include <sys/time.h>

#include "atomic.h"
#include "node.h"
#include "scg.h"

// FIXME - only good for 32 bits.
#define GOLDEN_PRIME 2663455159ul

scg_node_t * volatile scg_node_hash[SCG_NODE_HASH_SIZE];

//static volatile int enabled;

/* int bad_frame_bail; */


static scg_node_t * scg_put_node (scg_node_t * current,
                                  scg_address_t address,
                                  scg_node_t ** restrict new_node)
{
    /* Generate 32 bit hash key. */
    unsigned long hash = 5 * (unsigned long) current;
    hash += (unsigned long) address;
    hash *= GOLDEN_PRIME;

    /* Reduce to table size. */
    hash >>= sizeof (unsigned long) * 8 - SCG_NODE_HASH_ORDER;

    /* Now try to find the node. */
    scg_node_t * volatile * pnode = scg_node_hash + hash;

    while (1) {
        scg_node_t * node = *pnode;

        /* If there is no node here, we need to generate one. */
        if (node == NULL) {
            if (*new_node == NULL)
                *new_node = scg_allocate_node();

   /* If the supposed frame pointer is unreasonable, then bail now. */
/*    if (((unsigned long) frame) < context->esp || */
/*        ((unsigned long) frame) > context->esp + 0x10000) { */
/*        ++bad_frame_bail; */
/*        return; */
/*    } */
            (*new_node)->address = (scg_address_t) address;
            (*new_node)->next = current;
            (*new_node)->counter = 0;

            /* Insert it atomically for thread safty. */
            node = scg_atomic_compare_and_exchange ((void * volatile *) pnode,
                                                    (*new_node), NULL);
            if (node == NULL) {
                node = *new_node;
                *new_node = NULL;
                return node;            /* We're done. */
            }
        }

        /* See if the existing node is good enough. */
        if (node->address == address && node->next == current)
            return node;

        /* Try next node in hash list. */
        pnode = &node->hash_link;
    }
}


#if 1
static void scg_signal_handler (int signal, siginfo_t * info, void * p)
{
/*     if (!enabled) */
/*         return; */
    scg_node_t * new_node = NULL;
    scg_node_t * node = NULL;

    /* Setup the stack frame data. */
    const ucontext_t * context = (const ucontext_t *) p;
    scg_address_t stack = (scg_address_t) context->uc_mcontext.gregs[REG_ESP];

    const unsigned char * address
        = (scg_address_t) context->uc_mcontext.gregs[REG_EIP];
    const scg_address_t * frame
        = (const scg_address_t *) context->uc_mcontext.gregs[REG_EBP];
    scg_address_t dummy_frame[2];

    /* Deal with hits in the PLT tables.  */
    if (address[0] == 0xff && address[1] == 0xa3) {
        // The ebx-relative indirect jump in the SO PLT tables.
        const scg_address_t * ptr = (const scg_address_t *)
            (context->uc_mcontext.gregs[REG_EBX]
             + address[2] + address[3] * 256);
        if ((3 & (unsigned) ptr) == 0)
            address = *ptr;
    }
    if (address[0] == 0xff && address[1] == 0x25) {
        // The absolute indirect jump in the main PLT.
        const scg_address_t * ptr = (const scg_address_t *) (
            address[2] + address[3] * 256
            + (address[4] << 16) + (address[5] << 24));
        if ((3 & (unsigned) ptr) == 0)
            address = *ptr;
    }

    /* We attempt to deal with corner cases during function entry and exit.
     * The prologue is "push %ebp (0x55), mox %esp,%ebp (0x89 0xe5)" and the
     * epilogue is "pop %ebp, ret (0xc3)".
     *
     * If we are in the prologue, then we "fast forward" the instructions.  If
     * we are on a ret, then we "rewind" the pop.
     */
    if (address[0] == 0x55 /*&& address[1] == 0x89 && address[2] == 0xe5)*/ ||
        address[0] == 0xc3) {
        /* We are before the "push %ebp; mov %esp,%ebp" or after the "pop %ebp".
         * Generate a dummy stack frame and use it.  */
        dummy_frame[0] = frame;
        dummy_frame[1] = * (const scg_address_t *) stack;

        frame = dummy_frame;
    }
    else if (address[-1] == 0x55 ||
             (address[0] == 0x89 && address[1] == 0xe5)) {
        /* We are just before the mov %esp,%ebp.  Pretend that has been done. */
        frame = stack;
    }

    /* If the supposed frame pointer is unreasonable, then bail now. */
/*    if (((unsigned long) frame) < (unsigned long) stack || */
/*        ((unsigned long) frame) > (unsigned long) stack + 0x10000) { */
/*        write (2, "BAIL", 4); */
/*       return; */
/*    } */

    const scg_address_t * old_frame;
    do {
        node = scg_put_node (node, address, &new_node);

        /* Onto the next stack frame. */
        old_frame = frame;
        address = frame[1];
        frame = (const scg_address_t *) frame[0];

    }
    /* FIXME - need more precise test. */
    while ((3 & (unsigned long) frame) == 0 &&
           ((unsigned long) frame) > ((unsigned long) old_frame) &&
           ((unsigned long) frame) < ((unsigned long) old_frame) + 0x100000);

    /* Now increment the counter. */
    scg_atomic_increment (&node->counter);

    /* FIXME - we potentially leak new_node here... this is not going to happen
     * very often in real life - not worth worrying about!!! */
}
#else
#include <libunwind.h>
#include <string.h>
#include <unistd.h>
#define CHECK(s) check(s, #s "\n")
static inline int check (int s, const char * w)
{
    if (s >= 0)
        return s;
    write (2, w, strlen (w));
    abort();
}
static void scg_signal_handler (int signal, siginfo_t * info, void * p)
{
/*     if (!enabled) */
/*         return; */
    scg_node_t * new_node = NULL;
    scg_node_t * node = NULL;

    /* Setup the stack frame data. */
/*    const ucontext_t * context = (const ucontext_t *) p; */
/*    ucontext_t ct = * (const ucontext_t *) p; */
/*    ct.uc_stack.ss_sp = (void*) ct.uc_mcontext.gregs[REG_ESP]; */
    unw_context_t context;
    CHECK (unw_getcontext (&context));

    unw_cursor_t cursor;
    CHECK (unw_init_local (&cursor, &context));
    do {
        unw_word_t ip = 0;
        CHECK (unw_get_reg (&cursor, UNW_X86_EIP, &ip));
        node = scg_put_node (node, (scg_address_t) ip, &new_node);
/*         unw_proc_info_t pi; */
/*         CHECK (unw_get_proc_info (&cursor, &pi)); */
/*         node = scg_put_node (node, (scg_address_t) pi.start_ip, &new_node); */
/*         if (pi.unwind_info == NULL) */
/*             abort(); */
/*         if (ip > 0xbf000000 && ip < 0xc0000000) */
/*             abort(); */
    }
    while (CHECK (unw_step (&cursor)) > 0);

    scg_atomic_increment (&node->counter);
}
#endif


static int is_initialized;

/* Start the profile timer for this thread. */
void scg_thread_initialize (void)
{
    struct itimerval timer;

    if (!is_initialized)
        return;

    /* Fire 500 times / second. */
    timer.it_interval.tv_sec  = 0;
    timer.it_interval.tv_usec = 2000;

    timer.it_value   .tv_sec  = 0;
    timer.it_value   .tv_usec = 2000;

    setitimer (ITIMER_PROF, &timer, NULL);
}

static void user1_handler (int signal, siginfo_t * info, void * p)
{
//    enabled = 1;
}

static void user2_handler (int signal, siginfo_t * info, void * p)
{
//    enabled = 0;
    scg_output_profile();
}

/* Setup the signal handler and timer. */
void scg_initialize (void)
{
    struct sigaction action;

    action.sa_sigaction = scg_signal_handler;
    action.sa_flags = SA_RESTART | SA_SIGINFO;
    sigemptyset (&action.sa_mask);

    sigaction (SIGPROF, &action, NULL);
//   sigaction (SIGALRM, &action, NULL);

    action.sa_sigaction = user1_handler;
    sigaction (SIGUSR1, &action, NULL);

    action.sa_sigaction = user2_handler;
    sigaction (SIGUSR2, &action, NULL);

    is_initialized = 1;

    scg_thread_initialize();
}

// Dirty hack to monitor calls to new().
/* void * _Znwj (unsigned int n) */
/* { */
/*     if (is_initialized) { */
/*         ucontext_t c; */
/*         getcontext (&c); */
/*         scg_signal_handler (0, NULL, &c); */
/*     } */
/*     return malloc (n); */
/* } */
