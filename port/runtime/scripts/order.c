/* The mods' load order (order.h). */
#include "order.h"

#include <stdlib.h>
#include <string.h>

static char *copy_span(const char *s, size_t n)
{
    char *out = malloc(n + 1);
    if (out != NULL) {
        memcpy(out, s, n);
        out[n] = 0;
    }
    return out;
}

static int add(KhdaysOrder *o, const char *folder, size_t n, int on)
{
    if (o->count == o->capacity) {
        const int grown = o->capacity == 0 ? 8 : o->capacity * 2;
        KhdaysOrderEntry *e = realloc(o->entries, (size_t)grown * sizeof(*e));
        if (e == NULL) {
            return 0;
        }
        o->entries = e;
        o->capacity = grown;
    }
    o->entries[o->count].folder = copy_span(folder, n);
    if (o->entries[o->count].folder == NULL) {
        return 0;
    }
    o->entries[o->count].on = on;
    o->entries[o->count].present = 0;
    ++o->count;
    return 1;
}

static int find(const KhdaysOrder *o, const char *folder)
{
    for (int i = 0; i < o->count; ++i) {
        if (strcmp(o->entries[i].folder, folder) == 0) {
            return i;
        }
    }
    return -1;
}

void khdays_order_parse(KhdaysOrder *o, const char *text)
{
    memset(o, 0, sizeof(*o));
    for (const char *p = text; p != NULL && *p;) {
        const char *eol = strchr(p, '\n'), *line = p, *eq = NULL, *k1, *v0, *v1;
        if (eol == NULL) {
            eol = p + strlen(p);
        }
        p = *eol ? eol + 1 : eol;
        for (const char *q = line; q < eol; ++q) {
            if (*q == '=') {
                eq = q; /* the last one: a folder's name may hold '=' */
            }
        }
        while (line < eol && (*line == ' ' || *line == '\t')) {
            ++line;
        }
        if (eq == NULL || *line == '#') {
            continue;
        }
        k1 = eq;
        while (k1 > line && (k1[-1] == ' ' || k1[-1] == '\t')) {
            --k1;
        }
        v0 = eq + 1, v1 = eol;
        while (v0 < v1 && (*v0 == ' ' || *v0 == '\t')) {
            ++v0;
        }
        while (v1 > v0 && (v1[-1] == ' ' || v1[-1] == '\t' || v1[-1] == '\r')) {
            --v1;
        }
        if (k1 > line) {
            char *folder = copy_span(line, (size_t)(k1 - line));
            if (folder != NULL && find(o, folder) < 0) {
                add(o, line, (size_t)(k1 - line), !((size_t)(v1 - v0) == 3 && memcmp(v0, "off", 3) == 0));
            }
            free(folder);
        }
    }
}

void khdays_order_merge(KhdaysOrder *o, const char *const *found, int count)
{
    for (int i = 0; i < count; ++i) {
        int at = find(o, found[i]);
        if (at < 0 && add(o, found[i], strlen(found[i]), 1)) {
            at = o->count - 1;
        }
        if (at >= 0) {
            o->entries[at].present = 1;
        }
    }
}

void khdays_order_move(KhdaysOrder *o, int index, int by)
{
    int other = index;
    if (index < 0 || index >= o->count || !o->entries[index].present) {
        return;
    }
    do {
        other += by;
    } while (other >= 0 && other < o->count && !o->entries[other].present);
    if (other >= 0 && other < o->count) {
        const KhdaysOrderEntry t = o->entries[index];
        o->entries[index] = o->entries[other];
        o->entries[other] = t;
    }
}

char *khdays_order_text(const KhdaysOrder *o)
{
    size_t size = 1, at = 0;
    char *text;
    for (int i = 0; i < o->count; ++i) {
        size += strlen(o->entries[i].folder) + 7;
    }
    text = malloc(size);
    if (text == NULL) {
        return NULL;
    }
    for (int pass = 1; pass >= 0; --pass) {
        for (int i = 0; i < o->count; ++i) {
            const KhdaysOrderEntry *e = &o->entries[i];
            const size_t n = strlen(e->folder);
            if (e->present != pass) {
                continue;
            }
            memcpy(text + at, e->folder, n);
            at += n;
            memcpy(text + at, e->on ? " = on\n" : " = off\n", e->on ? 6 : 7);
            at += e->on ? 6 : 7;
        }
    }
    text[at] = 0;
    return text;
}

void khdays_order_free(KhdaysOrder *o)
{
    for (int i = 0; i < o->count; ++i) {
#pragma warning(suppress : 6001) /* every entry below the count was written before the count moved */
        free(o->entries[i].folder);
    }
    free(o->entries);
    memset(o, 0, sizeof(*o));
}
