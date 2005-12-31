
#include <errno.h>
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "symboltable.h"

/* Number of stack entries to keep for each allocation.  */
#ifndef STACK_SIZE
#define STACK_SIZE 10
#endif

/* We ignore the top STACK_ADJUST entries.  */
#ifndef STACK_ADJUST
#define STACK_ADJUST 3
#endif

/* Do we want thread protection?  */
#ifndef THREADS
#define THREADS 1
#endif

/* Account for malloc overheads?  */
#ifndef OVERHEADS
#define OVERHEADS 0
#endif

/* Poison freed memory?  */
#ifndef POISON
#define POISON 0
#endif

/* Are we currently inside the memory tracing code?  */
static int depth;

#if THREADS
#include <pthread.h>
static pthread_mutex_t mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#else
#define pthread_mutex_lock(X) do { } while (0)
#define pthread_mutex_unlock(X) do { } while (0)
#endif

/* Do we need to print out a report?  */
static volatile int need_report;

/* How many bytes are allocated?  */
static size_t global_bytes;

/* Generate a report.  */
static void print_report (void);

/* Declare the glibc internal malloc functions.  */
void   __libc_free (void *ptr);
void * __libc_calloc (size_t nmemb, size_t size);
void * __libc_malloc (size_t size);
void * __libc_memalign (size_t boundary, size_t size);
void * __libc_realloc (void *ptr, size_t size);
void * __libc_valloc (size_t size);

/******************************************************************************
 * Locking functions
 * 
 * enter() locks, leave() unlocks.  These give us thread protection, and also
 * enable us to detect recursive calls due to memory usage with the memory
 * tracer.  Also print the report if needed.
 *****************************************************************************/
static inline void enter (void)
{						
   pthread_mutex_lock (&mutex);			
   ++depth;
   if (need_report)
      print_report();
}

static inline void leave (void)
{
   --depth;
   pthread_mutex_unlock (&mutex);
}

/******************************************************************************
 * The hash table of stack traces.
 *
 * Each entry in the hash table corresponds to a stack trace.  Each entry
 * tracks of the number of bytes allocated against that stack trace.
 *****************************************************************************/
typedef struct StackTrace
{
   struct StackTrace *  next;	/* Linked list structure. */
   struct StackTrace ** prevnext;

   ssize_t     bytes;		/* Bytes outstanding on this stack. */
   size_t      refs;		/* Reference count.  */
   const void * stack [STACK_SIZE]; /* Stack trace (NULL padded). */
} StackTrace;

/* The hash table.  */
#define STACK_HASH_SIZE 786433
static StackTrace * stack_hash[STACK_HASH_SIZE];

/******************************************************************************
 * account
 *
 * Optionally round-up a byte count to approximately account for the overheads
 * that malloc has in normal operation.
 *****************************************************************************/
static inline size_t account (size_t n)
{
#if OVERHEADS
   /* Add 8 and round up to a multiple of 8.  This approximates the normal
    * overhead from the glibc malloc. */
   return ((n) + 8 + 7) & -8;
#else
   return n;
#endif
}

/******************************************************************************
 * The malloc records.
 *
 * We track malloc'd memory in a hash table.
 *****************************************************************************/
typedef struct MemRecord {
   struct MemRecord * next;
   void *             memory;
   StackTrace *       stack;
   size_t             refs;
   size_t             bytes;
} MemRecord;

#define MEM_HASH_SIZE 12582917
static MemRecord * mem_hash[MEM_HASH_SIZE];

/* Calculate the hash bucket for a pointer.  */
static inline MemRecord ** mem_bucket (void * p)
{
   return &mem_hash [((size_t)(p)) % MEM_HASH_SIZE];
}

/******************************************************************************
 * free StackTrace and MemRecord items.
 *
 *****************************************************************************/
static inline void free_StackTrace (StackTrace * p)
{
   StackTrace * next = p->next;
   *p->prevnext = next;
   if (next)
      next->prevnext = p->prevnext;
   __libc_free (p);
}
static inline void free_MemRecord (MemRecord * p, MemRecord ** pp)
{
   *pp = p->next;
   __libc_free (p);
}

