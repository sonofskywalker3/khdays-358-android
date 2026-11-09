/* stdout and stderr into Android's log (logcat, tag "khdays") and the session log: an app's streams go nowhere
 * otherwise, and the port reports through them. A thread waits on a pipe and writes each line; the session log's
 * close (at exit, on a crash) takes what is left itself (khdays_android_log_drain). */
#if defined(__ANDROID__)
#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../runtime/core/session_log.h"

#define LINE_ROOM 1024
#define DRAIN_MS 200
#define DRAIN_STEP_US 5000

static int pipe_ends[2] = {-1, -1};
/* held while the pipe is read and its lines written, by the thread or by a drain: one that holds it has seen every
 * byte read so far written out */
static pthread_mutex_t reading = PTHREAD_MUTEX_INITIALIZER;
static char buffer[LINE_ROOM];
static size_t used;

static void write_line(const char *line)
{
    __android_log_write(ANDROID_LOG_INFO, "khdays", line);
    khdays_log_line(line); /* the session log too, for a player to send */
}

/* What the pipe holds now, its whole lines written (`reading` held; the pipe's reads never wait). 0 at its end. */
static int take_available(void)
{
    for (;;) {
        const ssize_t got = read(pipe_ends[0], buffer + used, sizeof(buffer) - 1 - used);
        if (got == 0) {
            return 0;
        }
        if (got < 0) {
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        size_t start = 0;
        used += (size_t)got;
        for (size_t i = 0; i < used; ++i) {
            if (buffer[i] == '\n') {
                buffer[i] = '\0';
                write_line(buffer + start);
                start = i + 1;
            }
        }
        if (start == 0 && used == sizeof(buffer) - 1) { /* a line longer than the buffer: in parts */
            buffer[used] = '\0';
            write_line(buffer);
            used = 0;
        } else {
            memmove(buffer, buffer + start, used - start);
            used -= start;
        }
    }
}

static void *forward(void *unused)
{
    (void)unused;
    for (;;) {
        struct pollfd wait = {pipe_ends[0], POLLIN, 0};
        if (poll(&wait, 1, -1) < 0 && errno != EINTR) {
            return NULL;
        }
        pthread_mutex_lock(&reading);
        const int open = take_available();
        pthread_mutex_unlock(&reading);
        if (!open) {
            return NULL;
        }
    }
}

/* What stderr holds written out before the session log closes (a stop writes its reason just before exit()):
 * taken here, once the thread is not inside a read; given up after DRAIN_MS (the thread stopped inside one, a
 * crash on it) -- never waits longer, a crash's handler calls it. */
void khdays_android_log_drain(void)
{
    if (pipe_ends[0] < 0) {
        return;
    }
    for (int waited = 0; waited < DRAIN_MS * 1000; waited += DRAIN_STEP_US) {
        if (pthread_mutex_trylock(&reading) == 0) {
            take_available();
            pthread_mutex_unlock(&reading);
            return;
        }
        usleep(DRAIN_STEP_US);
    }
}

void khdays_android_log_streams(void)
{
    pthread_t thread;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    if (pipe(pipe_ends) != 0) {
        pipe_ends[0] = pipe_ends[1] = -1;
        return;
    }
    fcntl(pipe_ends[0], F_SETFL, fcntl(pipe_ends[0], F_GETFL) | O_NONBLOCK); /* the reads: never waiting */
    dup2(pipe_ends[1], STDOUT_FILENO);
    dup2(pipe_ends[1], STDERR_FILENO);
    if (pthread_create(&thread, NULL, forward, NULL) == 0) {
        pthread_detach(thread);
    }
}
#endif
