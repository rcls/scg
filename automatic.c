
#include "scg.h"

#include <stdlib.h>
#include <sys/time.h>

/* Automatically call the profiling functions. */

/* Provide your own definition of scg_auto_start to suppress automatic
 * invocation.  */
void scg_auto_start (void) __attribute__ ((constructor,weak));
static void scg_auto_stop (void);

void scg_auto_start (void)
{
   scg_initialize();

   atexit (scg_auto_stop);
}

void scg_auto_stop (void)
{
   scg_output_profile();
}

#include <ucontext.h>

/* Provide our own memcpy; the libc one doesn't set up a stack frame.  */
void * memcpy (void * __restrict dest, const void * __restrict src, size_t n)
{
    char * d = dest;
    const char * s = src;
    while (n) {
        *d++ = *s++;
        --n;
    }
    return dest;
}