/******************************************************************************
 * get_StackTrace
 *
 * Get a stack trace record for the current stack.  We discard STACK_OFFSET
 * entries off the stack.  Currently this is 3: get_StackTrace(), the recording
 * function, and the entry point.
 *****************************************************************************/
static StackTrace * get_StackTrace (void)
{
   size_t hash = 0;

   /* Grab the stack trace and clear any entries off the end.  */
   void * stack_data [STACK_SIZE + STACK_ADJUST];
   int i = backtrace (stack_data, STACK_SIZE + STACK_ADJUST);
   memset (stack_data + i, 0, sizeof (void*) * (STACK_SIZE + STACK_ADJUST - i));
   void ** stack = stack_data + STACK_ADJUST;

   for (int count = 0; count != STACK_SIZE; ++count)
      hash = hash * 31 + (size_t) stack[count];

   /* Lookup the stacktrace in the hash chain.  */
   StackTrace ** bucket = &stack_hash[hash % STACK_HASH_SIZE];
   for (StackTrace * it = *bucket; it; it = it->next)
      if (memcmp (it->stack, stack, STACK_SIZE * sizeof (void *)) == 0)
         /* Got it.  */
         return it;

   /* Allocate a new one.  Put the new item at the start of the hash bucket,
    * expecting most objects to be short lived.  */
   StackTrace * it = __libc_malloc (sizeof (StackTrace));
   if (it == NULL)
      return it;

   it->next = *bucket;
   if (it->next)
      it->next->prevnext = &it->next;
   it->prevnext = bucket;
   *bucket = it;
   it->refs = 0;
   it->bytes = 0;
   memcpy (it->stack, stack, sizeof (void *) * STACK_SIZE);

   return it;
}

/******************************************************************************
 * record_malloc
 *
 * Record a malloc (or calloc) of size 'bytes'.
 *****************************************************************************/
static void * record_malloc (void *  memory,
			     size_t  bytes)
{
   enter();

   /* If the allocation failed, or we're not at depth 1, then nothing to
    * do.  */
   if (memory == NULL || depth != 1)
      goto out;

   /* Create a record to record that allocation.  */
   MemRecord * record = __libc_malloc (sizeof (MemRecord));
   if (record == NULL)
      goto out;			/* OOM : can't record.  */

   /* Chuck us into the hash table.  */
   MemRecord ** bucket = mem_bucket (memory);
   record->next = *bucket;
   *bucket = record;

   record->memory = memory;
   record->bytes = bytes;

   /* Get the stacktrace object to use.  */
   StackTrace * it = get_StackTrace();
   if (it != NULL) {
      record->stack = it;
      it->bytes += account (bytes);
      global_bytes += account (bytes);
      ++it->refs;
   }
   else
      free_MemRecord (record, bucket);

 out:
   leave();
   return memory;
}

/******************************************************************************
 * record_free
 *
 * Remove the pointer from the tables.
 *****************************************************************************/
static void record_free (void * ptr)
{
   if (ptr == NULL)
      return;

   enter();

   /* Try and find the memory record.  */
   MemRecord * it;
   for (MemRecord ** it_p = mem_bucket (ptr); (it = *it_p); it_p = &it->next)
      if (it->memory == ptr) {
	 /* Found.  */
	 StackTrace * stack = it->stack;
	 stack->bytes -= account (it->bytes);
	 global_bytes -= account (it->bytes);
#if POISON
	 memset (ptr, 0xcd, it->bytes);
#endif
	 --stack->refs;
	 if (stack->bytes == 0 && stack->refs == 0)
	    free_StackTrace (stack);
	 free_MemRecord (it, it_p);

	 if (depth != 1)
	    dprintf (STDERR_FILENO,
		     "mtrace:  Recorded free at depth %i.  Maybe harmless.\n",
		     depth);

	 leave();
	 return;
      }

   /* We did not find the address.  If this happens at depth==1, it's probably
    * and error : write an error message to stderr.  */

   if (depth == 1)
      /* Use dprintf because that might be a bit safer than fprintf if
       * stdio stuff is on the stack.  */
      dprintf (STDERR_FILENO, "mtrace: Free of unknown pointer %p.\n", ptr);

   leave();

   return;
}

