/* host/app/updates.h. */
#include "updates.h"

#include "release_version.h"
#include "update_install.h"
#include "../net/https.h"
#include "../../runtime/core/session_log.h"
#include "../../runtime/core/sha256.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef KHDAYS_VERSION
#define KHDAYS_VERSION "0.0.0" /* port/CMakeLists.txt gives the VERSION file's */
#endif

#if defined(__ANDROID__)
/* The Android port's releases: test builds (pre-releases, which /releases/latest leaves out) tagged
 * "android-<version>", each with its APK (khdays_release_newest_android) */
#define REPOSITORY "sonofskywalker3/khdays-thor"
#define LATEST "https://api.github.com/repos/" REPOSITORY "/releases?per_page=30"
#define TAG_PREFIX "android-"
#else
#define REPOSITORY "Yokimitsuro/khdays-port"
#define LATEST "https://api.github.com/repos/" REPOSITORY "/releases/latest"
#define TAG_PREFIX ""
#endif
#define DOWNLOADS "https://github.com/" REPOSITORY "/releases/download/"
#define STATE "saves/updates.txt"

/* a version's tag is short: a longer one is no tag of the port's */
enum { TAG = 64 };

static SDL_Mutex *lock;   /* latest, installer, skip, asked and the download: the threads write them */
static char latest[TAG];  /* the newest release GitHub gave ("v0.1.1"), or "" */
static char skip[TAG];    /* the release the player said not to be told of */
static long long asked;   /* when GitHub was last asked, in seconds since 1970 */
static int looking, told; /* the window thread's: the check is on; the player answered this run */
/* the latest release's installer as GitHub listed it, its name "" when it listed none */
static KhdaysReleaseFile installer;

/* The download of the installer (KHDAYS_DOWNLOAD_*), how far it is, the file it made, why it failed. */
static int download_state;
static long long download_done;
static SDL_AtomicInt download_cancel;
static char download_file[1024], download_why[256];

const char *khdays_updates_version(void)
{
    return KHDAYS_VERSION;
}

/* The address the check asks instead of GitHub (KHDAYS_UPDATE_SOURCE), for a test; NULL when none. */
static const char *test_source(void)
{
    const char *source = SDL_getenv("KHDAYS_UPDATE_SOURCE");
    return source != NULL && *source != '\0' ? source : NULL;
}

static char *trim(char *s)
{
    char *end;
    while (*s == ' ' || *s == '\t') {
        ++s;
    }
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        *--end = '\0';
    }
    return s;
}

static void keep_tag(char *to, const char *value)
{
    if (strlen(value) < TAG && khdays_version_valid(value)) {
        SDL_strlcpy(to, value, TAG);
    }
}

/* Whether the installer's address is one of the port's releases' files (any address for a test). */
static int trusted(const char *url)
{
    return test_source() != NULL || strncmp(url, DOWNLOADS, sizeof(DOWNLOADS) - 1) == 0;
}

static void read_state(void)
{
    char *text = (char *)SDL_LoadFile(STATE, NULL);
    char *line = text;
    if (text == NULL) {
        return;
    }
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n'), *equals;
        if (next != NULL) {
            *next++ = '\0';
        }
        equals = strchr(line, '=');
        if (line[0] != '#' && equals != NULL) {
            char *key, *value;
            *equals = '\0';
            key = trim(line);
            value = trim(equals + 1);
            if (strcmp(key, "asked") == 0) {
                asked = strtoll(value, NULL, 10);
            } else if (strcmp(key, "latest") == 0) {
                keep_tag(latest, value);
            } else if (strcmp(key, "skip") == 0) {
                keep_tag(skip, value);
            } else if (strcmp(key, "installer") == 0) {
                SDL_strlcpy(installer.name, value, sizeof(installer.name));
            } else if (strcmp(key, "installer_url") == 0) {
                SDL_strlcpy(installer.url, value, sizeof(installer.url));
            } else if (strcmp(key, "installer_sha256") == 0) {
                SDL_strlcpy(installer.sha256, value, sizeof(installer.sha256));
            } else if (strcmp(key, "installer_size") == 0) {
                installer.size = strtoll(value, NULL, 10);
            }
        }
        line = next;
    }
    SDL_free(text);
    /* all of it, from the port's releases, or none */
    if (installer.name[0] == '\0' || !trusted(installer.url) || strlen(installer.sha256) != 64 || installer.size <= 0) {
        memset(&installer, 0, sizeof(installer));
    }
}

