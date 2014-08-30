#define UNW_LOCAL_ONLY

#include <libunwind.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "atomic.h"
#include "node.h"
#include "scg.h"

//#define GOLDEN_PRIME 2663455159ul
//#define GOLDEN_PRIME 11400714819323198549ul
static const unsigned long GOLDEN_PRIME = sizeof(unsigned long) == 4
    ? 2663455159ul : 11400714819323198549ul;

scg_node_t * volatile scg_node_hash[SCG_NODE_HASH_SIZE];


#define CHECK(s) check(s, #s "\n")
static inline int check (int s, const char * w)
{
    if (s >= 0)
        return s;
    write (2, w, strlen (w));
    abort();
}


static scg_node_t * scg_put_node (scg_node_t * current,
                                  uintptr_t address,
                                  scg_node_t ** restrict new_node)
{
    /* Generate hash key. */
    unsigned long hash = 5 * (unsigned long) current;
    hash += (unsigned long) address;
    hash *= GOLDEN_PRIME;

    /* Reduce to table size. */
    hash >>= sizeof (unsigned long) * 8 - SCG_NODE_HASH_ORDER;

    /* Now try to find the node. */
    scg_node_t * volatile * pnode = scg_node_hash + hash;

    while (1) {
        scg_node_t * node = *pnode;

        if (node != NULL) {
            /* See if the existing node is good enough. */
            if (node->address == address && node->next == current)
                return node;

            /* Try next node in hash list. */
            pnode = &node->hash_link;
            continue;
        }

        if (*new_node == NULL)
            *new_node = scg_allocate_node();

        (*new_node)->address = address;
        (*new_node)->next = current;
        (*new_node)->counter = 0;

        /* Insert it atomically. */
        if (!__atomic_compare_exchange_n(pnode, &node, *new_node, true,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            continue;

        node = *new_node;
        *new_node = NULL;
        return node;            /* We're done. */
    }
}


static void scg_signal_handler (int signal, siginfo_t * info, void * p)
{
    static __thread scg_node_t * new_node = NULL;
    scg_node_t * node = NULL;

    /* Setup the stack frame data.  Skip me and __restore_rt.  */
    unw_context_t context;
    unw_cursor_t cursor;
    if (unw_getcontext (&context) < 0
        || unw_init_local (&cursor, &context) < 0
        || unw_step(&cursor) <= 0 || unw_step(&cursor) <= 0)
        return;

    do {
        unw_word_t ip = 0;
        if (unw_get_reg (&cursor, UNW_TDEP_IP, &ip) < 0 || ip == 0)
            break;
        node = scg_put_node (node, ip, &new_node);
    }
    while (unw_step (&cursor) > 0);

    scg_atomic_increment (&node->counter);
}


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
