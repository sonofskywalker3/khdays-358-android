/* The handles behind host/posix/include/windows.h: threads, events and folder listings on POSIX. A
 * thread's handle is signalled when it ends, an event's when set, as WaitForSingleObject sees them. */
#include <windows.h>

#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>

enum { KIND_THREAD = 1, KIND_EVENT, KIND_FIND };

typedef struct Handle {
    int kind;
    pthread_mutex_t lock;
    pthread_cond_t changed;
    int signalled;
    int manual_reset; /* an event's; a thread's handle stays signalled once it ended */
    pthread_t thread;
    LPTHREAD_START_ROUTINE start;
    void *parameter;
    int detached; /* closed before it ended: it frees itself */
    DIR *dir;
    char pattern[MAX_PATH];
} Handle;

static Handle *new_handle(int kind)
{
    Handle *h = (Handle *)calloc(1, sizeof(*h));
    if (h != NULL) {
        h->kind = kind;
        pthread_mutex_init(&h->lock, NULL);
        pthread_cond_init(&h->changed, NULL);
    }
    return h;
}

static void free_handle(Handle *h)
{
    pthread_cond_destroy(&h->changed);
    pthread_mutex_destroy(&h->lock);
    free(h);
}

static void *thread_main(void *argument)
{
    Handle *h = (Handle *)argument;
    int detached;
    h->start(h->parameter);
    pthread_mutex_lock(&h->lock);
    h->signalled = 1;
    detached = h->detached;
    pthread_cond_broadcast(&h->changed);
    pthread_mutex_unlock(&h->lock);
    if (detached) {
        free_handle(h);
    }
    return NULL;
}

HANDLE khdays_posix_create_thread(LPTHREAD_START_ROUTINE start, LPVOID parameter)
{
    Handle *h = new_handle(KIND_THREAD);
    if (h == NULL) {
        return NULL;
    }
    h->start = start;
    h->parameter = parameter;
    h->manual_reset = 1;
    if (pthread_create(&h->thread, NULL, thread_main, h) != 0) {
        free_handle(h);
        return NULL;
    }
    pthread_detach(h->thread);
    return h;
}

HANDLE khdays_posix_create_event(BOOL manual_reset, BOOL initial_state)
{
    Handle *h = new_handle(KIND_EVENT);
    if (h != NULL) {
        h->manual_reset = manual_reset;
        h->signalled = initial_state;
    }
    return h;
}

BOOL SetEvent(HANDLE event)
{
    Handle *h = (Handle *)event;
    pthread_mutex_lock(&h->lock);
    h->signalled = 1;
    pthread_cond_broadcast(&h->changed);
    pthread_mutex_unlock(&h->lock);
    return TRUE;
}

BOOL ResetEvent(HANDLE event)
{
    Handle *h = (Handle *)event;
    pthread_mutex_lock(&h->lock);
    h->signalled = 0;
    pthread_mutex_unlock(&h->lock);
    return TRUE;
}

DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds)
{
    Handle *h = (Handle *)handle;
    struct timespec until;
    DWORD result = WAIT_OBJECT_0;
    if (h == NULL) {
        return WAIT_FAILED;
    }
    if (milliseconds != INFINITE) {
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += (time_t)(milliseconds / 1000u);
        until.tv_nsec += (long)(milliseconds % 1000u) * 1000000L;
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec += 1;
            until.tv_nsec -= 1000000000L;
        }
    }
    pthread_mutex_lock(&h->lock);
    while (!h->signalled) {
        if (milliseconds == INFINITE) {
            pthread_cond_wait(&h->changed, &h->lock);
        } else if (milliseconds == 0 || pthread_cond_timedwait(&h->changed, &h->lock, &until) == ETIMEDOUT) {
            result = h->signalled ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
            break;
        }
    }
    if (result == WAIT_OBJECT_0 && !h->manual_reset) {
        h->signalled = 0; /* an auto-reset event lets one waiter through */
    }
    pthread_mutex_unlock(&h->lock);
    return result;
}

BOOL CloseHandle(HANDLE handle)
{
    Handle *h = (Handle *)handle;
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    if (h->kind == KIND_THREAD) {
        pthread_mutex_lock(&h->lock);
        if (!h->signalled) {
            h->detached = 1; /* the thread frees it when it ends */
            pthread_mutex_unlock(&h->lock);
            return TRUE;
        }
        pthread_mutex_unlock(&h->lock);
    } else if (h->kind == KIND_FIND && h->dir != NULL) {
        closedir(h->dir);
    }
    free_handle(h);
    return TRUE;
}

/* The next entry of a listing that matches its pattern, or 0 at its end. */
static BOOL next_match(Handle *h, WIN32_FIND_DATAA *found)
{
    struct dirent *entry;
    while ((entry = readdir(h->dir)) != NULL) {
        if (fnmatch(h->pattern, entry->d_name, 0) == 0) {
            strncpy(found->cFileName, entry->d_name, MAX_PATH - 1);
            found->cFileName[MAX_PATH - 1] = '\0';
            int folder = entry->d_type == DT_DIR;
            struct stat s;
            if (entry->d_type == DT_UNKNOWN && fstatat(dirfd(h->dir), entry->d_name, &s, 0) == 0) {
                folder = S_ISDIR(s.st_mode); /* a file system that leaves the kind out of its listing */
            }
            found->dwFileAttributes = folder ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            return TRUE;
        }
    }
    return FALSE;
}

HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *found)
{
    char folder[MAX_PATH];
    char *slash;
    Handle *h;
    size_t i;
    for (i = 0; pattern[i] != '\0' && i < MAX_PATH - 1; ++i) {
        folder[i] = pattern[i] == '\\' ? '/' : pattern[i];
    }
    folder[i] = '\0';
    slash = strrchr(folder, '/');
    h = new_handle(KIND_FIND);
    if (h == NULL) {
        return INVALID_HANDLE_VALUE;
    }
    if (slash != NULL) {
        *slash = '\0';
        snprintf(h->pattern, sizeof(h->pattern), "%s", slash + 1);
        h->dir = opendir(folder[0] != '\0' ? folder : "/");
    } else {
        snprintf(h->pattern, sizeof(h->pattern), "%s", folder);
        h->dir = opendir(".");
    }
    if (h->dir == NULL || !next_match(h, found)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

BOOL FindNextFileA(HANDLE find, WIN32_FIND_DATAA *found)
{
    return next_match((Handle *)find, found);
}

BOOL FindClose(HANDLE find)
{
    return CloseHandle(find);
}
