
/* To get Dl_info stuff ... */

#define _GNU_SOURCE 1

#include "node.h"
#include "scg.h"
#include "symboltable.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

struct scg_function_record;

/* Using the normal STL iterators seems to blow up if we are run during
 * shutdown.  Using malloc seems to be better behaved. */

template <class E>
struct malloc_allocator :
   std::__allocator <E, std::__malloc_alloc_template<0> >
{ };

template <class K, class V, class C = std::less <K> >
struct malloc_map :
   std::map <K, V, C, malloc_allocator <std::pair <K, V> > >
{ };

typedef malloc_map <scg_function_record *, size_t> record_counts;

// Work around non-standard G++-2 lib...
#if __GNUC__ == 2
#define char_traits string_char_traits
#endif

typedef std::basic_string <char, std::char_traits<char>,
                           malloc_allocator <char> > scg_string;

struct scg_function_record {
   scg_function_record() :
      address (NULL),
      call_count (0),
      terminal_count (0)
      { }

   scg_function_record (const scg_string & n,
                        scg_address_t      a) :
      name (n),
      address (a),
      call_count (0)
      { }

   scg_string    name;
   scg_address_t address;

   record_counts caller_counts;
   record_counts callee_counts;

   typedef std::vector <int> int_vector;
   // The number of times that we have occured at least once on the stack.
   int            call_count;
   // The number of times that we have occured as the innermost element on the
   // stack.
   int            terminal_count;
   // Call_counts[i] is number of sample with the function occuring i+1
   // times in the stack.
   int_vector     call_count_breakdown;

   // Print to out_file.
   void output (FILE *        out_file,
                unsigned long total_samples) const;
};

struct scg_database {
   scg_database() :
      spontaneous ("<spontaneous>", NULL),
      total_samples (0)
      { }

   // Function records indexed by base address.
   typedef std::map <scg_address_t, scg_function_record> record_map;
   record_map  records;

   // Function records indexed by return address.
   typedef std::map <scg_address_t, scg_function_record *> recordp_map;
   recordp_map canonicalisers;

   // Convert a return address to a record.
   scg_function_record & address_to_record (scg_address_t address);

   // Add node into database.
   void process_node (const scg_node_t & node,
                      unsigned long      counter);

   // Add entire hash table.
   void build_from (scg_node_t * volatile * hash_table,
                    size_t                  hash_table_size);

   // The '<spontaneous>' record.
   scg_function_record   spontaneous;

   // Total number of samples in database.
   unsigned long         total_samples;

   // Print to stderr.
   void output (FILE * out_file) const;
};

scg_function_record & scg_database::address_to_record (scg_address_t address)
{
   recordp_map::iterator i = canonicalisers.find (address);
   if (i != canonicalisers.end())
      return *i->second;

   const char * object;
   const char * name;
   size_t       offset;
   char         fake_name[20];

   reflect_symtab_lookup (&object, &name, &offset, address);

   if (name == NULL) {
      // The address was not found, so we fake it.
      sprintf (fake_name, "%p", address);
      name = fake_name;
      offset = 0;
   }

   address = (char *) address - offset;	// Base address.

   scg_function_record & result = records[address];

   if (result.address == NULL) {
      /* This is a new record; initialise it. */
      result.address = address;
      result.name = name;
//      fprintf (stderr, "New function: %s\n", info.dli_sname);
   }

//   fprintf (stderr, "New address : %p\n", address);
   return result;
}

void scg_database::process_node (const scg_node_t & node,
                                 unsigned long      counter)
{
//   fprintf (stderr, "Node counter is %li\n", counter);

   // Walk through the stack adding in the caller and callee counts.  We have a
   // fake '<spontaneous>' entry for the 'caller' of the stack top.
   record_counts  occur_counts;
   scg_function_record * caller = &spontaneous;
   for (const scg_node_t * i = &node; i; i = i->next) {
      scg_function_record & callee = address_to_record (i->address);
//      fprintf (stderr, "\t%s\n", callee.name.c_str());
      callee. caller_counts[ caller] += counter;
      caller->callee_counts[&callee] += counter;

      ++occur_counts[&callee];
      caller = &callee;
   }
   caller->terminal_count += counter;

   // Now increase all the record_counts.
   for (record_counts::iterator i = occur_counts.begin();
        i != occur_counts.end(); ++i) {
      i->first->call_count += counter;
      if (i->first->call_count_breakdown.size() < i->second) {
         i->first->call_count_breakdown.resize (i->second);
      }
      i->first->call_count_breakdown[i->second - 1] += counter;
   }
}

