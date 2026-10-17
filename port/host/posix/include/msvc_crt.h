/* MSVC's C library names the port's own code uses, for other compilers: force-included into the port's
 * C and C++ off Windows (port/CMakeLists.txt). Only the forms the port calls them with: sscanf_s with
 * numeric conversions alone, strncpy_s with _TRUNCATE. */
#ifndef KHDAYS_POSIX_MSVC_CRT_H
#define KHDAYS_POSIX_MSVC_CRT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _strdup strdup
#define _atoi64 atoll
#define _snprintf snprintf
#define sscanf_s sscanf
#define strtok_s strtok_r
#define _TRUNCATE ((size_t)-1)

static inline int khdays_fopen_s(FILE **file, const char *path, const char *mode)
{
    *file = fopen(path, mode);
    return *file != NULL ? 0 : 1;
}
#define fopen_s khdays_fopen_s

static inline int khdays_strncpy_s(char *destination, size_t size, const char *source, size_t count)
{
    size_t n = strlen(source);
    if (size == 0) {
        return 1;
    }
    if (count != _TRUNCATE && count < n) {
        n = count;
    }
    if (n >= size) {
        n = size - 1;
    }
    memcpy(destination, source, n);
    destination[n] = '\0';
    return 0;
}
#define strncpy_s khdays_strncpy_s

#endif
