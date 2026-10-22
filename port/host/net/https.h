/* HTTP(S) requests, for the update check and the update's download (host/app/updates.c). Each
 * system has its own (https_win.c on Windows). */
#ifndef KHDAYS_HOST_HTTPS_H
#define KHDAYS_HOST_HTTPS_H

/* GETs `url` (http or https) with `agent` as its User-Agent, each step waiting at most
 * `timeout_ms`: the answer's body, NUL-ended, to give back with free(), when the answer is 200 OK;
 * NULL otherwise. */
char *khdays_https_get(const char *url, const char *agent, int timeout_ms);

/* Told of each piece of a download (`done` bytes so far); a nonzero return gives it up. */
typedef int (*KhdaysDownloadProgress)(void *context, long long done);

/* GETs `url` into the file `path` (UTF-8, made anew), each step waiting at most `timeout_ms`,
 * following redirects: 1 when the answer was 200 OK and its whole body is written; 0 otherwise (a
 * failure, or `progress` gave it up), the file then removed. */
int khdays_https_download(const char *url, const char *agent, const char *path, int timeout_ms,
                          KhdaysDownloadProgress progress, void *context);

#endif
