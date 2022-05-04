#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h> 
#include <stdbool.h>
#include <getopt.h>

#include "plugin_api.h"
#include "plug_helpers.h"

#define VERSION "1.0.0"

#define MDBG "DEBUG: Plugin: "
#define MERR "ERR: "
#define LOG(...) if(DEBUG)fprintf(stderr, MDBG __VA_ARGS__); //if DEBUG is defined
#define ERROUT(...) {fprintf(stderr, MERR __VA_ARGS__);exit(EXIT_FAILURE);} //print a (user-visible) error

char* DEBUG = NULL;
char* pluginDirectory = NULL;
struct pluginref* pluginData = NULL; // last "undefined" one will have its ->next == NULL
struct pluginref* confirmedPlugins = NULL; // plugins that were given any options and will therefore be used
bool walkerReadPluginErrord = false;
bool givenOptionAnd = false, givenOptionOr = false;
bool givenOptionNot = false;
bool givenOptionIgnorePluginErrors = false;


void* emalloc(size_t size)
{
    void* result = malloc(size);
    if (!result)
        ERROUT("Not enough memory. Download some RAM=)\n");
    return result;
}

int walker_plugin(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf){
    
    if (!strcmp(fpath, pluginDirectory)) // Ignore the starting (plugin) directory
        return FTW_CONTINUE;
    
    if (sb==NULL || ftwbuf==NULL){  
        ERROUT("The dir is up limits :_)\n");
        return FTW_STOP;}            
    
    if (typeflag == FTW_D)
        return FTW_SKIP_SUBTREE; 
    
    char* ending = strrchr(fpath, '.');
    if (!ending || strcmp(ending, ".so"))
        return FTW_CONTINUE;

    void *dl = dlopen(fpath, RTLD_LAZY);
    if (!dl) {
        LOG("Skipped: %s\n", fpath)
        return FTW_CONTINUE;
    }
    LOG("Found  %s\n", fpath);

    // Check for plugin_get_info() func
    void *func = dlsym(dl, "plugin_get_info");
    if (!func) {
        LOG("got: %s\n", fpath);
        dlclose(dl);
        return FTW_CONTINUE;
    }
    // and for plugin_process_file() too
    void *func2 = dlsym(dl, "plugin_process_file");
    if (!func2) {
        LOG("got: %s\n", fpath);
        dlclose(dl);
        return FTW_CONTINUE;
    }

    typedef int (*pgi_func_t)(struct plugin_info*);
    pgi_func_t pgi_func = (pgi_func_t)func;

    struct pluginref* pluginDataCur = pluginData;
    while(pluginDataCur->next)
        pluginDataCur = pluginDataCur->next;

    int ret = pgi_func(&(pluginDataCur->ppi));
    if (ret < 0) {        
        // chech if everything is fine with func
        fprintf(stderr, "plugin_get_info() err: %s\n", fpath);
        walkerReadPluginErrord = true;
        return FTW_STOP;
    }

    // Clear the FLAG and list for log purposes
    struct plugin_info pi = pluginDataCur->ppi;
    for(int i = 0; i<(int)pi.sup_opts_len; i++)
    {
        pi.sup_opts[i].opt.flag = NULL;
        LOG("Can do: --%s\n", pi.sup_opts[i].opt.name);
    }
    // Fill out the remainder of the fields
    pluginDataCur->pluginPath = strdup(fpath); 
    pluginDataCur->call_func = (plugincall_func_t)func2;
    pluginDataCur->next = emalloc(sizeof(struct pluginref));
    pluginDataCur->next->next = NULL;
    pluginDataCur->dl_handle = dl;

    // check the list
    struct plugin_info pi_ours = pluginDataCur->ppi;

    for (struct pluginref* ptr = pluginData; ptr != pluginDataCur; ptr = ptr->next)
    {
        struct plugin_info pi = ptr->ppi;
        for (unsigned int i = 0; i<pi.sup_opts_len; i++)
        {
            const char* optName = pi.sup_opts[i].opt.name;
            for (unsigned int j=0; j<pi_ours.sup_opts_len; j++)
            {
                const char* optNameOurs = pi_ours.sup_opts[j].opt.name;
                if (!strcmp(optName, optNameOurs))
                {
                    fprintf(stderr, "Duplicate options: --%s!\n\tPlugin 1: %s\n\tPlugin 2: %s\n", optName, fpath, ptr->pluginPath);
                    walkerReadPluginErrord = true;
                    return FTW_STOP;
                }
            }
        }  
    }

    LOG("Acceptable options\n");

    return FTW_CONTINUE;
}

