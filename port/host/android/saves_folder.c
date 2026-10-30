/* saves_folder.h. The files are named whole: SDL opens a relative path from the app's private folder. */
#include "saves_folder.h"

#include "../app/files.h"

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SAVES_KEPT_IN "saves_folder.txt"   /* the folder chosen (MediaFolder.SAVES_KEPT_IN) */
#define SAVES_MOVED_IN "saves_moved.txt"   /* the folder the files were last moved to, every copy checked */
#define SAVES_MOVING_IN "saves_moving.txt" /* a move under way: the files in that folder are a copy of ours */
#define PORT_FILES 99                      /* slots/slot01.sav .. slot99.sav (runtime/menus/saveslots.h) */
#define CHIP_FILE "khdays.sav"
#define KEPT_FOLDER "kept" /* in the app's saves folder: its own files of before a move back */

enum { ABSENT, PRESENT, UNREADABLE }; /* what a path is (file_state) */

/* `name` in the app's private storage, into `path`; 0 when there is none. */
static int private_path(const char *name, char *path, size_t size)
{
    const char *inside = SDL_GetAndroidInternalStoragePath();
    return inside != NULL && snprintf(path, size, "%s/%s", inside, name) < (int)size;
}

/* The one line of a private file into `line`; "" when there is none (or it is longer than `size`:
 * never a folder cut short). */
static void read_line(const char *name, char *line, size_t size)
{
    char path[1024];
    FILE *f;
    line[0] = '\0';
    if (private_path(name, path, sizeof(path)) && (f = fopen(path, "r")) != NULL) {
        if (fgets(line, (int)size, f) == NULL) {
            line[0] = '\0';
        } else if (strchr(line, '\n') == NULL && !feof(f)) {
            fprintf(stderr, "khdays-native: saves: %s names too long a folder; not used\n", path);
            line[0] = '\0';
        }
        line[strcspn(line, "\r\n")] = '\0';
        fclose(f);
    }
}

/* A private file holding the one line `line`, or none for "". 1 when it is so. */
static int write_line(const char *name, const char *line)
{
    char path[1024], text[1100];
    if (!private_path(name, path, sizeof(path))) {
        return 0;
    }
    if (line[0] == '\0') {
        if (unlink(path) != 0 && errno != ENOENT) {
            fprintf(stderr, "khdays-native: saves: %s cannot be removed (%s)\n", path, strerror(errno));
            return 0;
        }
        return 1;
    }
    snprintf(text, sizeof(text), "%s\n", line);
    if (!khdays_host_write_file_atomic(path, text, strlen(text))) {
        fprintf(stderr, "khdays-native: saves: %s cannot be written\n", path);
        return 0;
    }
    return 1;
}

/* Whether `path` is a file: ABSENT only when it is surely not there, UNREADABLE when it cannot be told. */
static int file_state(const char *path, struct stat *st)
{
    if (stat(path, st) == 0) {
        return S_ISREG(st->st_mode) ? PRESENT : UNREADABLE;
    }
    return errno == ENOENT || errno == ENOTDIR ? ABSENT : UNREADABLE;
}

