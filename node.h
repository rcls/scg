/* The SCG records stack traces as linked lists.
 *
 * Each node in the linked list consists of a return stack address, and
 * a link to the next node.  The list starts at the outermost stack frame.
 *
 */

#ifndef SCG_NODE_H_
#define SCG_NODE_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef void * scg_address_t;

typedef struct scg_node_t {
   scg_address_t       address;   /* Return address from stack frame. */
   struct scg_node_t * next;      /* Next on stack frame. */

   /* We use non-locking operations to modify counter; hence it is volatile. */
   volatile unsigned long       counter;   /* Number of hits. */

   /* Link pointer for hash table.  It is volatile because we use non-locking
    * operations to extend that hash table.  */
   struct scg_node_t * volatile hash_link;
} scg_node_t;

/* Hash table has 1048675 entries... */
#define SCG_NODE_HASH_ORDER 20
#define SCG_NODE_HASH_SIZE (1 << 20)

extern scg_node_t * volatile scg_node_hash[SCG_NODE_HASH_SIZE];

scg_node_t * scg_allocate_node();

#ifdef __cplusplus
}
#endif

#endif
