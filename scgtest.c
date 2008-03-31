

#include <stdio.h>

int fib43();

#include "scg.h"

int main()
{
   scg_initialize();

    printf ("%i\n", fib43());

   scg_output_profile();

    return 0;
}
