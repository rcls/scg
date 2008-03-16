
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <link.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "symboltable.h"

/* Do-it-ourself symbol table handling using libelf.  */

typedef struct ElfSymbol
{
   const void * address;	/* The actual address in memory.  */
   size_t       size;		/* Size of the symbol.  */
   const char * name;
} ElfSymbol;

/* A struct representing an ELF object in memory.  */
typedef struct ElfObject
{
   const void * address;	/* Start of first PT_LOAD segment.  */
   size_t       size;		/* Size to cover all PT_LOAD segments.  */
   ssize_t      delta;		/* mapped address - object address.  */

   /* Name and file name.  filename is set to NULL on load failure.  */
   const char * name;
   const char * filename;

   /* libelf object.  Maybe null.  */
   Elf *        elf;
   /* File descriptor.  -1 means none.  */
   int          fd;

   /* Number and array of symbols.  */
   size_t       symbols_count;
   ElfSymbol *  symbols;
} ElfObject;

/* Storage for the known elf objects.  */
static ElfObject *  elf_object_array;
static unsigned int elf_object_count;

/* Try and get the path name to the main program.   */
static const char * main_program_path (void)
{
   static char saved [FILENAME_MAX + 1];
   if (saved[0] == '\0')
      readlink ("/proc/self/exe", saved, FILENAME_MAX);
   return saved;
}

/* Append one elf object to the array.  */
static int build_elf_object_1 (struct dl_phdr_info * info,
			       size_t size, void * unused)
{
   /* Reallocate the array.  We're not performance critical, so reallocing
      item by item is fine.  */
   ElfObject * it
      = realloc (elf_object_array, (elf_object_count + 1) * sizeof (ElfObject));
   if (it == NULL)
      return 1;

   elf_object_array = it;
   it += elf_object_count++;

   /* The dlpi_addr field appears to be a misnomer.  It appears to be the
      difference between the object's address and the mapped address.  */
   it->delta = info->dlpi_addr;

   /* Find the address range...  */
   size_t min_vaddress = (size_t) -1;
   size_t max_vaddress = 0;
   for (int i = 0; i < info->dlpi_phnum; ++i) {
      const ElfW(Phdr) * header = &info->dlpi_phdr[i];
      if (header->p_type != PT_LOAD)
	 continue;

      if (min_vaddress > header->p_vaddr)
	 min_vaddress = header->p_vaddr;

      if (max_vaddress < header->p_vaddr + header->p_memsz)
	 max_vaddress = header->p_vaddr + header->p_memsz;
   }

   it->address = ((char *) min_vaddress) + it->delta;
   it->size = max_vaddress - min_vaddress;

   /* Assume that no name is the main program...  */
   it->name = info->dlpi_name;
   it->filename = it->name;
   if (it->name == NULL || it->name[0] == '\0') {
      it->name = program_invocation_short_name;
      it->filename = main_program_path();
   }

   it->elf = 0;
   it->fd = -1;
   it->symbols_count = 0;
   it->symbols = NULL;

#ifdef DEBUG
   fprintf (stderr, "%s at %p size %u delta %x\n",
	    it->name, it->address, it->size, it->delta);
#endif

   return 0;
}

/* Comparison function for sorting the array of elf objects.  */
static int compare_elf_object (const void * a, const void * b)
{
   const ElfObject * aa = a;
   const ElfObject * bb = b;
   return aa->address == bb->address ? 0 :
      aa->address < bb->address ? -1 : 1;
}

/* Create the array of elf objects.  */
void reflect_symtab_create (void)
{
   elf_version (EV_CURRENT);

   if (elf_object_array != NULL)
      reflect_symtab_destroy();

   dl_iterate_phdr (build_elf_object_1, NULL);

   /* Sort it so we can look up by binary search.  */
   qsort (elf_object_array, elf_object_count, sizeof (ElfObject),
	  compare_elf_object);

   return;
}

/* Comparison function for sorting an array of symbols.  */
static int compare_elf_symbol (const void * a, const void * b)
{
   const ElfSymbol * aa = a;
   const ElfSymbol * bb = b;
   return aa->address == bb->address ? 0 :
      aa->address < bb->address ? -1 : 1;
}

