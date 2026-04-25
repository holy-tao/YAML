#ifndef AHKYAML_ERROR_H
#define AHKYAML_ERROR_H

#define UNICODE

#include <MCL.h>
#include <string.h>

#ifndef NULL
#define NULL 0
#endif

/**
 * Shared utils for passing error information back to AHK.
 */

// TODO store messages as BSTRs and let AHK free them
static int  g_err_line;    MCL_EXPORT_GLOBAL(g_err_line, Int);
static int  g_err_column;  MCL_EXPORT_GLOBAL(g_err_column, Int);
static char g_err_message[256];
static char g_err_extra[256];

MCL_EXPORT(get_err_message, CDecl_Ptr);
const char *get_err_message(void) { return g_err_message; }

MCL_EXPORT(get_err_extra, CDecl_Ptr);
const char *get_err_extra(void) { return g_err_extra; }

/**
 * Set the error message, retrieved by AHK.
 */
MCL_EXPORT(set_err, Ptr, msg, Ptr, extra, Int, line, Int, col, CDecl_Int);
int set_err(const char *msg, const char* extra, int line, int col)
{
    g_err_line = line;
    g_err_column = col;

    strncpy(g_err_message, msg, (int)sizeof(g_err_message) - 1);
    g_err_message[sizeof(g_err_message) - 1] = 0;

    // extra is optional, clear it if null
    if (extra != NULL) {
        strncpy(g_err_extra, extra, (int)sizeof(g_err_extra) - 1);
        g_err_extra[sizeof(g_err_extra) - 1] = 0;
    }
    else {
        memset(g_err_extra, 0, sizeof(g_err_extra));
    }

    return 0;
}

/**
 * Clear any errors stored in the library.
 */
static void clear_err()
{
    memset(g_err_message, 0, sizeof(g_err_message));
    memset(g_err_extra, 0, sizeof(g_err_extra));
    g_err_line = 0;
    g_err_column = 0;
}

#endif /* AHKYAML_ERROR_H */