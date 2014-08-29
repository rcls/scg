
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
#include <string.h>
#include <vector>

struct scg_function_record;

typedef std::map <scg_function_record *, size_t> record_counts;

struct scg_function_record {
    scg_function_record() :
        address (0),
        call_count (0),
        terminal_count (0)
        { }

    scg_function_record (const std::string & n, uintptr_t a) :
        name (n),
        address (a),
        call_count (0)
        { }

    std::string name;
    uintptr_t  address;

    record_counts caller_counts;
    record_counts callee_counts;

    // The number of times that we have occured at least once on the stack.
    int            call_count;
    // The number of times that we have occured as the innermost element on the
    // stack.
    int            terminal_count;
    // Call_counts[i] is number of samples with the function occuring i+1
    // times in the stack.
    std::vector <int> call_count_breakdown;

    // Print to out_file.
    void output (FILE *        out_file,
                 unsigned long total_samples) const;
};

struct scg_database {
    scg_database() :
        spontaneous ("<spontaneous>", 0),
        total_samples (0)
        { }

    // Function records indexed by base address.
    std::map <uintptr_t, scg_function_record> records;

    // Function records indexed by return address.
    std::map <uintptr_t, scg_function_record *> canonicalisers;

    // Convert a return address to a record.
    scg_function_record & address_to_record (uintptr_t address);

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

scg_function_record & scg_database::address_to_record (uintptr_t address)
{
    auto i = canonicalisers.find (address);
    if (i != canonicalisers.end())
        return *i->second;

    const char * object;
    const char * name;
    size_t       offset;
    char         fake_name[20];

    reflect_symtab_lookup (&object, &name, &offset, (const void *) address);

    if (name == NULL) {
        // The address was not found, so we fake it.
        sprintf (fake_name, "%#tx", address);
        name = fake_name;
        offset = 0;
    }

    address = address - offset;         // Base address.

    scg_function_record & result = records[address];

    if (result.address == 0) {
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
    for (auto & i : occur_counts) {
        i.first->call_count += counter;
        if (i.first->call_count_breakdown.size() < i.second)
            i.first->call_count_breakdown.resize (i.second);

        i.first->call_count_breakdown[i.second - 1] += counter;
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
    std::multimap <int, const scg_function_record *, std::greater<int> >
        sorted;

    for (auto & i : records)
        sorted.insert (std::make_pair (i.second.call_count, &i.second));

    fprintf (out_file, "Profile for %s with %lu samples.\n",
             program_invocation_short_name, total_samples);

    for (const auto & i : sorted)
        i.second->output (out_file, total_samples);
}

typedef std::multimap <int, scg_function_record *> sorted_counts;
static void scg_map_switcheroo (sorted_counts &       output,
                                const record_counts & input)
{
    for (const auto & i : input)
        output.insert (std::make_pair (i.second, i.first));
}

void scg_function_record::output (FILE *        out_file,
                                  unsigned long total_samples) const
{
    /* Output a banner. */
    fprintf (out_file, "-------------------------------------------------------------------------------\n");
    /* Output the callers, least common to most common. */
    sorted_counts sorted;
    scg_map_switcheroo (sorted, caller_counts);

    for (const auto & i : sorted)
        fprintf (out_file, "\t%i\t%s\n", i.first, i.second->name.c_str());

    /* Output the function name with the call count(s). */
    if (call_count_breakdown.size() <= 1) {
        fprintf (out_file, "+%s\t%i/%i (%.2f%%/%.2f%%)\n", name.c_str(),
                 terminal_count, call_count,
                 terminal_count * 1e2 / total_samples,
                 call_count * 1e2 / total_samples);
    }
    else {
        fprintf (out_file, "+%s\t%i/%i (",
                 name.c_str(), terminal_count, call_count);
        for (auto count : call_count_breakdown)
            fprintf (out_file, " %i", count);

        fprintf (out_file, " ) (%.2f%%/%.2f%%)\n",
                 terminal_count * 1e2 / total_samples,
                 call_count * 1e2 / total_samples);
    }

    /* Output the callees, most common to least common. */
    sorted.clear();
    scg_map_switcheroo (sorted, callee_counts);

    for (auto i = sorted.rbegin(); i != sorted.rend(); ++i)
        fprintf (out_file, "\t%i\t%s\n", i->first, i->second->name.c_str());
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
            sprintf (name2, "%*s%i%s",
                     (int) (ppos - name), name, getpid(), ppos + 1);
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