/* Under the lock. Written whole: a new file, then put in place. */
static void write_state(void)
{
    char *text = NULL;
    const int n = SDL_asprintf(&text,
                               "# The port's look for a newer release (host/app/updates.c): when GitHub was last\n"
                               "# asked, the newest release it gave and its installer, the one the player said not\n"
                               "# to be told of.\n"
                               "asked = %lld\nlatest = %s\ninstaller = %s\ninstaller_url = %s\ninstaller_sha256 = %s\n"
                               "installer_size = %lld\nskip = %s\n",
                               asked, latest, installer.name, installer.url, installer.sha256, installer.size, skip);
    SDL_CreateDirectory("saves");
    if (n > 0 && SDL_SaveFile(STATE ".partial", text, (size_t)n)) {
        SDL_RenamePath(STATE ".partial", STATE);
    }
    SDL_free(text);
}

static int SDLCALL ask(void *unused)
{
    const char *source = test_source();
    char agent[TAG + 16], tag[TAG];
    KhdaysReleaseFile found_installer = {0};
    char *json;
    int found, has_installer;
    (void)unused;
    khdays_update_install_clean(); /* the installer an earlier update ran */
    SDL_snprintf(agent, sizeof(agent), "khdays-port/%s", KHDAYS_VERSION);
    json = khdays_https_get(source != NULL ? source : LATEST, agent, 8000);
    if (json == NULL) {
        khdays_log("updates: no answer about the latest release (asked again at the next start)");
        return 0;
    }
#if defined(__ANDROID__)
    found = khdays_release_newest_android(json, tag, sizeof(tag), &found_installer);
    has_installer = found && trusted(found_installer.url);
#else
    found = khdays_release_tag(json, tag, sizeof(tag));
    has_installer = found && khdays_release_installer(json, &found_installer) && trusted(found_installer.url);
#endif
    free(json);
    SDL_LockMutex(lock);
    asked = (long long)time(NULL);
    if (found) {
        SDL_strlcpy(latest, tag, sizeof(latest));
        if (has_installer) {
            installer = found_installer;
        } else {
            memset(&installer, 0, sizeof(installer));
        }
    }
    write_state();
    SDL_UnlockMutex(lock);
    if (found) {
        khdays_log("updates: the latest release is %s (%s); this is %s", tag,
                   has_installer ? found_installer.name : "no installer listed", KHDAYS_VERSION);
        fprintf(stderr, "updates: the latest release is %s (%s); this is %s\n", tag,
                has_installer ? found_installer.name : "no installer listed", KHDAYS_VERSION);
    } else {
        khdays_log("updates: the answer named no release");
    }
    return 0;
}

void khdays_updates_start(int enabled)
{
    const char *off = SDL_getenv("KHDAYS_UPDATES");
    SDL_Thread *thread;
    /* turned off, or a run nobody watches (but for a test's own source) */
    if (!enabled || (SDL_getenv("KHDAYS_INPUT") != NULL && test_source() == NULL) ||
        SDL_getenv("KHDAYS_HEADLESS") != NULL || (off != NULL && SDL_strcasecmp(off, "off") == 0)) {
        return;
    }
    lock = SDL_CreateMutex();
    if (lock == NULL) {
        return;
    }
    read_state();
    looking = 1;
    /* at every start: a release is seen the first time the game starts after it (GitHub answers an
     * address 60 times an hour without signing in) */
    thread = SDL_CreateThread(ask, "updates", NULL);
    if (thread != NULL) {
        SDL_DetachThread(thread);
    }
}

