
#ifndef SCG_ATOMIC_H_
#define SCG_ATOMIC_H_

static inline void scg_atomic_increment (volatile unsigned long * p)
{
   asm ("lock incl %0" :
        "=m" (*p) :
        "m" (*p));
}



#endif