void scg_database::build_from (scg_node_t * volatile * hash_table,
                               size_t                  hash_table_size)
{
   for (size_t i = 0; i != hash_table_size; ++i) {
      for (const scg_node_t * node = hash_table[i];
           node; node = node->hash_link) {
         unsigned long counter = node->counter;

         if (counter != 0) {
            process_node (*node, counter);
            total_samples += counter;
         }
      }
   }
}

void scg_database::output (FILE * out_file) const
{
   typedef std::multimap <int, const scg_function_record *> sorted_t;
   sorted_t sorted;

   for (record_map::const_iterator i = records.begin();
        i != records.end(); ++i) {
      sorted.insert (std::make_pair (i->second.call_count, &i->second));
   }

   fprintf (out_file, "Profile for %s with %lu samples.\n",
            program_invocation_short_name, total_samples);

   for (sorted_t::reverse_iterator i = sorted.rbegin();
        i != sorted.rend(); ++i) {
      i->second->output (out_file, total_samples);
   }
}

typedef std::multimap <int, scg_function_record *> sorted_counts;
static void scg_map_switcheroo (sorted_counts &       output,
                                const record_counts & input)
{
   for (record_counts::const_iterator i = input.begin();
        i != input.end(); ++i) {
      output.insert (std::make_pair (i->second, i->first));
   }
}

void scg_function_record::output (FILE *        out_file,
                                  unsigned long total_samples) const
{
   /* Output a banner. */
   fprintf (out_file, "-------------------------------------------------------------------------------\n");
   /* Output the callers, least common to most common. */
   sorted_counts sorted;
   scg_map_switcheroo (sorted, caller_counts);

   for (sorted_counts::iterator i = sorted.begin(); i != sorted.end(); ++i) {
      fprintf (out_file, "\t%i\t%s\n", i->first, i->second->name.c_str());
   }

   /* Output the function name with the call count(s). */
   if (call_count_breakdown.size() <= 1) {
      fprintf (out_file, "%s\t%i/%i (%.2f%%/%.2f%%)\n", name.c_str(),
               terminal_count, call_count,
               terminal_count * 1e2 / total_samples,
               call_count * 1e2 / total_samples);
   }
   else {
      fprintf (out_file, "%s\t%i/%i (",
               name.c_str(), terminal_count, call_count);
      for (int_vector::const_iterator i = call_count_breakdown.begin();
           i != call_count_breakdown.end(); ++i) {
         fprintf (out_file, " %i", *i);
      }
      fprintf (out_file, " ) (%.2f%%/%.2f%%)\n",
               terminal_count * 1e2 / total_samples,
               call_count * 1e2 / total_samples);
   }

   /* Output the callees, most common to least common. */
   sorted.clear();
   scg_map_switcheroo (sorted, callee_counts);

   for (sorted_counts::reverse_iterator i = sorted.rbegin();
        i != sorted.rend(); ++i) {
      fprintf (out_file, "\t%i\t%s\n", i->first, i->second->name.c_str());
   }
}

void scg_output_profile()
{
   scg_database database;

   reflect_symtab_create();
   database.build_from (scg_node_hash, SCG_NODE_HASH_SIZE);
   reflect_symtab_destroy();

   FILE * out_file = NULL;
   bool   close_it = true;

   const char * name = getenv ("SCG_OUTPUT");
   if (name != NULL && name[0] != 0) {
      const char * ppos = strchr (name, '%');
      if (ppos != NULL) {
         // Replace the '%' with the pid.
         size_t namelen = strlen (name);
         char name2 [namelen + 32];
         sprintf (name2, "%*s%i%s", ppos - name, name, getpid(), ppos + 1);
         out_file = fopen (name2, "w");
      }
      else {      
         out_file = fopen (name, "w");
      }
   }
   if (out_file == NULL) {
      out_file = stderr;
      close_it = false;
   }

   database.output (out_file);
   fflush (out_file);

   if (close_it) {
      fclose (out_file);
   }
}