int khdays_updates_offer(char *tag, size_t size)
{
    int offer;
    if (!looking || told || tag == NULL || size == 0) {
        return 0;
    }
    SDL_LockMutex(lock);
    offer = latest[0] != '\0' && khdays_version_compare(latest, KHDAYS_VERSION) > 0 && strcmp(latest, skip) != 0;
    if (offer) {
        SDL_strlcpy(tag, latest, size);
    }
    SDL_UnlockMutex(lock);
    return offer;
}

int khdays_updates_can_install(void)
{
    int can;
    if (!looking) {
        return 0;
    }
    SDL_LockMutex(lock);
    can = installer.name[0] != '\0';
    SDL_UnlockMutex(lock);
    return can && (khdays_update_install_possible() || test_source() != NULL);
}

static int download_progress(void *unused, long long done)
{
    (void)unused;
    SDL_LockMutex(lock);
    download_done = done;
    SDL_UnlockMutex(lock);
    return SDL_GetAtomicInt(&download_cancel);
}

/* Whether the file `path` is `size` bytes long with the SHA-256 `sha256`. */
static int file_matches(const char *path, long long size, const char *sha256)
{
    SDL_IOStream *in = SDL_IOFromFile(path, "rb");
    KhdaysSha256 h;
    unsigned char digest[32];
    char got[65];
    long long total = 0;
    size_t n;
    static unsigned char chunk[64 * 1024];
    if (in == NULL) {
        return 0;
    }
    khdays_sha256_begin(&h);
    while ((n = SDL_ReadIO(in, chunk, sizeof(chunk))) > 0) {
        khdays_sha256_add(&h, chunk, n);
        total += (long long)n;
    }
    SDL_CloseIO(in);
    khdays_sha256_end(&h, digest);
    for (int i = 0; i < 32; ++i) {
        SDL_snprintf(got + 2 * i, 3, "%02x", digest[i]);
    }
    return total == size && strcmp(got, sha256) == 0;
}

static void download_over(int state, const char *why)
{
    SDL_LockMutex(lock);
    download_state = state;
    SDL_strlcpy(download_why, why, sizeof(download_why));
    SDL_UnlockMutex(lock);
    if (state == KHDAYS_DOWNLOAD_FAILED) {
        khdays_log("updates: the installer was not downloaded: %s", why);
    }
}

static int SDLCALL fetch(void *unused)
{
    char folder[1024], file[1200], partial[1220], agent[TAG + 16];
    KhdaysReleaseFile want;
    (void)unused;
    SDL_LockMutex(lock);
    want = installer;
    SDL_UnlockMutex(lock);
    if (!khdays_update_install_folder(folder, sizeof(folder))) {
        download_over(KHDAYS_DOWNLOAD_FAILED, "no folder to download it into");
        return 0;
    }
    SDL_snprintf(file, sizeof(file), "%s%s", folder, want.name);
    SDL_snprintf(partial, sizeof(partial), "%s.partial", file);
    SDL_snprintf(agent, sizeof(agent), "khdays-port/%s", KHDAYS_VERSION);
    khdays_log("updates: downloading %s (%lld bytes)", want.url, want.size);
    if (!khdays_https_download(want.url, agent, partial, 15000, download_progress, NULL)) {
        download_over(SDL_GetAtomicInt(&download_cancel) ? KHDAYS_DOWNLOAD_NONE : KHDAYS_DOWNLOAD_FAILED,
                      SDL_GetAtomicInt(&download_cancel) ? "given up" : "the download did not complete");
        return 0;
    }
    if (!file_matches(partial, want.size, want.sha256)) {
        SDL_RemovePath(partial);
        download_over(KHDAYS_DOWNLOAD_FAILED, "the file is not the one GitHub lists (its size or SHA-256 differ)");
        return 0;
    }
    SDL_RemovePath(file);
    if (!SDL_RenamePath(partial, file)) {
        download_over(KHDAYS_DOWNLOAD_FAILED, "the file could not be put in place");
        return 0;
    }
    SDL_LockMutex(lock);
    SDL_strlcpy(download_file, file, sizeof(download_file));
    SDL_UnlockMutex(lock);
    khdays_log("updates: %s downloaded and checked (SHA-256 %s)", want.name, want.sha256);
    download_over(KHDAYS_DOWNLOAD_READY, "");
    return 0;
}

