
#include <ctype.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "symboltable.h"

static void process (const char * filename)
{
  int fd = open (filename, O_RDONLY);

  if (fd == -1) {
    perror ("open");
    exit (EXIT_FAILURE);
  }

  Elf * elf = elf_begin (fd, ELF_C_READ_MMAP, NULL);
  if (elf == NULL) {
    fprintf (stderr, "Cannot get Elf descriptor: %s\n", elf_errmsg (-1));
    exit (1);
  }

  if (elf_kind (elf) != ELF_K_ELF) {
    fprintf (stderr, "File is not ELF.\n");
    exit (1);
  }

  unsigned section_count;		/* Number of sections.  */
  if (elf_getshnum (elf, &section_count) < 0) {
    fprintf (stderr, "Cannot get number of sections: %s", elf_errmsg (-1));
    exit (1);
  }

  /* Try for a symtab.  */
  Elf_Scn * section = NULL;
  GElf_Shdr   shdr_mem;		/* Section header.  */
  GElf_Shdr * shdr;

  while ((section = elf_nextscn (elf, section))) {
    shdr = gelf_getshdr (section, &shdr_mem);
    if (shdr->sh_type == SHT_SYMTAB)
      break;
  }
  if (section == NULL)
    /* If no symtab, try for dynsym.  */
    while ((section = elf_nextscn (elf, section))) {
      shdr = gelf_getshdr (section, &shdr_mem);
      if (shdr->sh_type == SHT_DYNSYM)
	break;
    }

  if (section == NULL) {
    fprintf (stderr, "Cannot find symbol table.\n");
    exit (1);
  }

  /* Get the symbol table data.  */
  Elf_Data *  data = elf_getdata (section, NULL);
  if (data == NULL) {
    fprintf (stderr, "Cannot get symbol table data.\n");
    exit (EXIT_FAILURE);
  }

  /* Get the section header string table index.  */
  size_t shstrndx;		/* Section Header STRings iNDeX.  */
  if (elf_getshstrndx (elf, &shstrndx) < 0) {
    fprintf (stderr, "Cannot get string table section index.\n");
    exit (EXIT_FAILURE);
  }

  size_t num_syms = shdr->sh_size / shdr->sh_entsize;

  printf ("Symbol table %s has %u entries.\n",
	  elf_strptr (elf, shstrndx, shdr->sh_name), num_syms);

  GElf_Shdr glink;		/* Temporary section header use for links.  */

  printf (" %lu local symbols  String table: [%u] '%s'\n",
	  (unsigned long) shdr->sh_info,
	  (unsigned) shdr->sh_link,
	  elf_strptr (elf, shstrndx,
		      gelf_getshdr (elf_getscn (elf, shdr->sh_link),
				    &glink)->sh_name));

  for (size_t i = 0; i != num_syms; ++i) {
      GElf_Sym sym_mem;
      GElf_Sym * sym = gelf_getsym (data, i, &sym_mem);

      if (GELF_ST_TYPE (sym->st_info) == STT_FUNC &&
	  sym->st_shndx != SHN_UNDEF)
	printf ("%6i: 0x%08llx %5lld\t%s \t%s\n",
		i, sym->st_value, sym->st_size,
		elf_strptr (elf, shstrndx,
			    gelf_getshdr (elf_getscn (elf, sym->st_shndx),
					  &glink)->sh_name),
		elf_strptr (elf, shdr->sh_link, sym->st_name));
  }

  if (elf_end (elf) != 0) {
    fprintf (stderr, "Cannot close Elf descriptor.\n");
    exit (EXIT_FAILURE);
  }

  close (fd);
}

#include <errno.h>
#include <link.h>

static int do_a_phdr (struct dl_phdr_info * phdr, size_t size, void * ignored)
{
  printf ("%3i %08x %s\n",
	  phdr->dlpi_phnum,
	  phdr->dlpi_addr,	/* Add to file addresses to get mapped?  */
	  phdr->dlpi_name);
  for (int i = 0; i != phdr->dlpi_phnum; ++i) {
    const ElfW(Phdr) * seg = &phdr->dlpi_phdr[i];
    printf ("    %8x %6x %08x %08x %5i %5i %3i %3i\n",
	    seg->p_type,
	    seg->p_offset,
	    seg->p_vaddr,
	    seg->p_paddr,
	    seg->p_filesz,
	    seg->p_memsz,
	    seg->p_flags,
	    seg->p_align);
  }
  return 0;
}

static void crazy_shit (void)
{
  printf ("%s\n", program_invocation_name);
  printf ("%s\n", program_invocation_short_name);

  dl_iterate_phdr (do_a_phdr, NULL);
}

int main (int argc, char ** argv)
{
  int i;

  if (argc == 1)
    crazy_shit();

  reflect_symtab_create();

  elf_version (EV_CURRENT);

  for (i = 1; i < argc; ++i) {
    const char * object;
    const char * symbol;
    size_t       offset;
    const void * address;

    if (!isdigit (argv[i][0])) {
      process (argv[i]);
      continue;
    }

    /* Symbol table fun!!!  */
    address = (void *) strtoul (argv[i], NULL, 0);
    reflect_symtab_lookup (&object, &symbol, &offset, address);

    if (object == NULL)
      object = "(none)";
    if (symbol == NULL)
      symbol = "(none)";

    printf ("%p : %s\t%s\t%u\n", address, object, symbol, offset);
    
  }

  reflect_symtab_destroy();

  return 0;
}
