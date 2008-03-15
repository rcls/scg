
#include <dlfcn.h>
#include <malloc.h>
#include <pthread.h>

#include "scg.h"

typedef void * (* thread_func) (void *);
typedef int (* pth_creat) (pthread_t *, const pthread_attr_t *,
                           thread_func, void *);

static pth_creat pthread_create_real;

typedef struct {
   thread_func  function;
   void *       arg;
} context_t;

static void * my_thread_func (void * p)
{
   thread_func function = ((context_t *) p)->function;
   void *      arg      = ((context_t *) p)->arg;

   free (p);

   scg_thread_initialize();

   return function (arg);
}

__asm__ ("\n.symver my_pthread_create, pthread_create@@GLIBC_2.1\n");

int my_pthread_create (pthread_t * __restrict            thread,
                       const pthread_attr_t * __restrict attr,
                       thread_func                       function,
                       void *                            arg)
{
   context_t * context = (context_t *) malloc (sizeof (context_t));
   int ret;

   if (pthread_create_real == NULL) {
      pthread_create_real = (pth_creat) dlvsym (RTLD_NEXT,
                                                "pthread_create",
                                                "GLIBC_2.1");
   }

   context->function = function;
   context->arg = arg;

   ret = pthread_create_real (thread, attr, my_thread_func, context);

   if (ret != 0) {
      free (context);
   }

   return ret;
}