/******************************************************************************
 * malloc.
 *
 *****************************************************************************/
void * malloc (size_t size)
{
   return record_malloc (__libc_malloc (size), size);
}

/******************************************************************************
 * free and cfree.
 *
 * cfree is an alias of free in libc, so we do that too.
 *****************************************************************************/
void free (void * ptr)
{
   record_free (ptr);
   __libc_free (ptr);
}

/******************************************************************************
 * realloc.
 *
 * We treat realloc as a free followed by a malloc.  Not perfect, but works.
 *****************************************************************************/
void * realloc (void * ptr, size_t bytes)
{
   void * ret = __libc_realloc (ptr, bytes);
   // FIXME - this is not right if we are poisoning.
   if (ret != NULL)
      record_free (ptr);	/* Handles ptr == NULL.  */
   return record_malloc (ret, bytes); /* Handles ret = NULL.  */
}

/******************************************************************************
 * calloc, cfree, valloc, memalign.
 *
 * posix_memalign not done as __libc_posix_memalign not exported.
 *****************************************************************************/
void * calloc (size_t n, size_t bytes)
{
   return record_malloc (__libc_calloc (n, bytes), n * bytes);
}

void * valloc (size_t size)
{
   return record_malloc (__libc_valloc (size), size);
}

void * memalign (size_t boundary, size_t size)
{
   return record_malloc (__libc_memalign (boundary, size), size);
}

/******************************************************************************
 * Aliases
 * 
 * We map C++ operator new / delete to malloc and free.  Not quite perfect, but
 * good enough in real life.  Also, instead of worrying about whether size_t is
 * unsigned int or unsigned long, we take both.
 *****************************************************************************/
#define ALIAS(s,t) __asm__ (".equiv " #s ", " #t "\n.globl " #s "\n")

ALIAS (cfree, free);
ALIAS (_Znwj, malloc);		/* new(unsigned int) */
ALIAS (_Znaj, malloc);		/* new[](unsigned int) */
ALIAS (_Znwm, malloc);		/* new(unsigned long) */
ALIAS (_Znam, malloc);		/* new[](unsigned long) */
ALIAS (_ZdlPv, free);		/* delete(void *) */
ALIAS (_ZdaPv, free);		/* delete[](void *) */


/******************************************************************************
 * Report data structures.
 *
 *****************************************************************************/
typedef struct ReportItem {
   const char * string;
   ssize_t      bytes;
} ReportItem;

static int compare_by_string (const void * a, const void * b)
{
   const ReportItem * aa = a;
   const ReportItem * bb = b;
   return strcmp (aa->string, bb->string);
}

static int compare_by_bytes (const void * a, const void * b)
{
   const ReportItem * aa = a;
   const ReportItem * bb = b;
   if (aa->bytes == bb->bytes)
      return 0;

   return aa->bytes > bb->bytes ? -1 : 1;
}

/* Should we keep the offsets into functions? */
int report_offsets = 0;

/******************************************************************************
 * print_report
 *
 *****************************************************************************/
