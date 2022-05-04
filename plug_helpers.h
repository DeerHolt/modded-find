#ifndef SEEN_PLUGHELPERS_H
#define SEEN_PLUGHELPERS_H

#include <stdio.h>

#include "plugin_api.h"

typedef int (*plugincall_func_t)(const char*, struct option[], size_t);

struct pluginref {
    struct pluginref* next;
    struct pluginref* nextConfirmed; 
    struct plugin_info ppi;
    struct option* pluginBareOptions; // Copied over from PPI to make processing quicker
    plugincall_func_t call_func;
    char* pluginPath; // for the collision error messages
    void* dl_handle;
};

int parseStrToDecBinHex(const char* str, char** endptr_ret);
char* retrievePluginOption(struct option in_opts[], size_t in_opts_len, const char* optionName);


#endif