/* Try and open the elf object, following .gnu_debuglink if possible.  */
static void open_elf_object (ElfObject * it)
{
   it->elf = NULL;

   it->fd = open (it->filename, O_RDONLY);
   if (it->fd == -1)
      return;

   it->elf = elf_begin (it->fd, ELF_C_READ_MMAP, NULL);
   if (it->elf == NULL)
      return;

   size_t shstrndx;		/* Section Header STRings iNDeX.  */
   if (elf_getshstrndx (it->elf, &shstrndx) < 0)
      return;

   /* Now look for PROGBITS section .gnu_debuglink.  */
   Elf_Scn * section = NULL;
   while ((section = elf_nextscn (it->elf, section))) {
      GElf_Shdr  section_head;
      gelf_getshdr (section, &section_head);
      if (section_head.sh_type != SHT_PROGBITS)
	 continue;

      const char * name = elf_strptr (it->elf, shstrndx, section_head.sh_name);
      if (name != NULL && strcmp (name, ".gnu_debuglink") == 0)
	 break;
   }

   if (section == NULL)
      return;

   /* Ok, we have a .gnu_debuglink section.  It starts with a file
      name.  FIXME: validation and checksum checking not done...  */
   Elf_Data * data = elf_getdata (section, NULL);
   if (data == NULL)
      return;

   /* Now create the debug file name: /usr/lib/debug/ +
      dirname(it->file) + debuglink */
   const char * filename_slash = strrchr (it->filename, '/');
   if (filename_slash == NULL)
      return;

   char * debug_filename;
   if (asprintf (&debug_filename, "/usr/lib/debug%.*s%s",
		 filename_slash - it->filename + 1, it->filename,
		 (const char *) data->d_buf) < 0)
      return;

#ifdef DEBUG
   fprintf (stderr, "debuglink(%s) = %s\n", it->name, debug_filename);
#endif

   /* Now try and load the debug info elf object.  */
   int debug_fd = open (debug_filename, O_RDONLY);
   free (debug_filename);
   if (debug_fd < 0)
      return;

   Elf * debug_elf = elf_begin (debug_fd, ELF_C_READ_MMAP, NULL);
   if (debug_elf == NULL) {
      close (debug_fd);
      return;
   }

   /* Update it->delta, just in case elf and debug_elf have different
      base addresses.  E.g., because of prelinking....  */
   GElf_Ehdr orig_header;
   GElf_Ehdr debug_header;
   it->delta += gelf_getehdr (it->elf, &orig_header)->e_entry
      -         gelf_getehdr (debug_elf, &debug_header)->e_entry;

   elf_end (it->elf);
   close (it->fd);
   it->elf = debug_elf;
   it->fd = debug_fd;
}

/* Build the symbol table in an ElfObject.   */
static void fill_in_elf_object (ElfObject * it)
{
   /* If we're already filled in, or we've already failed, do nothing.  */
   if (it->fd != -1 || it->filename == NULL)
      return;

#ifdef DEBUG
   fprintf (stderr, "Loading elf object %s\n", it->name);
#endif

   open_elf_object (it);
   if (it->elf == NULL)
      goto failed;

   /* Look for a symtab.  */
   Elf_Scn *  section = NULL;
   GElf_Shdr  section_header;
   while ((section = elf_nextscn (it->elf, section))) {
      gelf_getshdr (section, &section_header);
      if (section_header.sh_type == SHT_SYMTAB)
	 break;
   }
   if (section == NULL)
      /* If no symtab, try for dynsym.  */
      while ((section = elf_nextscn (it->elf, section))) {
	 gelf_getshdr (section, &section_header);
	 if (section_header.sh_type == SHT_DYNSYM)
	    break;
      }
   if (section == NULL)
      goto failed;

   /* Get the symbol table data.  */
   Elf_Data * symbol_data = elf_getdata (section, NULL);
   if (symbol_data == NULL)
      goto failed;

   /* Number of symbols.  We won't actually be interested in them all, but
      it's not going to be excessively large.  */
   size_t symbol_count = section_header.sh_size / section_header.sh_entsize;
   if (symbol_count <= 0)
      goto failed;

   /* We count the exact number of symbols we're interested in; saves us 4k
      entries on libc.  */
   size_t wanted = 0;
   for (size_t i = 0; i != symbol_count; ++i) {
      GElf_Sym symbol;
      gelf_getsym (symbol_data, i, &symbol);
      /* We're only interested in symbols that are defined functions.
	 Decrement symbol_count for the others.  */
      if ((GELF_ST_TYPE (symbol.st_info) == STT_FUNC ||
	   GELF_ST_TYPE (symbol.st_info) == STT_OBJECT)
	  && symbol.st_shndx != SHN_UNDEF)
	 ++wanted;
   }

   /* Now build the symbol array.  */
   it->symbols = malloc (wanted * sizeof (ElfSymbol));
   it->symbols_count = 0;

   for (size_t i = 0; i != symbol_count; ++i) {
      GElf_Sym symbol;
      gelf_getsym (symbol_data, i, &symbol);
      ElfSymbol * s = &it->symbols[it->symbols_count];

      /* We're only interested in symbols that are defined functions.  */
      if ((GELF_ST_TYPE (symbol.st_info) != STT_FUNC &&
	   GELF_ST_TYPE (symbol.st_info) != STT_OBJECT)
	  || symbol.st_shndx == SHN_UNDEF)
	 continue;

      /* st_value is 64 bits in the gelf stuff, so need to cast to
	 avoid warnings.  */
      s->address = ((char *) (size_t) symbol.st_value) + it->delta;
      s->size = symbol.st_size;
      s->name = elf_strptr (it->elf,
			    section_header.sh_link,
			    symbol.st_name);

      ++it->symbols_count;
   }

   assert (it->symbols_count == wanted);

   /* Sort the symbols by address so we can do a binary search later.  */
   qsort (it->symbols, it->symbols_count, sizeof (ElfSymbol),
	  compare_elf_symbol);

#ifdef DEBUG
   fprintf (stderr, "Grokked %u symbols out of %u\n",
	    it->symbols_count, symbol_count);
/*    for (size_t i = 0; i != it->symbols_count; ++i) */
/*       fprintf (stderr, "\t%p %5i\t%s\n", */
/* 	       it->symbols[i].address, */
/* 	       it->symbols[i].size, */
/* 	       it->symbols[i].name); */

   fprintf (stderr, "Loading elf object %s success\n", it->name);
#endif

   return;

 failed:
#ifdef DEBUG
   fprintf (stderr, "Loading elf object %s FAILED\n", it->name);
#endif
   if (it->elf) {
      elf_end (it->elf);
      it->elf = NULL;
   }
   if (it->fd >= 0) {
      close (it->fd);
      it->fd = -1;
   }
   it->filename = NULL;
}

