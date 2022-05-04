#ifndef _PLUGIN_API_H
#define _PLUGIN_API_H

#include <getopt.h>

/*
    A structure, describing the option supported by the plugin.
*/
struct plugin_option {
    /* Option in the format supported by getopt_long */
    struct option opt;
    /* Description of the option provided by the plugin. */
    const char *opt_descr;
};

/*
    Structure containing information about the plugin.
*/
struct plugin_info {
    /* Purpose of the plugin */
    const char *plugin_purpose;
    /* Author */
    const char *plugin_author;
    /* Length of the options list */
    size_t sup_opts_len;
    /* List of options supported by the plugin */
    struct plugin_option *sup_opts;
};


int plugin_get_info(struct plugin_info* ppi);
/*
    
    A function that allows you to get information about the plugin.
    
    Arguments:
        ppi - address of the structure that the plugin fills in with its information.
        
    Return value:
          0 - if success
          -1 - if faill
*/



int plugin_process_file(const char *fname,
        struct option in_opts[],
        size_t in_opts_len);

/* 
    A function that allows you to find out if the file suitable.
    
    Arguments:
        fname - file path, which is checked using array in_opts.

        in_opts - list of options (gives to plugin).
            struct option {
               const char *name;
               int         has_arg;
               int        *flag;
               int         val;
            };
            flag - for the value. I dont use val.
           
        in_opts_len - length of the options list.
                    
    Возвращаемое значение:
          0 - if success,
        > 0 - if the file does not suitable,
        < 0 - error, also sets the errno
*/
        
#endif