static int is_folder(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Whether a file can be made in `folder` now (the access to all files given, the memory card in). */
static int writable(const char *folder)
{
    char probe[1100];
    FILE *f;
    if (!is_folder(folder)) {
        return 0;
    }
    snprintf(probe, sizeof(probe), "%s/.khdays-probe", folder);
    f = fopen(probe, "wb");
    if (f == NULL) {
        return 0;
    }
    fclose(f);
    unlink(probe);
    return 1;
}

/* The save file `index` of a folder, relative to it: the port's files first, the chip's last. 0 past
 * the last. */
static int file_name(int index, char *name, size_t size)
{
    if (index < PORT_FILES) {
        snprintf(name, size, "slots/slot%02d.sav", index + 1);
        return 1;
    }
    if (index == PORT_FILES) {
        snprintf(name, size, "%s", CHIP_FILE);
        return 1;
    }
    return 0;
}

/* What save files `folder` holds: ABSENT none, PRESENT some, UNREADABLE when one cannot be told. */
static int saves_state(const char *folder)
{
    char name[64], path[1200];
    struct stat st;
    int found = ABSENT;
    for (int i = 0; file_name(i, name, sizeof(name)); ++i) {
        snprintf(path, sizeof(path), "%s/%s", folder, name);
        switch (file_state(path, &st)) {
        case PRESENT: found = PRESENT; break;
        case UNREADABLE: return UNREADABLE;
        default: break;
        }
    }
    return found;
}

/* `from` copied to `to`, then read back and compared. 1 when the copy is the same (or there is surely
 * no `from`: nothing to copy), 0 when not (said). */
static int copy_file(const char *from, const char *to)
{
    struct stat st;
    unsigned char *data, *back;
    long got;
    int same;
    const int state = file_state(from, &st);
    if (state == ABSENT) {
        return 1;
    }
    if (state == UNREADABLE) {
        fprintf(stderr, "khdays-native: saves: %s cannot be read (%s)\n", from, strerror(errno));
        return 0;
    }
    data = (unsigned char *)malloc((size_t)st.st_size + 1);
    back = (unsigned char *)malloc((size_t)st.st_size + 1);
    if (data == NULL || back == NULL) {
        free(data);
        free(back);
        return 0;
    }
    got = khdays_host_read_file(from, data, (size_t)st.st_size + 1);
    same = got == (long)st.st_size && khdays_host_write_file_atomic(to, data, (size_t)got) &&
           khdays_host_read_file(to, back, (size_t)st.st_size + 1) == got && memcmp(data, back, (size_t)got) == 0;
    if (!same) {
        fprintf(stderr, "khdays-native: saves: %s could not be copied to %s\n", from, to);
    }
    free(data);
    free(back);
    return same;
}

/* Every save file of `from` copied into `to`, each checked. 1 when all are. */
static int copy_saves(const char *from, const char *to)
{
    char name[64], source[1200], target[1200];
    int ok = 1, count = 0;
    struct stat st;
    for (int i = 0; ok && file_name(i, name, sizeof(name)); ++i) {
        snprintf(source, sizeof(source), "%s/%s", from, name);
        snprintf(target, sizeof(target), "%s/%s", to, name);
        if (file_state(source, &st) != ABSENT) {
            ok = copy_file(source, target);
            count += ok;
        }
    }
    fprintf(stderr, "khdays-native: saves: %d files copied from %s to %s%s\n", count, from, to,
            ok ? ", each checked" : "; the copy stopped");
    return ok;
}

/* Into the chosen folder: from the folder the files were moved to before, else the app's own. The
 * folder they are in stays theirs until every copy is checked. */
static void move_to(const char *own, const char *chosen, const char *moved, char *folder, size_t size)
{
    char moving[1024];
    const char *from = moved[0] != '\0' ? moved : own;
    snprintf(folder, size, "%s", from);
    if (moved[0] != '\0' && !is_folder(moved)) {
        /* the files are in a folder that cannot be read now: not taken from the app's older ones */
        snprintf(folder, size, "%s", own);
        fprintf(stderr, "khdays-native: saves: %s cannot be read now; the move to %s waits for it\n", moved,
                chosen);
        return;
    }
    read_line(SAVES_MOVING_IN, moving, sizeof(moving));
    if (strcmp(moving, chosen) != 0) {
        const int there = saves_state(chosen);
        if (there == UNREADABLE) {
            fprintf(stderr, "khdays-native: saves: %s cannot be read whole; the saves stay in %s\n", chosen, from);
            return;
        }
        if (there == PRESENT) {
            /* saves there already (synced from another device): taken as they are, never written over */
            if (write_line(SAVES_MOVED_IN, chosen)) {
                snprintf(folder, size, "%s", chosen);
                fprintf(stderr, "khdays-native: saves: %s holds saves already; they are used, %s keeps its own\n",
                        chosen, from);
            }
            return;
        }
        if (!write_line(SAVES_MOVING_IN, chosen)) {
            return;
        }
    }
    /* (a move that stopped part way is made again: the files there are a copy of ours) */
    if (copy_saves(from, chosen) && write_line(SAVES_MOVED_IN, chosen)) {
        write_line(SAVES_MOVING_IN, "");
        snprintf(folder, size, "%s", chosen);
    }
}

/* Back to the app's folder: the files from the folder they were moved to, the app's own of before kept
 * aside first. Until every copy is checked the saves stay in the folder they were moved to. */
static void move_back(const char *own, const char *moved, char *folder, size_t size)
{
    char kept[512];
    const time_t now = time(NULL);
    struct tm at;
    const int had = saves_state(own);
    snprintf(folder, size, "%s", moved);
    if (had == UNREADABLE) {
        fprintf(stderr, "khdays-native: saves: %s cannot be read whole; the saves stay in %s\n", own, moved);
        return;
    }
    if (had == PRESENT) {
        localtime_r(&now, &at);
        snprintf(kept, sizeof(kept), "%s/%s/%04d%02d%02d-%02d%02d%02d", own, KEPT_FOLDER, at.tm_year + 1900,
                 at.tm_mon + 1, at.tm_mday, at.tm_hour, at.tm_min, at.tm_sec);
        if (!copy_saves(own, kept)) {
            return;
        }
    }
    if (copy_saves(moved, own) && write_line(SAVES_MOVED_IN, "")) {
        snprintf(folder, size, "%s", own);
        fprintf(stderr, "khdays-native: saves: back in %s from %s (which keeps its copy)\n", own, moved);
    }
}

void khdays_saves_folder_decide(const char *own, char *folder, size_t size)
{
    char chosen[1024], moved[1024];
    read_line(SAVES_KEPT_IN, chosen, sizeof(chosen));
    read_line(SAVES_MOVED_IN, moved, sizeof(moved));
    snprintf(folder, size, "%s", own);
    if (chosen[0] != '\0' && strcmp(chosen, moved) == 0) {
        if (!writable(chosen)) {
            /* a memory card taken out, the access to all files taken back: the app's folder meanwhile,
             * whose files stay as they are when the folder comes back */
            fprintf(stderr, "khdays-native: saves: %s cannot be written now; the app's folder %s meanwhile\n",
                    chosen, own);
            return;
        }
        snprintf(folder, size, "%s", chosen);
    } else if (chosen[0] != '\0') {
        if (!writable(chosen)) {
            snprintf(folder, size, "%s", moved[0] != '\0' && is_folder(moved) ? moved : own);
            fprintf(stderr, "khdays-native: saves: %s cannot be written now; the saves stay in %s\n", chosen,
                    folder);
            return;
        }
        move_to(own, chosen, moved, folder, size);
    } else if (moved[0] != '\0') {
        if (!is_folder(moved)) {
            fprintf(stderr, "khdays-native: saves: %s cannot be read now; the app's folder meanwhile, the saves "
                            "come back from it at a next start\n",
                    moved);
            return;
        }
        move_back(own, moved, folder, size);
    }
    fprintf(stderr, "khdays-native: saves: in %s\n", folder);
}