int walker_readFiles(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    if (typeflag & (FTW_D|FTW_DNR)) //dublicate from walker_plugin
        return FTW_CONTINUE; 

    if (sb==NULL || ftwbuf==NULL)   
        ERROUT("The dir is up limits :_)\n");
    
    bool fileValid = (givenOptionAnd ? true : false);
    for (struct pluginref* ptr = confirmedPlugins; ptr != NULL; ptr = ptr->nextConfirmed) {
        LOG("validX %s\n", ptr->pluginPath);
        int result = ptr->call_func(fpath, ptr->pluginBareOptions, ptr->ppi.sup_opts_len);

        if (result < 0) 
            ERROUT("plugin took collision, errorcode %i: %s\n", result, ptr->pluginPath);
        if (result == 0) // SUCCESS, FILE MATCHES
            if (givenOptionOr) {
                fileValid = true;
                break;
            }
        if (result > 0) // FILE DOES NOT MATCH
            if (givenOptionAnd) {
                fileValid = false;
                break;
            }
    }
    if (givenOptionNot)
        fileValid = !fileValid;
    
    if (fileValid)
        printf("%s\n", fpath);

    LOG("[%s] If found: %s\n", fileValid ? "SUCCESS" : "FILE DOES NOT MATCH", fpath);
    return FTW_CONTINUE;
}

