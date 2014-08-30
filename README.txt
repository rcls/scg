The Sampling Call Graph profiler
================================

These days mostly oprofile or perf are more likely to be of use for
you.

I wrote this to provide gprof-style call-graphs but avoiding
limitations of gprof (needs recompilation, didn't support threads,
didn't support shared libraries on ia32).

Quick Usage
-----------

Set LD_PRELOAD=libscg.so in the environment and run the program.  If
you want the output going to a file instead of stderr, set the
environment variable SCG_OUTPUT to the file name.

Hard Usage
----------

1.  Insert a call to scg_initialize() at the start of main().

2.  Insert a call to scg_thread_initialize() at the start of any other
    thread you wish to profile.

3.  Call scg_output_profile() to print the profile.

4.  Link against libscg.a
