/* The session log (session_log.h). */
#include "session_log.h"
#include "../../host/app/instance.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

static FILE *file;
static SRWLOCK lock = SRWLOCK_INIT;
static LARGE_INTEGER started, frequency;
static volatile LONG frame;
static u32 flushed_frame;
static u32 marks;
static char log_path[160]; /* the open log's */
static volatile LONG wanted;   /* the settings ask for it */
static int opened_for_setting; /* this log was opened because they did */

/* The port's own messages (stderr) when nothing takes them -- the game opened from its shortcut has no
 * console, and a stop of the port's (an update returning no function, a register it does not model, the
 * sound driver's) says why only there, the session log just ending -- go to stderr-<the log's time>.log
 * beside it (buffered: exit(), which those stops call, writes the buffer out). Run from a console or with
 * stderr redirected, they go where they went. 1 when they were sent to `path`. */
static int keep_stderr(const char *path)
{
#if !defined(_WIN32)
    (void)path; /* stderr has its place there (Android: the log, host/posix/android_log.c) */
    return 0;
#else
    static int done;
    const HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (done) {
        return 0;
    }
    done = 1;
    if (h != NULL && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN) {
        return 0;
    }
    if (freopen(path, "w", stderr) == NULL) {
        return 0;
    }
    setvbuf(stderr, NULL, _IOFBF, 16384);
    return 1;
#endif
}

static void open_now(void)
{
    char path[160], err_path[160];
    SYSTEMTIME now;
    /* saves/logs -- a further player's copy on the PC: saves/player<N>/logs */
    const char *folder = khdays_host_user_folder();
    CreateDirectoryA("saves", NULL);
    CreateDirectoryA(folder, NULL);
    snprintf(path, sizeof(path), "%s/logs", folder);
    CreateDirectoryA(path, NULL);
    GetLocalTime(&now);
    snprintf(path, sizeof(path), "%s/logs/session-%04u%02u%02u-%02u%02u%02u.log", folder, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);
    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "log: cannot write %s\n", path);
        return;
    }
    snprintf(log_path, sizeof(log_path), "%s", path);
#if defined(__ANDROID__)
    /* each line written at once: a start that ends without a signal the crash handler sees (the system
     * closing the app) keeps every line up to it */
    setvbuf(file, NULL, _IOLBF, 0);
#endif
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    snprintf(err_path, sizeof(err_path), "%s/logs/stderr-%04u%02u%02u-%02u%02u%02u.log", folder, now.wYear, now.wMonth,
             now.wDay, now.wHour, now.wMinute, now.wSecond);
    const int kept = keep_stderr(err_path);
    fprintf(stderr, "log: this session's log is %s (F12 marks it)\n", path);
    fprintf(file, "khdays-native session log, %04u-%02u-%02u %02u:%02u:%02u\n", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond);
    {
        MEMORYSTATUSEX memory;
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory)) {
            fprintf(file, "memory: the PC has %llu MB, %llu MB of them free\n",
                    (unsigned long long)(memory.ullTotalPhys >> 20), (unsigned long long)(memory.ullAvailPhys >> 20));
        }
    }
    if (kept) {
        fprintf(file, "log: the port's messages (stderr, its stops' reasons among them) go to %s\n", err_path);
    }
    fflush(file);
}

