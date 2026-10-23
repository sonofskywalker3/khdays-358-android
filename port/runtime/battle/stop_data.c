/* The Stop magic's data (stop_data.h): stop_magic.txt read line by line. */
#include "stop_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const khdays_stop_languages[KHDAYS_STOP_LANGUAGES] = {"en", "es", "fr", "de", "it"};

/* The line's next word (spaces between), or NULL at its end. */
static const char *next_word(const char **at, const char *end, int *length)
{
    const char *p = *at;
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    if (p >= end) {
        *at = p;
        return NULL;
    }
    const char *start = p;
    while (p < end && *p != ' ' && *p != '\t') {
        ++p;
    }
    *at = p;
    *length = (int)(p - start);
    return start;
}

static int word_int(const char *word, int length, int *out)
{
    char buffer[24];
    char *stop;
    if (word == NULL || length <= 0 || length >= (int)sizeof(buffer)) {
        return 0;
    }
    memcpy(buffer, word, (size_t)length);
    buffer[length] = '\0';
    const long value = strtol(buffer, &stop, 0);
    *out = (int)value;
    return *stop == '\0';
}

/* The rest of the line as the game's words: `\n` a line break, `[...]` highlighted (\x02 ... \x01). */
static char *game_words(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    char *out = calloc((size_t)(end - p) + 1, 1);
    char *o = out;
    if (out == NULL) {
        return NULL;
    }
    for (; p < end; ++p) {
        if (*p == '\\' && p + 1 < end && p[1] == 'n') {
            *o++ = '\n';
            ++p;
        } else if (*p == '[') {
            *o++ = '\x02';
        } else if (*p == ']') {
            *o++ = '\x01';
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
    return out;
}

/* The line's next word as a number: 0 when there is none or it is not one. */
static int next_int(const char **at, const char *end, int *out)
{
    int length;
    const char *word = next_word(at, end, &length);
    return word_int(word, length, out);
}

/* The line's next word as a level, 1 to KHDAYS_STOP_LEVELS. */
static int next_level(const char **at, const char *end, int *level)
{
    return next_int(at, end, level) && *level >= 1 && *level <= KHDAYS_STOP_LEVELS;
}

static int language_of(const char *word, int length)
{
    for (int i = 0; word != NULL && i < KHDAYS_STOP_LANGUAGES; ++i) {
        if (length == 2 && memcmp(word, khdays_stop_languages[i], 2) == 0) {
            return i;
        }
    }
    return -1;
}

static int read_level(const char *p, const char *end, KhdaysStopData *d)
{
    int n, level;
    const char *w;
    if (!next_level(&p, end, &level)) {
        return 0;
    }
    KhdaysStopLevel *l = &d->levels[level - 1];
    char reach[24];
    if (!next_int(&p, end, &l->id) || !next_int(&p, end, &l->ticks) || (w = next_word(&p, end, &n)) == NULL ||
        n >= (int)sizeof(reach) || !next_int(&p, end, &l->price)) {
        return 0;
    }
    memcpy(reach, w, (size_t)n);
    reach[n] = '\0';
    l->reach = atof(reach);
    l->set = l->id > 0 && l->ticks > 0 && l->reach > 0.0;
    return l->set;
}

static int read_recipe(const char *p, const char *end, KhdaysStopData *d)
{
    int level, length;
    const char *w;
    if (!next_level(&p, end, &level)) {
        return 0;
    }
    KhdaysStopRecipe *r = &d->recipes[level - 1];
    memset(r, 0, sizeof(*r));
    if (!next_int(&p, end, &r->munny) || r->munny < 0) {
        return 0;
    }
    while ((w = next_word(&p, end, &length)) != NULL) {
        const char *x = memchr(w, 'x', (size_t)length);
        if (x == NULL || r->material_count == KHDAYS_STOP_INGREDIENTS) {
            return 0;
        }
        KhdaysStopMaterial *m = &r->materials[r->material_count++];
        if (!word_int(w, (int)(x - w), &m->id) || !word_int(x + 1, length - (int)(x - w) - 1, &m->count) ||
            m->id <= 0 || m->count <= 0) {
            return 0;
        }
    }
    r->set = r->material_count > 0;
    return r->set;
}

static int read_words(const char *key, int key_length, const char *p, const char *end, KhdaysStopData *d)
{
    int level, length;
    const char *word = next_word(&p, end, &length);
    const int language = language_of(word, length);
    if (language < 0 || !next_level(&p, end, &level)) {
        return 0;
    }
    KhdaysStopWords *w = &d->words[language][level - 1];
    char **slot = key_length == 4 && memcmp(key, "name", 4) == 0    ? &w->name
                  : key_length == 5 && memcmp(key, "panel", 5) == 0 ? &w->panel
                                                                    : &w->magic;
    free(*slot);
    *slot = game_words(p, end);
    return *slot != NULL && (*slot)[0] != '\0';
}

int khdays_stop_data_read(const char *text, unsigned size, KhdaysStopData *out, char *why, int why_size)
{
    const char *p = text, *end = text + size;
    int line = 0;
    memset(out, 0, sizeof(*out));
    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *stop = eol != NULL ? eol : end;
        const char *hash = memchr(p, '#', (size_t)(stop - p));
        const char *at = p;
        int length, ok = 1;
        ++line;
        if (hash != NULL) {
            stop = hash;
        }
        while (stop > at && (stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t')) {
            --stop;
        }
        const char *key = next_word(&at, stop, &length);
        if (key == NULL) {
            /* an empty line or a comment */
        } else if (length == 5 && memcmp(key, "level", 5) == 0) {
            ok = read_level(at, stop, out);
        } else if (length == 8 && memcmp(key, "magic_lv", 8) == 0) {
            ok = next_int(&at, stop, &out->magic_lv) && out->magic_lv >= 0;
        } else if (length == 6 && memcmp(key, "recipe", 6) == 0) {
            ok = read_recipe(at, stop, out);
        } else if ((length == 4 && memcmp(key, "name", 4) == 0) || (length == 5 && memcmp(key, "panel", 5) == 0) ||
                   (length == 5 && memcmp(key, "magic", 5) == 0)) {
            ok = read_words(key, length, at, stop, out);
        } else {
            ok = 0;
        }
        if (!ok) {
            snprintf(why, (size_t)why_size, "line %d: %.*s", line, (int)(stop - p), p);
            return line;
        }
        p = eol != NULL ? eol + 1 : end;
    }
    return 0;
}

int khdays_stop_data_panels_whole(const KhdaysStopData *d)
{
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        if (!d->levels[l].set) {
            return 0;
        }
        for (int g = 0; g < KHDAYS_STOP_LANGUAGES; ++g) {
            const KhdaysStopWords *w = &d->words[g][l];
            if (w->name == NULL || w->panel == NULL || w->magic == NULL) {
                return 0;
            }
        }
    }
    return 1;
}

int khdays_stop_data_shop_whole(const KhdaysStopData *d)
{
    for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
        if (!d->recipes[l].set) {
            return 0;
        }
    }
    return khdays_stop_data_panels_whole(d);
}

static void free_words(KhdaysStopWords *w)
{
    free(w->name);
    free(w->panel);
    free(w->magic);
}

void khdays_stop_data_free(KhdaysStopData *d)
{
    for (int g = 0; g < KHDAYS_STOP_LANGUAGES; ++g) {
        for (int l = 0; l < KHDAYS_STOP_LEVELS; ++l) {
            free_words(&d->words[g][l]);
        }
    }
    memset(d, 0, sizeof(*d));
}