int main(int argc, char* argv[]) {
    char cwd[PATH_MAX]; //mallocless

    DEBUG = getenv("PLUGINDEBUG");

    char* plugin_location = NULL;
    int result = 1;
    char pluginDirectoryPathBuffer[PATH_MAX];

    // Initialize, parse commands
    bool givenOptionVersion = false, givenOptionHelp = false;

    char* shortArgs = emalloc(argc);
    int shortArgPtr = -1;
    struct option* longArgs = emalloc(sizeof(struct option) * argc);
    int longArgPtr = -1;

    bool justDefinedLongArg = false;
    int givenP = 0; //0 = not given, 1 = just given, 2 = already given and captured

    // COMMAND PARSE
    {
        if (argc == 1) {
            LOG("Usage: PluginLauncher /path/to/PluginLib.so [options_for_lib] /path/to/file\n");
            givenOptionVersion = true;
            givenOptionHelp = true;
            plugin_location = ".";
        }
        else {
            for (int i = 1; i < argc - 1; i++)
            {
                char* arg = argv[i];
                if (givenP == 1) {
                    givenP = 2;
                    pluginDirectory = arg;
                }
                else if (strlen(arg) == 2 && arg[0] == '-' && arg[1] != '-') {
                    // Short option
                    shortArgPtr++;
                    shortArgs[shortArgPtr] = arg[1];
                    justDefinedLongArg = false;
                    if (shortArgs[shortArgPtr] == 'P')
                    {
                        if (givenP != 0)
                        {
                            result = EXIT_FAILURE;
                            printf("mess with argument -P. Restart the program\n");
                            goto END;
                        }
                        givenP = 1;
                    }
                }
                else if (strlen(arg) > 2 && arg[0] == '-' && arg[1] == '-') {
                    // Long option
                    longArgPtr++;
                    longArgs[longArgPtr].name = (char*)(arg + 2); // cuts first -- off
                    longArgs[longArgPtr].has_arg = false;
                    longArgs[longArgPtr].val = 0;
                    longArgs[longArgPtr].flag = NULL;
                    justDefinedLongArg = true;
                }
                else {
                    // Something else
                    if (justDefinedLongArg) {
                        longArgs[longArgPtr].has_arg = true;
                        // cruthch. To be workable
                        longArgs[longArgPtr].flag = (int*)arg;
                        justDefinedLongArg = false;
                    }
                    else {
                        ERROUT("Cannot identify the command line option: %s\n", arg);
                    }
                }

            }

            // Get and validate the target directory
            plugin_location = argv[argc - 1];
            struct stat st;
            if ((lstat(plugin_location, &st)) < 0)
                ERROUT("Unknown error during lstat: %s\n", plugin_location);
            if ((st.st_mode & S_IFMT) == S_IFLNK) // Check for a symbolic link
                LOG("File a symbolic link!\n");
            if (lstat(plugin_location, &st) != 0)
                ERROUT("There's no such dir: %s\n", plugin_location);
            if (!S_ISDIR(st.st_mode))
                ERROUT("Invalid dir: %s\n", plugin_location);
            LOG("Catched dir: %s\n", plugin_location);
            if (DEBUG)
                LOG("there're %i short options and %i long options.\n", shortArgPtr + 1, longArgPtr + 1);
        }
    }
    pluginData = emalloc(sizeof(struct pluginref));
    pluginData->next = NULL;

    // Try all names that are passed via argv
    if (givenP == 0)
    {
        char* cwdResult = getcwd(cwd, sizeof(cwd));//if we want return absolute path
        if (!cwdResult)
            ERROUT("trying to get the local directory..fail");
        pluginDirectory = cwd;
    }
    else {
        // Validating plugin directory
        char* pluginDirPtr = realpath(pluginDirectory, pluginDirectoryPathBuffer);
        if (!pluginDirPtr)
            ERROUT("Invalid plugin directory: %s\n", pluginDirectory);
        pluginDirectory = pluginDirPtr;

        struct stat st;
        if (stat(pluginDirectory, &st) != 0)
            ERROUT("Cannot find plugin directory: %s\n", pluginDirectory;
        if (!S_ISDIR(st.st_mode))
            ERROUT("The given plugin directory isn't a valid directory: %s\n", pluginDirectory);
        LOG("Accepting the plugin directory: %s\n", pluginDirectory);
    }

    LOG("Reading directory... %s\n", pluginDirectory);

    nftw(pluginDirectory, walker_plugin, 10, FTW_ACTIONRETVAL | FTW_PHYS);
    if (walkerReadPluginErrord) // All messages already handled by it, now clean up
        goto END;

    for (int i = 0; i <= shortArgPtr; i++)
    {
        if (shortArgs[i] == 'v')
            givenOptionVersion = true;
        if (shortArgs[i] == 'h')
            givenOptionHelp = true;
    }

    if (givenOptionVersion)
    {
        printf("actual version:" VERSION "\n");
    }
    if (givenOptionHelp)
    {
    printf(
                "This multipurpose program loads any compatible predicate plugins, recursively"
                "looks for matching files and outputs their full filenames.\n"
                "\n"
                "Usage: "__FILE__" [[options] plugin_location]\n" // run it and hope its bug free =)
                "\n"
                "Supported basic options are:\n"
                "-P dir     Plugin directory; defaults to current directory.\n"
                "-A         AND: A file must match all of the provided plugin predicates.\n"
                "-O         OR: A file must match any of the provided plugin predicates.\n"
                "-N         NOT: File will be output only if it *didn't* match the predicates.\n"
                "-v         Output the program's version and, if -h is not given, exit.\n"
                "-h         Output this help and exit.\n"
        );
    }
    if (givenOptionVersion && !givenOptionHelp) {
        return 0; // just leave
    }
    if (givenOptionHelp) {
        int loadedPlugins = 0;
        for (struct pluginref* ptr = pluginData; ptr->next != NULL; ptr = ptr->next)
            loadedPlugins++;
        if (loadedPlugins == 0) {
            printf("none supported options (!?)\n");// no plugins found
        }
        else {
            printf("\n supported (long) options %i: \n", loadedPlugins);
            for (struct pluginref* pr = pluginData; pr->next != NULL; pr = pr->next)
            {
                struct plugin_info pi = pr->ppi;
                printf("From plugin: %s\n", pr->pluginPath);
                for (size_t i = 0; i < pi.sup_opts_len; i++) {
                     printf("\t--%-17s %-60s\n", pi.sup_opts[i].opt.name, pi.sup_opts[i].opt_descr);
                }
            }
        }
        result = 0;
        goto END;
    }

        {
            if (longArgPtr == -1)
                ERROUT("No long options given; cannot choose any plugins! \n"); // there's no sence to make this but I wrote a check so that exists
            for (int i = 0; i <= shortArgPtr; i++)
            {
                if (shortArgs[i] == 'A') { //'and'
                    if (givenOptionAnd) ERROUT("mess with -A");
                    givenOptionAnd = true;
                }
                else if (shortArgs[i] == 'O') { //'or'
                    if (givenOptionOr) ERROUT("mess with -O");
                    givenOptionOr = true;
                }
                else if (shortArgs[i] == 'N') { //'not'
                    if (givenOptionNot) ERROUT("mess with -N");
                    givenOptionNot = true;
                }
            }
            if (givenOptionAnd && givenOptionOr) ERROUT("-A and -O are unacceptable at the same time");
            if (!(givenOptionAnd || givenOptionOr))
                givenOptionAnd = true; //set 'and' as default 
        }

        {
            for (struct pluginref* ptr = pluginData; ptr->next != NULL; ptr = ptr->next) {
                bool pluginUsed = false;
                for (int i = 0; i < ((int)ptr->ppi.sup_opts_len); i++) {
                    struct option ppiOpt = ptr->ppi.sup_opts[i].opt;

                    for (int j = 0; j <= longArgPtr; j++) {
                        const char* optName = longArgs[j].name;

                        if (!strcmp(ppiOpt.name, optName)) {
                            pluginUsed = true; // at least one arg is used
                            if (longArgs[i].has_arg && ppiOpt.has_arg == no_argument)
                                ERROUT("Mess with arguments --%s: extra value\n", optName);
                            if (!longArgs[i].has_arg && ppiOpt.has_arg == required_argument)
                                ERROUT("Mess with arguments --%s: missing value\n", optName);
                            ptr->ppi.sup_opts[i].opt.flag = longArgs[j].flag;
                            longArgs[i].val = 1;
                        }
                    }
                }

                if (pluginUsed) {
                    LOG("Entered plugin: %ss\n", ptr->pluginPath);
                    if (confirmedPlugins == NULL) {
                        ptr->nextConfirmed = NULL;
                        confirmedPlugins = ptr;
                    }
                    else {
                        ptr->nextConfirmed = confirmedPlugins;
                        confirmedPlugins = ptr;
                    }
                    ptr->pluginBareOptions = malloc(sizeof(struct plugin_option) * ptr->ppi.sup_opts_len);
                    for (unsigned int i = 0; i < ptr->ppi.sup_opts_len; i++)
                        ptr->pluginBareOptions[i] = ptr->ppi.sup_opts[i].opt;
                }
                else {
                    LOG("No arg left: %s\n", ptr->pluginPath);
                }
            }

            bool leftoverLongArgs = false;
            for (int i = 0; i <= longArgPtr; i++){
                if (longArgs[i].val == 0) {
                    fprintf(stderr, "Unused plugin option: --%s\n", longArgs[i].name);
                    leftoverLongArgs = true;
                }
            }
            if (leftoverLongArgs) {
                fprintf(stderr, "Please re-check your plugin options and the presence of all the required plugins.\n");
                goto END;
            }
        }

        {
            result = nftw(pluginDirectory, walker_readFiles, 20, FTW_ACTIONRETVAL | FTW_PHYS);
            if (result < 0)
                fprintf(stderr, MERR "nftw err");
        }

    END:
        LOG("FIN\n");
        if (shortArgs)
            free(shortArgs);
        if (longArgs)
            free(longArgs);
        while (pluginData)
        {
            if (pluginData->pluginPath)
                free(pluginData->pluginPath);
            if (pluginData->pluginBareOptions)
                free(pluginData->pluginBareOptions);
            if (pluginData->dl_handle)
                dlclose(pluginData->dl_handle);

            struct pluginref* pr = pluginData->next;
            free(pluginData);
            pluginData = pr;
        }
        return result;
}