/* The few Windows calls the runtime's portable files make (locks, atomics, the counter, events, threads,
 * folders), on POSIX: on the include path only off Windows. What needs more than a call's worth
 * (memory, faults, contexts, files) has its own _posix file. */
#ifndef KHDAYS_POSIX_WINDOWS_H
#define KHDAYS_POSIX_WINDOWS_H

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef unsigned int DWORD;
typedef unsigned int ULONG;
typedef unsigned int UINT;
typedef int LONG;
typedef long long LONG64;
typedef unsigned long long ULONG64;
typedef unsigned long long DWORD64;
typedef size_t SIZE_T;
typedef void *PVOID;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef void *HANDLE;
typedef void *HMODULE;
typedef union {
    long long QuadPart;
} LARGE_INTEGER;

#define WINAPI
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define INFINITE 0xffffffffu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xffffffffu
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES 0xffffffffu
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#define FILE_ATTRIBUTE_NORMAL 0x80u
#define ERROR_ALREADY_EXISTS 183u
#define ERROR_FILE_NOT_FOUND 2u

typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned char BOOLEAN;

/* Slim reader/writer locks: a recursive mutex, shared or exclusive alike (a condition variable waits
 * on it, and a reader may take it again) */
typedef pthread_mutex_t SRWLOCK;
#define SRWLOCK_INIT PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
static inline void InitializeSRWLock(SRWLOCK *lock)
{
    pthread_mutexattr_t attributes;
    pthread_mutexattr_init(&attributes);
    pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(lock, &attributes);
    pthread_mutexattr_destroy(&attributes);
}
#define AcquireSRWLockExclusive(lock) pthread_mutex_lock(lock)
#define ReleaseSRWLockExclusive(lock) pthread_mutex_unlock(lock)
#define AcquireSRWLockShared(lock) pthread_mutex_lock(lock)
#define ReleaseSRWLockShared(lock) pthread_mutex_unlock(lock)
#define TryAcquireSRWLockExclusive(lock) (pthread_mutex_trylock(lock) == 0)

/* Condition variables, over those locks */
typedef pthread_cond_t CONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT PTHREAD_COND_INITIALIZER
#define InitializeConditionVariable(variable) pthread_cond_init((variable), NULL)
#define WakeConditionVariable(variable) pthread_cond_signal(variable)
#define WakeAllConditionVariable(variable) pthread_cond_broadcast(variable)
static inline BOOL SleepConditionVariableSRW(CONDITION_VARIABLE *variable, SRWLOCK *lock, DWORD milliseconds,
                                             ULONG flags)
{
    struct timespec until;
    (void)flags;
    if (milliseconds == INFINITE) {
        return pthread_cond_wait(variable, lock) == 0;
    }
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += (time_t)(milliseconds / 1000u);
    until.tv_nsec += (long)(milliseconds % 1000u) * 1000000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec += 1;
        until.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(variable, lock, &until) == 0;
}

/* One-time initialization */
typedef struct {
    pthread_mutex_t lock;
    int done;
    void *context;
} INIT_ONCE, *PINIT_ONCE;
#define INIT_ONCE_STATIC_INIT {PTHREAD_MUTEX_INITIALIZER, 0, NULL}
typedef BOOL(CALLBACK *PINIT_ONCE_FN)(PINIT_ONCE once, PVOID parameter, PVOID *context);
static inline BOOL InitOnceExecuteOnce(PINIT_ONCE once, PINIT_ONCE_FN function, PVOID parameter, PVOID *context)
{
    BOOL ok = TRUE;
    pthread_mutex_lock(&once->lock);
    if (!once->done) {
        ok = function(once, parameter, &once->context);
        once->done = ok;
    }
    pthread_mutex_unlock(&once->lock);
    if (context != NULL) {
        *context = once->context;
    }
    return ok;
}

#if defined(__aarch64__)
#define YieldProcessor() __asm__ __volatile__("yield")
#else
#define YieldProcessor() ((void)0)
#endif

/* Interlocked operations, on whatever integer the caller declared (LONG, long, LONG64) */
#define InterlockedExchange(target, value) __atomic_exchange_n((target), (value), __ATOMIC_SEQ_CST)
#define InterlockedExchange64(target, value) __atomic_exchange_n((target), (value), __ATOMIC_SEQ_CST)
#define InterlockedExchangePointer(target, value) __atomic_exchange_n((target), (value), __ATOMIC_SEQ_CST)
#define InterlockedIncrement(target) __atomic_add_fetch((target), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(target) __atomic_sub_fetch((target), 1, __ATOMIC_SEQ_CST)
#define InterlockedIncrement64(target) __atomic_add_fetch((target), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd(target, value) __atomic_fetch_add((target), (value), __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd64(target, value) __atomic_fetch_add((target), (value), __ATOMIC_SEQ_CST)
#define InterlockedAdd64(target, value) __atomic_add_fetch((target), (value), __ATOMIC_SEQ_CST)
#define InterlockedCompareExchange(target, exchange, comparand)                                                       \
    __extension__({                                                                                                    \
        __typeof__(*(target)) khdays_expected_ = (comparand);                                                          \
        __atomic_compare_exchange_n((target), &khdays_expected_, (exchange), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);   \
        khdays_expected_;                                                                                              \
    })
