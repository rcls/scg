
#include <execinfo.h>
#include <ucontext.h>
#include <stdlib.h>
#include <sys/time.h>

#include "atomic.h"
#include "node.h"

#define GOLDEN_PRIME 2663455159ul

scg_node_t * volatile scg_node_hash[SCG_NODE_HASH_SIZE];

static volatile int enabled;

typedef struct {
   const scg_address_t * saved_base_pointer;
   scg_address_t         return_address;
} stack_frame;

static void scg_signal_handler (int signal, siginfo_t * info, void * p)
{
/*     if (!enabled) */
/*         return; */

   scg_node_t *          new_node;
   scg_node_t *          next = NULL;
   const scg_address_t * old_frame;
   scg_node_t *          node;

   /* Setup the stack frame data. */
   struct sigcontext *   context = (struct sigcontext *) (20 + (char *) p);
   const unsigned char * address = (scg_address_t) context->eip;
   const scg_address_t * frame = (void *) context->ebp;
   stack_frame           dummy_frame;

   /* We attempt to deal with corner cases during function entry and exit.
    * The prologue is "push %ebp (0x55), mox %esp,%ebp (0x89 0xe5)" and the
    * epilogue is "pop %ebp, ret (0xc3)".
    *
    * If we are in the prologue, then we "fast forward" the instructions.  If
    * we are on a ret, then we "rewind" the pop.
    */
   if ((address[0] == 0x55 && address[1] == 0x89 && address[2] == 0xe5) ||
       (address[0] == 0xc3)) {
      /* We are before the "push %ebp; mov %esp,%ebp" or after the "pop %ebp".
       * Generate a dummy stack frame and use it.  */
      dummy_frame.saved_base_pointer = frame;
      dummy_frame.return_address = * (scg_address_t *) context->esp;

      frame = (scg_address_t) &dummy_frame;
   }
   else if (address[0] == 0x89 && address[1] == 0xe5) {
      /* We are just before the mov %esp,%ebp.  Pretend that has been done. */
      frame = (scg_address_t) context->esp;
   }

   /* If the supposed frame pointer is unreasonable, then bail now. */
   if (((unsigned long) frame) < context->esp ||
       ((unsigned long) frame) > context->esp + 0x10000) {
      return;
   }

   do {
      unsigned long           hash;
      scg_node_t * volatile * pnode;

      /* Generate 32 bit hash key. */

      hash = (unsigned long) next;

      hash *= 5;

      hash += (unsigned long) address;

      hash *= GOLDEN_PRIME;

      /* Reduce to table size. */
      hash >>= (32 - SCG_NODE_HASH_ORDER);

      /* Now try to find the node. */
      pnode = scg_node_hash + hash;

      new_node = NULL;

      while (1) {
         node = *pnode;

         /* If there is no node here, we need to generate one. */
         if (node == NULL) {
            if (new_node == NULL) {
               new_node = scg_allocate_node();
            }

            new_node->address = (scg_address_t) address;
            new_node->next = next;
            new_node->counter = 0;

            /* Insert it atomically for thread safty. */
            node = scg_atomic_compare_and_exchange ((void * volatile *) pnode,
                                                    new_node, NULL);
            if (node == NULL) {
               node = new_node;
               new_node = NULL;
               break;              /* We're done. */
            }
         }

         /* See if the existing node is good enough. */
         if (node->address == address && node->next == next) {
            break;
         }

         /* Try next node in hash list. */
         pnode = &node->hash_link;
      }
      next = node;

      /* Onto the next stack frame. */
      old_frame = frame;

      address = frame[1];
      frame = (const scg_address_t *) frame[0];

   }
   /* FIXME - need more precise test. */
   while (((unsigned long) frame) > ((unsigned long) old_frame) &&
          ((unsigned long) frame) < ((unsigned long) old_frame) + 0x10000);

   /* Now increment the counter. */
   scg_atomic_increment (&node->counter);

   /* FIXME - we potentially leak new_node here... this is not going to happen
    * very often in real life - not worth worrying about!!! */
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
    enabled = 1;
}

static void user2_handler (int signal, siginfo_t * info, void * p)
{
    enabled = 0;
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
