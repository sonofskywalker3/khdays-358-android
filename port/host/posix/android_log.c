/* stdout and stderr into Android's log (logcat, tag "khdays"): an app's streams go nowhere otherwise, and
 * the port reports through them. A thread reads both through a pipe and writes each line. */
#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int pipe_ends[2];

static void *forward(void *unused)
{
    char buffer[1024];
    size_t used = 0;
    (void)unused;
    for (;;) {
        const ssize_t got = read(pipe_ends[0], buffer + used, sizeof(buffer) - 1 - used);
        size_t start = 0;
        if (got <= 0) {
            break;
        }
        used += (size_t)got;
        for (size_t i = 0; i < used; ++i) {
            if (buffer[i] == '\n') {
                buffer[i] = '\0';
                __android_log_write(ANDROID_LOG_INFO, "khdays", buffer + start);
                start = i + 1;
            }
        }
        if (start == 0 && used == sizeof(buffer) - 1) { /* a line longer than the buffer: in parts */
            buffer[used] = '\0';
            __android_log_write(ANDROID_LOG_INFO, "khdays", buffer);
            used = 0;
        } else {
            memmove(buffer, buffer + start, used - start);
            used -= start;
        }
    }
    return NULL;
}

void khdays_android_log_streams(void)
{
    pthread_t thread;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    if (pipe(pipe_ends) != 0) {
        return;
    }
    dup2(pipe_ends[1], STDOUT_FILENO);
    dup2(pipe_ends[1], STDERR_FILENO);
    if (pthread_create(&thread, NULL, forward, NULL) == 0) {
        pthread_detach(thread);
    }
}
#endif
