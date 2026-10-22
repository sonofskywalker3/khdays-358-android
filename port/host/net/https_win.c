/* host/net/https.h on Windows: WinHTTP, with the system's proxies and certificates. */
#include "https.h"

#include <windows.h>
#include <winhttp.h>

#include <stdlib.h>
#include <string.h>

/* `s` (UTF-8) as a wide string, to give back with free(); NULL if it cannot be made. */
static wchar_t *wide(const char *s)
{
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
    if (w != NULL && MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n) != n) {
        free(w);
        w = NULL;
    }
    return w;
}

/* The `n` wide characters at `s` as a string of their own, to give back with free(). */
static wchar_t *piece(const wchar_t *s, DWORD n)
{
    wchar_t *w = (wchar_t *)malloc(((size_t)n + 1) * sizeof(wchar_t));
    if (w != NULL) {
        if (n > 0) {
            memcpy(w, s, (size_t)n * sizeof(wchar_t));
        }
        w[n] = L'\0';
    }
    return w;
}

/* One GET's handles. */
typedef struct Request {
    HINTERNET session, connection, request;
} Request;

static void request_close(Request *r)
{
    if (r->request != NULL) {
        WinHttpCloseHandle(r->request);
    }
    if (r->connection != NULL) {
        WinHttpCloseHandle(r->connection);
    }
    if (r->session != NULL) {
        WinHttpCloseHandle(r->session);
    }
    memset(r, 0, sizeof(*r));
}

/* Sends a GET for `url` (with `accept` as its Accept header, or none) and waits for the answer's
 * headers, following redirects (a release's file is served from another address): 1 when it is
 * 200 OK, `r` then open to read its body; 0 otherwise, `r` closed. */
static int request_open(Request *r, const char *url, const char *agent, int timeout_ms, const wchar_t *accept)
{
    wchar_t *wurl = wide(url), *wagent = wide(agent), *host = NULL, *path = NULL;
    URL_COMPONENTS parts;
    DWORD status = 0, status_size = sizeof(status);
    int ok = 0;
    memset(r, 0, sizeof(*r));
    if (wurl == NULL || wagent == NULL) {
        goto done;
    }
    ZeroMemory(&parts, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wurl, 0, 0, &parts) || parts.lpszHostName == NULL || parts.lpszUrlPath == NULL) {
        goto done;
    }
    host = piece(parts.lpszHostName, parts.dwHostNameLength);
    /* the path with its query after it: the two follow each other in the address */
    path = parts.dwUrlPathLength + parts.dwExtraInfoLength > 0
               ? piece(parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength)
               : piece(L"/", 1);
    if (host == NULL || path == NULL) {
        goto done;
    }
    r->session =
        WinHttpOpen(wagent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (r->session == NULL) {
        goto done;
    }
    WinHttpSetTimeouts(r->session, timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    r->connection = WinHttpConnect(r->session, host, parts.nPort, 0);
    if (r->connection == NULL) {
        goto done;
    }
    r->request = WinHttpOpenRequest(r->connection, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (r->request == NULL) {
        goto done;
    }
    if (accept != NULL) {
        WinHttpAddRequestHeaders(r->request, accept, (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (!WinHttpSendRequest(r->request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r->request, NULL)) {
        goto done;
    }
    ok = WinHttpQueryHeaders(r->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) &&
         status == 200;
done:
    if (!ok) {
        request_close(r);
    }
    free(path);
    free(host);
    free(wagent);
    free(wurl);
    return ok;
}

char *khdays_https_get(const char *url, const char *agent, int timeout_ms)
{
    Request r;
    char *body = NULL;
    size_t size = 0;
    int whole = 0;
    if (!request_open(&r, url, agent, timeout_ms, L"Accept: application/vnd.github+json")) {
        return NULL;
    }
    for (;;) {
        DWORD available = 0, read = 0;
        char *grown;
        if (!WinHttpQueryDataAvailable(r.request, &available)) {
            goto done;
        }
        if (available == 0) {
            break;
        }
        grown = (char *)realloc(body, size + available + 1);
        if (grown == NULL) {
            goto done;
        }
        body = grown;
        if (!WinHttpReadData(r.request, body + size, available, &read)) {
            goto done;
        }
        size += read;
    }
    if (body == NULL) {
        body = (char *)malloc(1);
        if (body == NULL) {
            goto done;
        }
    }
    body[size] = '\0';
    whole = 1;
done:
    if (!whole) {
        free(body);
        body = NULL;
    }
    request_close(&r);
    return body;
}

int khdays_https_download(const char *url, const char *agent, const char *path, int timeout_ms,
                          KhdaysDownloadProgress progress, void *context)
{
    enum { CHUNK = 64 * 1024 };
    Request r;
    wchar_t *wpath = wide(path);
    HANDLE file = INVALID_HANDLE_VALUE;
    char *chunk = (char *)malloc(CHUNK);
    long long done = 0;
    int whole = 0;
    if (wpath == NULL || chunk == NULL || !request_open(&r, url, agent, timeout_ms, NULL)) {
        free(chunk);
        free(wpath);
        return 0;
    }
    file = CreateFileW(wpath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        goto done;
    }
    for (;;) {
        DWORD read = 0, written = 0;
        if (!WinHttpReadData(r.request, chunk, CHUNK, &read)) {
            goto done;
        }
        if (read == 0) {
            break; /* the whole body */
        }
        if (!WriteFile(file, chunk, read, &written, NULL) || written != read) {
            goto done;
        }
        done += read;
        if (progress != NULL && progress(context, done)) {
            goto done; /* given up */
        }
    }
    whole = 1;
done:
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        if (!whole) {
            DeleteFileW(wpath);
        }
    }
    request_close(&r);
    free(chunk);
    free(wpath);
    return whole;
}
