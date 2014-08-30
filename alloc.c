
#include "node.h"

#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <sys/mman.h>

/* The node allocator.
 *
 * We bypass malloc() so that we can be safely used from a signal handler.
 */

/* Allocate memory in 1meg chunks. */
#define ALLOC_BYTES 1048576
#define ALLOC_NODES ((ALLOC_BYTES - sizeof (size_t)) / sizeof (scg_node_t))

typedef struct {
    volatile size_t next;
    scg_node_t      array[ALLOC_NODES];
} alloc_buffer_t;

static alloc_buffer_t initial_buffer;
/* Actually alloc_buffer_t*, but using void* avoids type-punning
   warnings when calling the atomic ops.  */
static void * volatile alloc_buffer = &initial_buffer;

scg_node_t * scg_allocate_node (void)
{
    while (1) {
        alloc_buffer_t * buffer = alloc_buffer;
        void * new_buffer;
        int errno_save;

        size_t index = buffer->next;
        if (index < ALLOC_NODES) {
            if (__atomic_compare_exchange_n (
                    &buffer->next, &index, index + 1,
                    true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
                return &buffer->array[index];

            /* Someone else got in before us - retry */
            continue;
        }

        /* We need a new buffer.  FIXME error handling.  */
        errno_save = errno;
        new_buffer = mmap (NULL, ALLOC_BYTES,
                           PROT_EXEC | PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANON, 0, 0);

        if (!__atomic_compare_exchange_n (
                &alloc_buffer, &buffer, new_buffer,
                true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            /* Someone else got in before us - retry. */
            munmap (new_buffer, ALLOC_BYTES);

        errno = errno_save;
    }
}
