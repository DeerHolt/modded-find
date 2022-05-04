#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h> 
#include <unistd.h> 

#include "plug_helpers.h"


int parseStrToDecBinHex(const char* str, char** endptr_ret)
{
    const char* nptr = str;
    char* endptr = NULL;
    long number;

    errno = 0;

    //endptr_ret can be NULL. This is valid behaviour and is handled by strtol.

    if(str==NULL){
        errno = EINVAL;
        return -1;
    }
    if (strlen(str)>=2 && (str[0]=='0' && str[1]=='b')) {
        number = strtol(nptr+2, &endptr, 2); //negatives aren't supported
    } 
    else {
        number = strtol(nptr, &endptr, 0); //to dec,hex and octal
    }

    if (nptr == endptr)
        errno = EINVAL; //invalid (no digits found)
    else if (errno == 0 && nptr && !*endptr)
        {} //valid  (and represents all characters read) 
           //may additional characters remain
    else if (number < INT_MIN || number > INT_MAX)
        errno = EINVAL; // OOB
    else
        errno = EINVAL; 

    if (endptr_ret)
        *endptr_ret = endptr;
    
    return (int)number;
}

char* retrievePluginOption(struct option in_opts[], size_t in_opts_len, const char* optionName)
{
    // Returns:
    //   <option value>, if the option was successfully located and has a value,
    //   "" (empty string), if the option was successfully located and doesn't have a value,
    //   NULL, if the option wasn't found
    // Only checks for the first occurence of the option and doesn't do any checks on whether it happens more than once;

    if (in_opts == NULL || optionName == NULL){
        errno = EINVAL;
        return NULL;
    }
    for(unsigned int i=0; i<in_opts_len;i++)
    {
        if (!strcmp(in_opts[i].name, optionName))
            return in_opts[i].has_arg ? (char*)in_opts[i].flag : "";
    }
    return NULL;
}