#define InterlockedCompareExchange64 InterlockedCompareExchange
#define InterlockedCompareExchangePointer InterlockedCompareExchange
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)

/* The performance counter: the monotonic clock in nanoseconds */
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER *count)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    count->QuadPart = (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
    return TRUE;
}
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency)
{
    frequency->QuadPart = 1000000000LL;
    return TRUE;
}
static inline unsigned long long GetTickCount64(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000ull + (unsigned long long)now.tv_nsec / 1000000ull;
}
static inline void Sleep(DWORD milliseconds)
{
    struct timespec span = {(time_t)(milliseconds / 1000u), (long)(milliseconds % 1000u) * 1000000L};
    while (nanosleep(&span, &span) != 0 && errno == EINTR) {
    }
}
#define timeBeginPeriod(period) ((void)(period), 0u)
#define timeEndPeriod(period) ((void)(period), 0u)

/* Threads and events: handles of host/posix/shim.c */
typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID parameter);
HANDLE khdays_posix_create_thread(LPTHREAD_START_ROUTINE start, LPVOID parameter);
HANDLE khdays_posix_create_event(BOOL manual_reset, BOOL initial_state);
BOOL SetEvent(HANDLE event);
BOOL ResetEvent(HANDLE event);
DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
BOOL CloseHandle(HANDLE handle);
#define CreateThread(attributes, stack, start, parameter, flags, id)                                                   \
    khdays_posix_create_thread((LPTHREAD_START_ROUTINE)(start), (parameter))
#define CreateEventA(attributes, manual, initial, name) khdays_posix_create_event((manual), (initial))
#define CreateEventW CreateEventA

static inline DWORD GetCurrentThreadId(void)
{
    return (DWORD)gettid();
}
static inline DWORD GetCurrentProcessId(void)
{
    return (DWORD)getpid();
}
static inline DWORD GetLastError(void)
{
    return errno == EEXIST ? ERROR_ALREADY_EXISTS : errno == ENOENT ? ERROR_FILE_NOT_FOUND : (DWORD)errno;
}
#define ExitProcess(code) exit((int)(code))

/* Folders and files. The port's paths are Windows's, "mods\<Mod>\sounds": each backslash a slash
 * here (FindFirstFileA's pattern too, shim.c), or the whole path would be one name. */
static inline void posix_path(const char *path, char out[MAX_PATH])
{
    size_t i;
    for (i = 0; path[i] != '\0' && i < MAX_PATH - 1; ++i) {
        out[i] = path[i] == '\\' ? '/' : path[i];
    }
    out[i] = '\0';
}
static inline BOOL CreateDirectoryA(const char *path, void *attributes)
{
    char posix[MAX_PATH];
    (void)attributes;
    posix_path(path, posix);
    return mkdir(posix, 0777) == 0;
}
static inline DWORD GetFileAttributesA(const char *path)
{
    char posix[MAX_PATH];
    struct stat s;
    posix_path(path, posix);
    if (stat(posix, &s) != 0) {
        return INVALID_FILE_ATTRIBUTES;
    }
    return S_ISDIR(s.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}
static inline DWORD GetEnvironmentVariableA(const char *name, char *buffer, DWORD size)
{
    const char *value = getenv(name);
    size_t length;
    if (value == NULL) {
        return 0;
    }
    length = strlen(value);
    if (length + 1 > size) {
        return (DWORD)(length + 1);
    }
    memcpy(buffer, value, length + 1);
    return (DWORD)length;
}

/* A folder's listing, for "folder\*" or "folder/*.ext" patterns */
typedef struct {
    DWORD dwFileAttributes;
    char cFileName[MAX_PATH];
} WIN32_FIND_DATAA;
HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *found);
BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA *found);
BOOL FindClose(HANDLE find);

/* The local time */
typedef struct {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME;
static inline void GetLocalTime(SYSTEMTIME *out)
{
    struct timespec now;
    struct tm local;
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &local);
    out->wYear = (WORD)(local.tm_year + 1900);
    out->wMonth = (WORD)(local.tm_mon + 1);
    out->wDayOfWeek = (WORD)local.tm_wday;
    out->wDay = (WORD)local.tm_mday;
    out->wHour = (WORD)local.tm_hour;
    out->wMinute = (WORD)local.tm_min;
    out->wSecond = (WORD)local.tm_sec;
    out->wMilliseconds = (WORD)(now.tv_nsec / 1000000L);
}

/* A path made absolute */
static inline DWORD GetFullPathNameA(const char *path, DWORD size, char *out, char **file_part)
{
    char resolved[4096];
    size_t length;
    (void)file_part;
    if (path[0] == '/') {
        snprintf(resolved, sizeof(resolved), "%s", path);
    } else {
        char here[4096];
        if (getcwd(here, sizeof(here)) == NULL) {
            return 0;
        }
        snprintf(resolved, sizeof(resolved), "%s/%s", here, path);
    }
    length = strlen(resolved);
    if (length + 1 > size) {
        return (DWORD)(length + 1);
    }
    memcpy(out, resolved, length + 1);
    return (DWORD)length;
}

/* The C library's MSVC names */
#include "msvc_crt.h"

#endif
