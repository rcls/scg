
int fib0 (void)
{
//    kill (getpid(), 0);
    return 0;
}

int fib1 (void)
{
    return 1;
}

#define FIB(A,B,C) int A (void) { return B() + C(); }

FIB(fib2,fib1,fib0)
FIB(fib3,fib2,fib1)
FIB(fib4,fib3,fib2)
FIB(fib5,fib4,fib3)
FIB(fib6,fib5,fib4)
FIB(fib7,fib6,fib5)
FIB(fib8,fib7,fib6)
FIB(fib9,fib8,fib7)
FIB(fib10,fib9,fib8)
FIB(fib11,fib10,fib9)

FIB(fib12,fib11,fib10)
FIB(fib13,fib12,fib11)
FIB(fib14,fib13,fib12)
FIB(fib15,fib14,fib13)
FIB(fib16,fib15,fib14)
FIB(fib17,fib16,fib15)
FIB(fib18,fib17,fib16)
FIB(fib19,fib18,fib17)
FIB(fib20,fib19,fib18)
FIB(fib21,fib20,fib19)

FIB(fib22,fib21,fib20)
FIB(fib23,fib22,fib21)
FIB(fib24,fib23,fib22)
FIB(fib25,fib24,fib23)
FIB(fib26,fib25,fib24)
FIB(fib27,fib26,fib25)
FIB(fib28,fib27,fib26)
FIB(fib29,fib28,fib27)
FIB(fib30,fib29,fib28)
FIB(fib31,fib30,fib29)

FIB(fib32,fib31,fib30)
FIB(fib33,fib32,fib31)
FIB(fib34,fib33,fib32)
FIB(fib35,fib34,fib33)
FIB(fib36,fib35,fib34)
FIB(fib37,fib36,fib35)
FIB(fib38,fib37,fib36)
FIB(fib39,fib38,fib37)
FIB(fib40,fib39,fib38)
FIB(fib41,fib40,fib39)

FIB(fib42,fib41,fib40)
FIB(fib43,fib42,fib41)
FIB(fib44,fib43,fib42)
FIB(fib45,fib44,fib43)
FIB(fib46,fib45,fib44)
FIB(fib47,fib46,fib45)
FIB(fib48,fib47,fib46)
FIB(fib49,fib48,fib47)
FIB(fib50,fib49,fib48)
FIB(fib51,fib50,fib49)
