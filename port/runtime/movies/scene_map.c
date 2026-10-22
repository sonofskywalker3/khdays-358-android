/* The scene map's text (scene_map.h): a line per scene,
 *   <script> <stem> [<stem> ...] ; <evidence>   <script> off ; <evidence>   <script> skip <stem> ; <evidence>
 * or per movie, mv#<movie> <stem> ... ; <evidence> (or off). A scene's or a movie's stem may be a part
 * of the video, <stem>[<from>:<until>] in seconds, either left out.
 * '#' comments, blank lines skipped. A script is EV_<world>#<index>, mi/<mission>/<script>:<section>
 * or mnl#<record> (a day's monologue, ui/mnl's record);
 * a stem is a video's file name without .mp4. */
#include "scene_map.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static char *copy_of(const char *s, size_t n)
{
    char *out = (char *)malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n);
        out[n] = '\0';
    }
    return out;
}

static void entry_free(KhdaysSceneEntry *e)
{
    for (int i = 0; i < e->stem_count; ++i) {
        free(e->stems[i]);
    }
    free(e->stems);
    free(e->from);
    free(e->until);
    free(e->key);
    free(e->evidence);
    memset(e, 0, sizeof(*e));
}

/* Digits only, at least one. */
static int digits(const char *p)
{
    if (*p == '\0') {
        return 0;
    }
    for (; *p != '\0'; ++p) {
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
    }
    return 1;
}

/* EV_<world>#<index>, mi/<mission>/<script>:<section> (letters, digits and _ in the names),
 * mnl#<record> or mv#<movie> */
static int key_valid(const char *k)
{
    if (strncmp(k, "mv#", 3) == 0) {
        return digits(k + 3);
    }
    if (strncmp(k, "EV_", 3) == 0) {
        const char *hash = strchr(k, '#');
        if (hash == NULL || hash == k + 3) {
            return 0;
        }
        for (const char *p = k + 3; p < hash; ++p) {
            if (!isalnum((unsigned char)*p) && *p != '_') {
                return 0;
            }
        }
        return digits(hash + 1);
    }
    if (strncmp(k, "mnl#", 4) == 0) {
        return digits(k + 4);
    }
    if (strncmp(k, "mi/", 3) == 0) {
        const char *slash = strchr(k + 3, '/');
        const char *colon = slash != NULL ? strchr(slash + 1, ':') : NULL;
        return slash != NULL && slash > k + 3 && colon != NULL && colon > slash + 1 && colon[1] != '\0';
    }
    return 0;
}

/* A video's part, "[<from>:<until>]" in seconds of it (either left out: its own start or end), at
 * `open`. Returns 0 when it is not one. */
static int read_part(const char *open, double *from, double *until)
{
    const char *colon = strchr(open, ':');
    const size_t n = strlen(open);
    char *end;
    if (colon == NULL || n < 3 || open[n - 1] != ']') {
        return 0;
    }
    *from = *until = 0.0;
    if (colon > open + 1) {
        *from = strtod(open + 1, &end);
        if (end != colon || !(*from >= 0.0)) {
            return 0;
        }
    }
    if (colon + 1 < open + n - 1) {
        *until = strtod(colon + 1, &end);
        if (end != open + n - 1 || !(*until > *from)) {
            return 0;
        }
    }
    return 1;
}

static int stem_valid(const char *s)
{
    if (*s == '\0') {
        return 0;
    }
    for (; *s != '\0'; ++s) {
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') {
            return 0;
        }
    }
    return 1;
}