void khdays_log_open(void)
{
    static int registered;
    const char *setting = getenv("KHDAYS_LOG");
    int on = InterlockedCompareExchange(&wanted, 0, 0) != 0;
#if defined(_DEBUG)
    on = on || setting == NULL || strcmp(setting, "0") != 0;
#else
    on = on || (setting != NULL && strcmp(setting, "1") == 0);
#endif
#if defined(__ANDROID__)
    /* always on Android: a game that stops as it starts leaves a log the setup screen copies to Download
     * (SessionLogs.kt), for the player to send; the settings have no switch for it there */
    on = 1;
#endif
    if (!registered) {
        registered = 1;
        atexit(khdays_log_close);
    }
    AcquireSRWLockExclusive(&lock);
    if (file == NULL && on) {
        open_now();
        opened_for_setting = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_want(int on)
{
    InterlockedExchange(&wanted, on != 0);
    AcquireSRWLockExclusive(&lock);
    if (on && file == NULL) {
        open_now();
        opened_for_setting = 1;
    } else if (!on && file != NULL && opened_for_setting) {
        fprintf(file, "log: turned off in the settings\n");
        fclose(file);
        file = NULL;
        opened_for_setting = 0;
        fprintf(stderr, "log: turned off in the settings\n");
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_log_on(void)
{
    return file != NULL;
}

const char *khdays_log_path(void)
{
    return file != NULL ? log_path : NULL;
}

void khdays_log_frame(u32 value)
{
    InterlockedExchange(&frame, (LONG)value);
    if (file != NULL && value - flushed_frame >= 60) {
        flushed_frame = value;
        AcquireSRWLockExclusive(&lock);
        if (file != NULL) {
            fflush(file);
        }
        ReleaseSRWLockExclusive(&lock);
    }
}

#define MEMORY_EVERY_S 60

#if !defined(_WIN32)
/* Reads `path` to its end through `buffer` without the C library's streams (they allocate, and this is read
 * when there may be no memory left to give): the bytes read, or -1 when it cannot be read whole. `lines`,
 * when given, takes its line ends; the buffer keeps the file's first bytes, ended with a 0. */
static long long read_proc(const char *path, char *buffer, size_t size, unsigned long long *lines)
{
    static char rest[16384];
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    long long total = 0;
    size_t kept = 0;
    if (fd < 0) {
        return -1;
    }
    for (;;) {
        const ssize_t n = read(fd, rest, sizeof(rest));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) {
            break;
        }
        for (ssize_t i = 0; i < n; ++i) {
            if (lines != NULL && rest[i] == '\n') {
                ++*lines;
            }
            if (kept + 1 < size) {
                buffer[kept++] = rest[i];
            }
        }
        total += n;
    }
    close(fd);
    buffer[kept] = '\0';
    return total;
}
#endif

void khdays_log_memory(void)
{
    static LONGLONG next;
    LARGE_INTEGER now, rate;
    if (!khdays_log_on()) {
        return;
    }
    QueryPerformanceCounter(&now);
    if (next != 0 && now.QuadPart < next) {
        return;
    }
    QueryPerformanceFrequency(&rate);
    next = now.QuadPart + MEMORY_EVERY_S * rate.QuadPart;
#if !defined(_WIN32)
    {
        /* /proc/self/statm: the program's size and what of it is in memory, in pages; /proc/self/maps: a
         * line a mapping. What cannot be read is said so, not counted as none */
        char text[64], size_text[32] = "?", held_text[32] = "?", maps_text[32] = "?";
        unsigned long long size = 0, resident = 0, mappings = 0;
        const unsigned long long page = (unsigned long long)sysconf(_SC_PAGESIZE);
        if (read_proc("/proc/self/statm", text, sizeof(text), NULL) > 0 &&
            sscanf(text, "%llu %llu", &size, &resident) == 2) {
            snprintf(size_text, sizeof(size_text), "%llu", size * page >> 20);
            snprintf(held_text, sizeof(held_text), "%llu", resident * page >> 20);
        }
        if (read_proc("/proc/self/maps", text, sizeof(text), &mappings) > 0) {
            snprintf(maps_text, sizeof(maps_text), "%llu", mappings);
        }
        khdays_log("memory: this game holds %s MB (%s MB of addresses) in %s mappings", held_text, size_text,
                   maps_text);
    }
#else
    {
        MEMORYSTATUSEX memory;
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory)) {
            khdays_log("memory: %llu MB free, %llu MB of this game's addresses in use",
                       (unsigned long long)(memory.ullAvailPhys >> 20),
                       (unsigned long long)((memory.ullTotalVirtual - memory.ullAvailVirtual) >> 20));
        }
    }
#endif
}

void khdays_log(const char *format, ...)
{
    LARGE_INTEGER now;
    va_list args;
    if (file == NULL) {
        return;
    }
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fprintf(file, "%9.3f f%-6ld ", (double)(now.QuadPart - started.QuadPart) / (double)frequency.QuadPart,
                (long)frame);
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fputc('\n', file);
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_line(const char *line)
{
    if (file == NULL) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fprintf(file, "stderr: %s\n", line);
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_mark(void)
{
    if (file == NULL) {
        return;
    }
    ++marks;
    khdays_log("mark %u (F12)", marks);
    AcquireSRWLockExclusive(&lock);
    fflush(file);
    ReleaseSRWLockExclusive(&lock);
    fprintf(stderr, "log: mark %u\n", marks);
}

u32 khdays_log_marks(void)
{
    return marks;
}

#if defined(__ANDROID__)
void khdays_android_log_drain(void); /* host/posix/android_log.c */
#endif

void khdays_log_close(void)
{
#if defined(__ANDROID__)
    khdays_android_log_drain(); /* the stderr lines written just before (a stop's reason before exit()) in it */
#endif
    /* a crash may come inside a line, the lock held: then only flushed */
    const BOOLEAN locked = TryAcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fflush(file);
        if (locked) {
            fclose(file);
            file = NULL;
        }
    }
    if (locked) {
        ReleaseSRWLockExclusive(&lock);
    }
}
