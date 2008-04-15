
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
    const void * address;               /* Start of first PT_LOAD segment.  */
    size_t       size;                /* Size to cover all PT_LOAD segments.  */
    ssize_t      delta;                 /* mapped address - object address.  */

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
    ElfObject * it = realloc (elf_object_array,
                              (elf_object_count + 1) * sizeof (ElfObject));
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


static Elf_Scn * get_elf_section (Elf * elf, uint32_t type,
                                  const char * name,
                                  GElf_Shdr * header)
{
   Elf_Scn * section = NULL;

   size_t shstrndx;		/* Section Header STRings iNDeX.  */
   if (elf_getshstrndx (elf, &shstrndx) < 0)
      return NULL;

   while ((section = elf_nextscn (elf, section))) {
      gelf_getshdr (section, header);
      if (header->sh_type != type)
          continue;

      if (name == NULL)
          break;

      const char * n = elf_strptr (elf, shstrndx, header->sh_name);
      if (n != NULL && strcmp (n, name) == 0)
	 break;
   }

   return section;
}


/* Try and open the elf object.  */
static Elf * open_elf (const char * path, int * fd)
{
    *fd = open (path, O_RDONLY);
    if (*fd == -1) {
        fprintf (stderr, "Failed to open %s: %s\n", path, strerror (errno));
        return NULL;
    }

    Elf * elf = elf_begin (*fd, ELF_C_READ_MMAP, NULL);
    if (elf == NULL) {
        close (*fd);
        *fd = -1;
    }

    return elf;
}


static void close_elf (Elf * elf, int fd)
{
    if (elf != NULL)
        elf_end (elf);

    if (fd >= 0)
        close (fd);
}


/* Follow .gnu_debuglink if possible.  */
static Elf * get_debuglink (ElfObject * it, int * dbgfd)
{
    GElf_Shdr temp;
    Elf_Scn * section = get_elf_section (it->elf, SHT_PROGBITS,
                                         ".gnu_debuglink", &temp);
    if (section == NULL)
        return NULL;

    /* Ok, we have a .gnu_debuglink section.  It starts with a file name.
       FIXME: validation and checksum checking not done...  */
    Elf_Data * data = elf_getdata (section, NULL);
    if (data == NULL)
        return NULL;

    /* Now create the debug file name: /usr/lib/debug/ +
       dirname(it->file) + debuglink */
    const char * filename_slash = strrchr (it->filename, '/');
#ifdef DEBUG
    fprintf (stderr, "Library filename = %s\n", it->filename);
#endif
    if (filename_slash == NULL)
        return NULL;

    char * debug_path;
    if (asprintf (&debug_path, "/usr/lib/debug/%.*s%s",
                  filename_slash - it->filename + 1, it->filename,
                  (const char *) data->d_buf) < 0)
        return NULL;

#ifdef DEBUG
    fprintf (stderr, "debuglink(%s) = %s\n", it->name, debug_path);
#endif

    /* Now try and load the debug info elf object.  */
    Elf * debug_elf = open_elf (debug_path, dbgfd);
    free (debug_path);

    return debug_elf;
}


/* Update it->delta, just in case elf and debug_elf have different
 * base addresses.  E.g., because of prelinking....  */
static void replace_elf (ElfObject * it, Elf * elf, int fd)
{
    GElf_Ehdr old_header;
    GElf_Ehdr new_header;
    it->delta += gelf_getehdr (it->elf, &old_header)->e_entry
        -         gelf_getehdr (elf, &new_header)->e_entry;
    close_elf (it->elf, it->fd);
    it->elf = elf;
    it->fd = fd;
}


/* Get an elf object symbol table.  */
static Elf_Scn * get_symtab (ElfObject * it, GElf_Shdr * header)
{
    it->elf = open_elf (it->filename, &it->fd);
    if (it->elf == NULL) {
        it->filename = NULL;
        return NULL;
    }

    Elf_Scn * section = get_elf_section (it->elf, SHT_SYMTAB, NULL, header);
    if (section != NULL) {
//#ifdef DEBUG
        fprintf (stderr, "%s: SYMTAB\n", it->name);
//#endif
        return section;
    }

    int debug_fd;
    Elf * debug = get_debuglink (it, &debug_fd);

    if (debug != NULL) {
        section = get_elf_section (debug, SHT_SYMTAB, NULL, header);
        if (section != NULL) {
//#ifdef DEBUG
            fprintf (stderr, "%s: SYMTAB (debuginfo)\n", it->name);
//#endif
            replace_elf (it, debug, debug_fd);
            return section;
        }
    }

    section = get_elf_section (it->elf, SHT_DYNSYM, NULL, header);
    if (section != NULL)
//#ifdef DEBUG
        fprintf (stderr, "%s: DYNSYM\n", it->name);
//#endif

    if (section == NULL && debug != NULL) {
        section = get_elf_section (debug, SHT_DYNSYM, NULL, header);
        if (section != NULL) {
//#ifdef DEBUG
            fprintf (stderr, "%s: DYNSYM (debuginfo)\n", it->name);
//#endif
            replace_elf (it, debug, debug_fd);
            return section;
        }
    }

    close_elf (debug, debug_fd);
    return section;
}


