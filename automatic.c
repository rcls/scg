
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