void print_report (void)
{
   /* Make absolutely sure we don't recurse here.  */
   if (depth != 1)
      return;

   /* Do this before we start doing stuff with the hash tables, just in case
    * allocations within the symbol table stuff ends up modifying them.
    * (mallocs there are fine, because they won't be recorded, but reallocs and
    * frees are more problematic.  */
   reflect_symtab_create();

   /* Count the number of changed entries in the hash table.  */
   size_t stack_hash_live = 0;
   for (StackTrace ** bucket = stack_hash;
	bucket != stack_hash + STACK_HASH_SIZE; ++bucket) {
      /* Iterate over this hash chain.  */
      for (StackTrace * it = *bucket; it; it = it->next)
         if (it->bytes)
            ++stack_hash_live;
   }

   ReportItem * report_array
      = __libc_malloc (stack_hash_live * sizeof (ReportItem));
   if (report_array == NULL) {
      /* We don't use fprintf(stderr) here as we may conceivably be called from
       * within such a printf!  */
      write (1, "mtrace: cannot report (Out of memory).\n", 39);
      need_report = 0;
      return;
   }

   ReportItem * report_array_end = report_array;

   /*** Iterate over the hash table.  ***/
   size_t total = 0;
   for (StackTrace ** bucket = stack_hash;
	bucket != stack_hash + STACK_HASH_SIZE; ++bucket) {
      /* Iterate over this hash chain. */
      StackTrace * next;
      for (StackTrace * it = *bucket; it; it = next) {
	 next = it->next;

         if (it->bytes == 0)
            continue;           /* Ignore this one. */

         /* Create report entry.  In theory this could corrupt our data
	    structures by freeing memory and changing stuff underneath us, but
	    in reality all the MM here should occur at depth > 1 and be
	    ignored.  */
         const char * string = reflect_symtab_format (it->stack,
						      STACK_SIZE,
						      report_offsets);
         if (string == NULL)
            continue;           // OOM.

         total += it->bytes;
         report_array_end->string = string;
         report_array_end->bytes = it->bytes;
         ++report_array_end;

         /* Reset the stack record.  */
         it->bytes = 0;

         if (it->refs == 0)
	    free_StackTrace (it); /* This item is dead; remove it.  */
      }
   }

   reflect_symtab_destroy();

   if (report_array != report_array_end) {
      /* Sort the array by string.  */
      qsort (report_array,
             report_array_end - report_array,
             sizeof (ReportItem),
             compare_by_string);

      /* Collapse items with identical strings.  */
      ReportItem * p = report_array;
      for (ReportItem * q = report_array + 1; q != report_array_end; ++q) {
         if (strcmp (p->string, q->string) == 0) {
            /* Collapse.  */
            p->bytes += q->bytes;
            free ((void *) q->string);
         }
         else
            /* No collapse; advance p.  */
            *++p = *q;
      }

      report_array_end = p + 1;

      /*** Now sort by number of bytes.  ***/
      qsort (report_array,
             report_array_end - report_array,
             sizeof (ReportItem),
             compare_by_bytes);
   }

   /*** Print the report.  ***/
   char * file_name;
   static int report_count;
   if (asprintf (&file_name, "%s-%i-%i.memlog",
		 program_invocation_short_name, getpid(), ++report_count) < 0)
      goto cleanup;

   FILE * output_file = fopen (file_name, "w");
   free (file_name);
   if (output_file == NULL)
      goto cleanup;

   fprintf (output_file, "Outanding bytes: %i (%+i)\n", global_bytes, total);
   for (ReportItem * p = report_array; p != report_array_end; ++p) {
      /* There may be some items with 0 bytes, due to collapsing items with
	 identical strings.  Make sure we skip these.  */
      if (p->bytes != 0) {
         fprintf (output_file, "%+i\n", p->bytes);
         fputs (p->string, output_file);
      }
   }

   fclose (output_file);

 cleanup:
   /* Free all the memory.  */
   for (ReportItem * p = report_array; p != report_array_end; ++p)
      __libc_free ((void *) p->string);

   __libc_free (report_array);

   need_report = 0;

   return;
}

static void start (void) __attribute__ ((constructor));
static void stop (void);

static void handler (int signal)
{
   need_report = 1;
}

void start (void)
{
   struct sigaction action;
   action.sa_handler = handler;
   action.sa_flags = SA_RESTART;
   sigemptyset (&action.sa_mask);

   sigaction (SIGUSR1, &action, NULL);

   const char * offset_string = getenv ("MTRACE_OFFSETS");
   report_offsets = (offset_string != NULL && *offset_string != 0);

   atexit (stop);
}

void stop (void)
{
   enter();

   print_report();

   leave();
}