void khdays_updates_download(void)
{
    SDL_Thread *thread;
    if (!khdays_updates_can_install()) {
        return;
    }
    SDL_LockMutex(lock);
    if (download_state == KHDAYS_DOWNLOAD_GETTING || download_state == KHDAYS_DOWNLOAD_READY) {
        SDL_UnlockMutex(lock);
        return;
    }
    download_state = KHDAYS_DOWNLOAD_GETTING;
    download_done = 0;
    download_why[0] = '\0';
    SDL_UnlockMutex(lock);
    SDL_SetAtomicInt(&download_cancel, 0);
    thread = SDL_CreateThread(fetch, "update download", NULL);
    if (thread != NULL) {
        SDL_DetachThread(thread);
    } else {
        download_over(KHDAYS_DOWNLOAD_FAILED, "no thread for it");
    }
}

int khdays_updates_download_state(long long *done, long long *total, char *why, size_t size)
{
    int state;
    if (!looking) {
        return KHDAYS_DOWNLOAD_NONE;
    }
    SDL_LockMutex(lock);
    state = download_state;
    if (done != NULL) {
        *done = download_done;
    }
    if (total != NULL) {
        *total = installer.size;
    }
    if (why != NULL && size > 0) {
        SDL_strlcpy(why, download_why, size);
    }
    SDL_UnlockMutex(lock);
    return state;
}

void khdays_updates_download_cancel(void)
{
    SDL_SetAtomicInt(&download_cancel, 1);
}

int khdays_updates_install(char *why, size_t size)
{
    char file[sizeof(download_file)];
    int ready, ok;
    SDL_LockMutex(lock);
    ready = download_state == KHDAYS_DOWNLOAD_READY;
    SDL_strlcpy(file, download_file, sizeof(file));
    SDL_UnlockMutex(lock);
    if (!ready) {
        SDL_strlcpy(why, "the installer is not downloaded", size);
        return 0;
    }
    ok = khdays_update_install_run(file, why, size);
    if (ok) {
        khdays_log("updates: %s runs to update this copy; the game closes", file);
    } else {
        khdays_log("updates: %s did not start: %s", file, why);
    }
    return ok;
}

void khdays_updates_answer(int answer)
{
    char page[TAG + 64] = "";
    if (!looking || told) {
        return;
    }
    told = 1;
    SDL_LockMutex(lock);
    if (answer == KHDAYS_UPDATE_DOWNLOAD) {
        SDL_snprintf(page, sizeof(page), "https://github.com/" REPOSITORY "/releases/tag/" TAG_PREFIX "%s", latest);
    } else if (answer == KHDAYS_UPDATE_SKIP) {
        SDL_strlcpy(skip, latest, sizeof(skip));
        write_state();
    }
    SDL_UnlockMutex(lock);
    khdays_log("updates: the player chose %s", answer == KHDAYS_UPDATE_DOWNLOAD  ? "its page"
                                               : answer == KHDAYS_UPDATE_INSTALL ? "to install it"
                                               : answer == KHDAYS_UPDATE_SKIP    ? "not to be told of it again"
                                                                                 : "later");
    if (page[0] != '\0' && !SDL_OpenURL(page)) {
        khdays_log("updates: the release's page could not be opened (%s)", SDL_GetError());
    }
}
