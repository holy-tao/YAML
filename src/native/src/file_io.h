#ifndef AHKYAML_FILE_IO_H
#define AHKYAML_FILE_IO_H

#include <MCL.h>
#include <stdio.h>
#include <stdint.h>

#include "ahk_error.h"

#ifndef NULL
#define NULL 0
#endif

#define _O_RDONLY 0x0000
#define _O_WRONLY 0x0001
#define _O_BINARY 0x8000

#define DUPLICATE_SAME_ACCESS 0x00000002

MCL_IMPORT(FILE*, msvcrt,   _wfopen,           (const wchar_t*, const wchar_t*));
MCL_IMPORT(int,   msvcrt,   _open_osfhandle,   (intptr_t, int));
MCL_IMPORT(FILE*, msvcrt,   _fdopen,           (int, const char*));
MCL_IMPORT(int,   msvcrt,   _close,            (int));
MCL_IMPORT(int,   msvcrt,   fflush,            (FILE*));
MCL_IMPORT(int,   Kernel32, DuplicateHandle,
           (void*, void*, void*, void**, unsigned long, int, unsigned long));
MCL_IMPORT(void*, Kernel32, GetCurrentProcess, ());
MCL_IMPORT(int,   Kernel32, CloseHandle,       (void*));

/* Open a FILE* from a UTF-16 path. Caller must fclose on success. Sets
 * err globals and returns NULL on failure. */
static FILE* file_from_path(const wchar_t* path, const wchar_t* mode)
{
    FILE* fp = _wfopen(path, mode);
    if (!fp) {
        set_err("Could not open file", NULL, 0, 0);
        return NULL;
    }
    return fp;
}

/**
 * Open a FILE* from a Win32 HANDLE.
 *
 * The caller's HANDLE remains owned by the caller. We DuplicateHandle to
 * obtain an independent handle; that duplicate becomes owned by the fd that
 * _open_osfhandle returns; that fd becomes owned by the FILE* that _fdopen
 * returns. A subsequent fclose on the returned FILE* closes the fd, which
 * closes our duplicate handle, leaving the caller's original untouched.
 *
 * flags: _O_RDONLY|_O_BINARY (read) or _O_WRONLY|_O_BINARY (write).
 * mode: "rb" or "wb" matching flags.
 */
static FILE* file_from_handle(intptr_t hWin32, int flags, const char* mode)
{
    if (!hWin32 || hWin32 == (intptr_t)-1) {
        set_err("Invalid file handle", NULL, 0, 0);
        return NULL;
    }
    void* dup = NULL;
    void* proc = GetCurrentProcess();
    if (!DuplicateHandle(proc, (void*)hWin32, proc, &dup,
                         0, 0, DUPLICATE_SAME_ACCESS)) {
        set_err("DuplicateHandle failed", NULL, 0, 0);
        return NULL;
    }
    int fd = _open_osfhandle((intptr_t)dup, flags);
    if (fd == -1) {
        CloseHandle(dup);
        set_err("_open_osfhandle failed", NULL, 0, 0);
        return NULL;
    }
    FILE* fp = _fdopen(fd, mode);
    if (!fp) {
        _close(fd); /* also closes the duplicate */
        set_err("_fdopen failed", NULL, 0, 0);
        return NULL;
    }
    return fp;
}

#endif /* AHKYAML_FILE_IO_H */