static void fill_in_elf_object (ElfObject * it)
{
    /* If we're already filled in, or we've already failed, do nothing.  */
    if (it->fd != -1 || it->filename == NULL)
        return;

#ifdef DEBUG
    fprintf (stderr, "Loading elf object %s\n", it->name);
#endif

    GElf_Shdr section_header;
    Elf_Scn * section = get_symtab (it, &section_header);
    if (section == NULL) {
        fprintf (stderr, "No smbol data in %s\n", it->name);
        goto failed;
    }

    /* Get the symbol table data.  */
    Elf_Data * symbol_data = elf_getdata (section, NULL);
    if (symbol_data == NULL) {
        fprintf (stderr, "No section data in %s\n", it->name);
        goto failed;
    }

    /* Number of symbols.  We won't actually be interested in them all, but it's
     * not going to be excessively large.  */
    size_t symbol_count = section_header.sh_size / section_header.sh_entsize;
    if (symbol_count <= 0) {
        fprintf (stderr, "No symbols in %s\n", it->name);
        goto failed;
    }

    /* We count the exact number of symbols we're interested in; saves us 4k
     * entries on libc.  */
    size_t wanted = 0;
    for (size_t i = 0; i != symbol_count; ++i) {
        GElf_Sym symbol;
        gelf_getsym (symbol_data, i, &symbol);
        /* We're only interested in symbols that are defined functions.  Ignore
         * others.  */
        if ((GELF_ST_TYPE (symbol.st_info) == STT_FUNC ||
             GELF_ST_TYPE (symbol.st_info) == STT_OBJECT)
             && symbol.st_value != 0)
            ++wanted;
    }

    /* Now build the symbol array.  */
    it->symbols = malloc (wanted * sizeof (ElfSymbol));
    if (it->symbols == NULL) {
        fprintf (stderr, "Malloc symbol array failed in %s\n", it->name);
        goto failed;
    }
    it->symbols_count = 0;

    for (size_t i = 0; i != symbol_count; ++i) {
        GElf_Sym symbol;
        gelf_getsym (symbol_data, i, &symbol);
        ElfSymbol * s = &it->symbols[it->symbols_count];

        /* We're only interested in symbols that are defined functions.  It
         * appears that some real functions get marked as undefined!  So test
         * for symbol value != 0 instead.  */
        if ((GELF_ST_TYPE (symbol.st_info) != STT_FUNC &&
             GELF_ST_TYPE (symbol.st_info) != STT_OBJECT)             
            || symbol.st_value == 0)
            continue;

        /* st_value is 64 bits in the gelf stuff, so need to cast to
           avoid warnings.  */
        s->address = ((char *) (size_t) symbol.st_value) + it->delta;
        s->size = symbol.st_size;
        if (s->size == 0)
            s->size = 16;
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
    fprintf (stderr, "Grokked %u symbols out of %u for %s\n",
             it->symbols_count, symbol_count, it->name);
/*    for (size_t i = 0; i != it->symbols_count; ++i) */
/*       fprintf (stderr, "\t%p %5i\t%s\n", */
/* 	       it->symbols[i].address, */
/* 	       it->symbols[i].size, */
/* 	       it->symbols[i].name); */
#endif
    return;

failed:
    close_elf (it->elf, it->fd);
    it->elf = NULL;
    it->fd = -1;
    it->filename = NULL;
}


/* Destroy the symbol table.  */
void reflect_symtab_destroy (void)
{
    /* Free each item in the array.  */
    for (unsigned int i = 0; i != elf_object_count; ++i) {
        ElfObject * o = &elf_object_array[i];

        free (o->symbols);
        close_elf (o->elf, o->fd);
    }

    /* And free the array storage.  */
    free (elf_object_array);
    elf_object_array = NULL;
    elf_object_count = 0;
}


/* Lookup object symbol and offset for an address.  object and/or
 * symbol may be set to NULL.  */
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
        fprintf (stderr, "%p: %s %p %i\n", address, s->name, s->address, s->size);
//        fprintf (stderr, "Lookup : out of range\n");
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
