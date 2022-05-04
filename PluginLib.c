#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include "plugin_api.h"
#include "plug_helpers.h"

static char* DEBUG = NULL;

static char *g_plugin_purpose = "Search for files, that contains all specified bytes";

static char *g_plugin_author = "DeerHolt";

static char const* g_plugin_purpose = "Search for files containing all the given bytes, regardless of order.";
static char const* g_plugin_author = "Torchilo Dmitry";
#define OPT_ARG1_STR "--bytes"
#define OPT_ARG1_DESC "Bytes to find, separated by commas without spaces (e.g. 24,0b00010100,0xA)"
#define OPT_ARG1_REQ required_argument

#define OPT_BYTES_STR "list-bit"
#define MDBG "DEBUG: PluginLib: "
#define MERR "ERR: "
#define LOG(...) if(DEBUG)fprintf(stderr, MDBG __VA_ARGS__); //log za zhing if DEBUG is defined
#define ERRSET(...) {errno=EINVAL;fprintf(stderr, MERR __VA_ARGS__);}

static struct plugin_option g_po_arr[] = {
    {
        {
            OPT_ARG1_STR,
            OPT_ARG1_REQ,
            0, 0,
        },
        OPT_ARG1_DESC
    }
};

static int g_po_arr_len = sizeof(g_po_arr)/sizeof(g_po_arr[0]);

//
//  Private functions
//
unsigned char* listbytes(unsigned char*);

//
//  API functions
//
int plugin_get_info(struct plugin_info* ppi) {
    if (!ppi) {
        fprintf(stderr, "ERROR: invalid argument\n");
        return -1;
    }
    
    ppi->plugin_purpose = g_plugin_purpose;
    ppi->plugin_author = g_plugin_author;
    ppi->sup_opts_len = g_po_arr_len;
    ppi->sup_opts = g_po_arr;
    
    return 0;
}

int plugin_process_file(const char *fname, struct option in_opts[], size_t in_opts_len) {

    unsigned char* file_mapptr = NULL;
    int fileCheckResult = -1;

    DEBUG = getenv("PLUGINDEBUG");
    
    if (!fname || !in_opts || !in_opts_len) {
        ERRSET("not enough data for processing\n");
        LOG("problem in: %s, %s and %s.\n",
            (!fname ? "fname" : "!fname"),
            (!in_opts ? "in_opts" : "!in_opts"),
            (!in_opts_len ? "in_opts_len" : "!in_opts_len"));
        return -1;
    }

    if (DEBUG) {
        for (size_t i = 0; i < in_opts_len; i++) {
            LOG("Got option '%s' with arg '%s'\n", in_opts[i].name, (char*)in_opts[i].flag);
        }
    }

    LOG("Opening file: %s\n", fname);

    int saved_errno = 0;

    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        // errno is set by open()
        ERRSET("Could not read the file!\n");
        return -1;
    }

    struct stat st = { 0 };
    int res = fstat(fd, &st);
    if (res < 0) {
        // errno is set already, but still
        ERRSET("Unknown error during fstat: %s\n", fname);
        goto END;
    }

    unsigned int file_size = st.st_size;
    file_mapptr = mmap(0, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_mapptr == MAP_FAILED) {
        // errno is set already
        ERRSET("Unknown error during mmap: %s\n", fname);
        goto END;
    }

    // Get all options
    char const* options;
    for (int i = 0; i < g_po_arr_len; i++) {
        const char* option = retrievePluginOption(in_opts, in_opts_len, (g_po_arr[i].opt.name));
        if (!option) {
            options = NULL;
            // ERRSET("Cannot find the required option --'%s'!\n", option); 
            // goto END;
        }
        else {
            options = option;
        }

    }


    
    if (strlen(options) == 0) {
        fileCheckResult = 0; 
        goto END;
    }
    int allBytes[256] = { 0 };
    for (unsigned int i = 0; i < file_size; i++)
        allBytes[file_mapptr[i]]++;

    fileCheckResult = 0; 
    char* token = strtok((char*)options, ",");
    while (token)
    {
        errno = 0;
        int byteParsed = parseStrToDecBinHex(token, NULL);
        if (errno) {
            ERRSET("Cannot parse the bytestring!\n");
            goto END;
        }
        if (byteParsed < 0 || byteParsed > 255) {
            ERRSET("Cannot parse the bytestring: number too large for a byte!\n");
            goto END;
        }
        if (allBytes[byteParsed] == 0) {
            fileCheckResult = 1; 
            break;
        }
        allBytes[byteParsed]--;
        token = strtok(NULL, ",");
    }
    //close
END:
    // ignore new errnos...
    saved_errno = errno;
    if (file_mapptr != MAP_FAILED && file_mapptr != NULL) munmap(file_mapptr, st.st_size);
    close(fd);
    errno = saved_errno;
    // ..untill here

    if (errno) {
        LOG("Error while reading the file!\n");
        return -1;
    }

    return fileCheckResult;
}