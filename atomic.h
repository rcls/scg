
#ifndef SCG_ATOMIC_H_
#define SCG_ATOMIC_H_

static inline void scg_atomic_increment (volatile unsigned long * p)
{
   asm ("lock incl %0" :
        "=m" (*p) :
        "m" (*p));
}

static inline void * scg_atomic_compare_and_exchange (void * volatile * p ,
                                                      void * newv,
                                                      void * oldv)
{
   void * ret;

   asm ("lock;  cmpxchgl %4, %0 \n" :

        "=m" (*p),      // *addr is %0 (output)
        "=a" (ret) :    // read value is %1, in eax (output)

        "m" (*p),       // *addr is %2 (input)
        "a" (oldv),     // oldv is %3, in eax (input)
        "r" (newv));    // newv is %4 (input)

   return ret;
}


#endif
