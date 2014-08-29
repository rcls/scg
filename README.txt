oprofile now does call graphs.  Use that instead - it's much better.

The Sampling Call Graph profiler
================================

This is a profiler that generates call graphs of CPU usage.

(*) It works with shared libaries.

(*) It does not need non standard libraries and compile options.

Easy Usage:

Set LD_PRELOAD=libscg.so in the environment and run the program.  If you want
the output going to a file instead of stderr, set the environment variable
SCG_OUTPUT to the file name.

Hard Usage:

1.  Insert a call to scg_initialize() at the start of main().

2.  Insert a call to scg_thread_initialize() at the start of any other thread
    you wish to profile.

3.  Call scg_output_profile() to print the profile.

4.  Link against libscg.a

TODO:

(*) Find a way of automatically profiling threads.
