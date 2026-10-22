/* https.h where the port has no HTTPS client (all but Windows, for now): no answer, so the update check
 * says nothing. */
#include "https.h"

#include <stddef.h>

char *khdays_https_get(const char *url, const char *agent, int timeout_ms)
{
    (void)url;
    (void)agent;
    (void)timeout_ms;
    return NULL;
}

int khdays_https_download(const char *url, const char *agent, const char *path, int timeout_ms,
                          KhdaysDownloadProgress progress, void *context)
{
    (void)url;
    (void)agent;
    (void)path;
    (void)timeout_ms;
    (void)progress;
    (void)context;
    return 0;
}
