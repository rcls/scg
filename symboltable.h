#ifndef SYMBOL_TABLE_H_
#define SYMBOL_TABLE_H_

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the symbol table data structures.  */
void reflect_symtab_create (void);
/* Destroy the symbol table data structures.  */
void reflect_symtab_destroy (void);
/* Look up address.  object is set to non-NULL if the elf object is
   found, symbol is set to non-NULL if a symbol covering the address
   is found.  offset is relative to the symbol if that's found, else
   object, or relative to NULL if neither found.  */
void reflect_symtab_lookup (const char ** object,
			    const char ** symbol,
			    size_t *      offset,
			    const void *  address);
/* Format a list of addresses, one per line, into a malloc'd buffer.  */
char * reflect_symtab_format (const void * const * addresses,
			      size_t               count,
			      int                  verbose);

#ifdef __cplusplus
}
#endif

#endif