/* Splits `line` (its own copy) into words at spaces and tabs. Returns the words' count; grows. */
static int words_of(char *line, char ***words)
{
    int count = 0, capacity = 0;
    char *p = line;
    *words = NULL;
    for (;;) {
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (*p == '\0') {
            return count;
        }
        if (count == capacity) {
            char **grown;
            capacity = capacity ? capacity * 2 : 8;
            grown = (char **)realloc(*words, (size_t)capacity * sizeof(char *));
            if (grown == NULL) {
                return count;
            }
            *words = grown;
        }
        (*words)[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
}

static void put(KhdaysSceneMap *map, KhdaysSceneEntry *e)
{
    for (int i = 0; i < map->count; ++i) {
        if (strcmp(map->entries[i].key, e->key) == 0) {
            entry_free(&map->entries[i]);
            map->entries[i] = *e;
            return;
        }
    }
    if (map->count == map->capacity) {
        const int capacity = map->capacity ? map->capacity * 2 : 64;
        KhdaysSceneEntry *grown = (KhdaysSceneEntry *)realloc(map->entries, (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            entry_free(e);
            return;
        }
        map->entries = grown;
        map->capacity = capacity;
    }
    map->entries[map->count++] = *e;
}

/* One line (without its end): 1 an entry put, 0 nothing to read, -1 malformed (`why` set). */
static int read_line(KhdaysSceneMap *map, const char *start, size_t length, const char **why)
{
    char *line = copy_of(start, length);
    char *evidence, *hash;
    char **words = NULL;
    int count, result = 0;
    KhdaysSceneEntry e;
    if (line == NULL) {
        *why = "out of memory";
        return -1;
    }
    /* a comment from a '#' at a line's start or after a space (EV_S#4 keeps its own) */
    for (hash = line; (hash = strchr(hash, '#')) != NULL; ++hash) {
        if (hash == line || hash[-1] == ' ' || hash[-1] == '\t') {
            *hash = '\0';
            break;
        }
    }
    evidence = strchr(line, ';');
    if (evidence != NULL) {
        *evidence++ = '\0';
        while (*evidence == ' ' || *evidence == '\t') {
            ++evidence;
        }
    }
    count = words_of(line, &words);
    memset(&e, 0, sizeof(e));
    if (count == 0) {
        result = 0;
    } else if (!key_valid(words[0])) {
        *why = "not a scene's script (EV_<world>#<index>, mi/<mission>/<script>:<section> or mnl#<record>) "
               "nor a movie (mv#<movie>)";
        result = -1;
    } else if (count == 1) {
        *why = "no video and no `off`";
        result = -1;
    } else if (count == 2 && strcmp(words[1], "off") == 0) {
        e.key = copy_of(words[0], strlen(words[0]));
        e.off = 1;
        result = 1;
    } else if (strcmp(words[1], "skip") == 0) {
        if (count != 3 || strncmp(words[0], "mv#", 3) == 0 || !stem_valid(words[2])) {
            *why = "`skip` takes the HD movie's video that shows the scene (a scene's, not a movie's)";
            result = -1;
        } else {
            e.key = copy_of(words[0], strlen(words[0]));
            e.skip = 1;
            e.stems = (char **)calloc(1, sizeof(char *));
            if (e.stems != NULL) {
                e.stems[e.stem_count++] = copy_of(words[2], strlen(words[2]));
            }
            result = 1;
        }
    } else {
        const int monologue = strncmp(words[0], "mnl#", 4) == 0;
        double *from = (double *)calloc((size_t)(count - 1), sizeof(double));
        double *until = (double *)calloc((size_t)(count - 1), sizeof(double));
        int parts = 0;
        *why = NULL;
        for (int i = 1; i < count && *why == NULL && from != NULL && until != NULL; ++i) {
            char *open = strchr(words[i], '[');
            if (open != NULL) {
                if (monologue || !read_part(open, &from[i - 1], &until[i - 1])) {
                    *why = "a part of a video is [<from>:<until>] in seconds, a scene's or a movie's (not a "
                           "monologue's)";
                }
                *open = '\0';
                parts = 1;
            }
            if (*why == NULL &&
                (!stem_valid(words[i]) || strcmp(words[i], "off") == 0 || strcmp(words[i], "skip") == 0)) {
                *why = "a video's name is not a file's name (letters, digits, _ and -)";
            }
        }
        if (from == NULL || until == NULL) {
            *why = "out of memory";
        }
        if (*why != NULL) {
            free(from);
            free(until);
            result = -1;
        } else {
            e.key = copy_of(words[0], strlen(words[0]));
            e.stems = (char **)calloc((size_t)(count - 1), sizeof(char *));
            if (e.stems != NULL) {
                for (int i = 1; i < count; ++i) {
                    e.stems[e.stem_count++] = copy_of(words[i], strlen(words[i]));
                }
            }
            if (parts) {
                e.from = from;
                e.until = until;
            } else {
                free(from);
                free(until);
            }
            result = 1;
        }
    }
    if (result == 1) {
        size_t n = evidence != NULL ? strlen(evidence) : 0;
        while (n > 0 && (evidence[n - 1] == ' ' || evidence[n - 1] == '\t')) {
            --n;
        }
        e.evidence = copy_of(evidence != NULL ? evidence : "", n);
        put(map, &e);
    }
    free(words);
    free(line);
    return result;
}

int khdays_scene_map_read(KhdaysSceneMap *map, const char *text, size_t size, const char *file,
                          KhdaysSceneReport report)
{
    size_t at = 0;
    int line = 0, read = 0;
    while (at < size) {
        size_t end = at;
        const char *why = NULL;
        int r;
        while (end < size && text[end] != '\n') {
            ++end;
        }
        ++line;
        r = read_line(map, text + at, (end > at && text[end - 1] == '\r') ? end - at - 1 : end - at, &why);
        if (r > 0) {
            ++read;
        } else if (r < 0 && report != NULL) {
            report(file, line, why);
        }
        at = end + 1;
    }
    return read;
}

const KhdaysSceneEntry *khdays_scene_map_find(const KhdaysSceneMap *map, const char *key)
{
    for (int i = 0; i < map->count; ++i) {
        if (strcmp(map->entries[i].key, key) == 0) {
            return &map->entries[i];
        }
    }
    return NULL;
}

void khdays_scene_map_free(KhdaysSceneMap *map)
{
    for (int i = 0; i < map->count; ++i) {
        entry_free(&map->entries[i]);
    }
    free(map->entries);
    memset(map, 0, sizeof(*map));
}