/* Destroy the symbol table.  */
void reflect_symtab_destroy (void)
{
   /* Free each item in the array.  */
   for (unsigned int i = 0; i != elf_object_count; ++i) {
      ElfObject * o = &elf_object_array[i];

      if (o->elf)
	 elf_end (o->elf);

      if (o->symbols)
	 free (o->symbols);

      if (o->fd >= 0)
	 close (o->fd);
   }

   /* And free the array storage.  */
   free (elf_object_array);
   elf_object_array = NULL;
   elf_object_count = 0;
}

/* Lookup object symbol and offset for an address.  object and/or
   symbol may be set to NULL.  */
void reflect_symtab_lookup (const char ** object,
			    const char ** symbol,
			    size_t *      offset,
			    const void *  address)
{
   *object = NULL;
   *symbol = NULL;
   *offset = (size_t) address;

   if (elf_object_count == 0) {
#ifdef DEBUG
      fprintf (stderr, "Lookup : no objects\n");
#endif
      return;			/* No elf objects...  */
   }

   /* Binary search for the object.  */
   ElfObject * o = elf_object_array;
   size_t range = elf_object_count;
   while (range > 1)
      if (o[range / 2].address <= address) {
	 o += range / 2;
	 range -= range / 2;
      }
      else
	 range /= 2;

   if ((size_t) (((char *) address) - ((char *) o->address)) > o->size) {
#ifdef DEBUG
      fprintf (stderr, "Lookup : object not found\n");
#endif
      return;			/* Not found.  */
   }

   fill_in_elf_object (o);

   *object = o->name;
   *offset = ((char *) address) - ((char *) o->address);

   /* Now try for the symbol.  */
   if (o->symbols_count == 0) {
#ifdef DEBUG
      fprintf (stderr, "Lookup : no symbols\n");
#endif
      return;
   }

   ElfSymbol * s = o->symbols;
   range = o->symbols_count;
   while (range > 1)
      if (s[range / 2].address <= address) {
	 s += range / 2;
	 range -= range / 2;
      }
      else
	 range /= 2;

   if ((size_t) (((char *) address) - ((char *) s->address)) > s->size) {
#ifdef DEBUG
      fprintf (stderr, "%s %p %i\n", s->name, s->address, s->size);
      fprintf (stderr, "Lookup : out of range\n");
#endif
      return;
   }

   *symbol = s->name;
   *offset = ((char *) address) - ((char *) s->address);
}

char * reflect_symtab_format (const void * const * addresses,
			      size_t               count,
			      int                  verbose)
{
   char * result;
   size_t result_size;

   FILE * f = open_memstream (&result, &result_size);
   if (f == NULL)
      return NULL;

   for (size_t i = 0; i != count && addresses[i] != NULL; ++i) {
      const char * object;
      const char * symbol;
      size_t       offset;

      reflect_symtab_lookup (&object, &symbol, &offset, addresses[i]);

      if (verbose && symbol)
	 fprintf (f, "\t%s+%i\t(%s)\n", symbol, offset, object);
      else if (verbose && object)
	 fprintf (f, "\t%s+%#x\n", object, offset);
      else if (symbol)
	 fprintf (f, "\t%s\t(%s)\n", symbol, object);
      else if (object)
	 fprintf (f, "\t%s\n", object);
      else
	 fprintf (f, "\t%p\n", (void *) offset);
   }

   if (fclose (f) != 0)
      return NULL;

   return result;
}
