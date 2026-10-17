/* files.h on POSIX. */
#include "files.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Creates the folders above `path` that are missing. */
static void make_folders(const char *path)
{
    char *copy = strdup(path);
    if (copy == NULL) {
        return;
    }
    for (char *c = copy + 1; *c != '\0'; ++c) {
        if (*c == '/' || *c == '\\') {
            const char keep = *c;
            *c = '\0';
            mkdir(copy, 0777);
            *c = keep;
        }
    }
    free(copy);
}

int khdays_host_write_file_atomic(const char *path, const void *data, size_t size)
{
    /* the temporary file is beside it, named after this process, so two copies never share one */
    const size_t length = strlen(path) + 32;
    char *temp = (char *)malloc(length);
    const unsigned char *at = (const unsigned char *)data;
    size_t left = size;
    int file, ok;
    if (temp == NULL) {
        return 0;
    }
    snprintf(temp, length, "%s.%ld.tmp", path, (long)getpid());
    make_folders(path);
    file = open(temp, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (file < 0) {
        fprintf(stderr, "host: cannot write beside %s (%s)\n", path, strerror(errno));
        free(temp);
        return 0;
    }
    while (left > 0) {
        const ssize_t done = write(file, at, left > 0x40000000u ? 0x40000000u : left);
        if (done <= 0) {
            if (done < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        at += done;
        left -= (size_t)done;
    }
    ok = left == 0 && fsync(file) == 0;
    close(file);
    if (ok && rename(temp, path) != 0) {
        ok = 0;
    }
    if (!ok) {
        fprintf(stderr, "host: cannot write %s (%s); it stays as it was\n", path, strerror(errno));
        unlink(temp);
    }
    free(temp);
    return ok;
}

long khdays_host_read_file(const char *path, void *data, size_t room)
{
    unsigned char *at = (unsigned char *)data;
    long total = 0;
    const int file = open(path, O_RDONLY);
    if (file < 0) {
        return -1;
    }
    while ((size_t)total < room) {
        const size_t left = room - (size_t)total;
        const ssize_t done = read(file, at + total, left > 0x40000000u ? 0x40000000u : left);
        if (done <= 0) {
            if (done < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        total += (long)done;
    }
    close(file);
    return total;
}

void khdays_host_local_time(int *hour, int *minute, int *second)
{
    SDL_Time now = 0;
    SDL_DateTime t;
    SDL_zero(t);
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &t, true)) {
        *hour = t.hour;
        *minute = t.minute;
        *second = t.second;
    } else {
        *hour = *minute = *second = 0;
    }
}
