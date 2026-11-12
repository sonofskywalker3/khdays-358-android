/* memory_parts.h */
#include "memory_parts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSS_FIELD "Rss:"

void khdays_memory_parts_begin(KhdaysMemoryParts *m)
{
    memset(m, 0, sizeof(*m));
    m->current = -1;
}

/* The files whose names say nothing apart (the system's code and fonts), by their extensions. */
static const char *const families[] = {".oat", ".art", ".vdex", ".odex", ".jar", ".apk", ".dex",
                                       ".ttf", ".otf", ".ttc",  ".hyb",  ".dat", ".prop"};
#define DALVIK_PREFIX "[anon:dalvik-"
#define DELETED_SUFFIX " (deleted)"

/* `name` as it is added up: its family's name (written into `room`) when it is of one. */
static const char *family_of(const char *name, char *room, size_t size)
{
    const char *dot;
    if (strncmp(name, DALVIK_PREFIX, sizeof(DALVIK_PREFIX) - 1) == 0) {
        return KHDAYS_MEMORY_PARTS_DALVIK;
    }
    if (name[0] == '[' || name[0] == '(') {
        return name;
    }
    dot = strrchr(name, '.');
    for (size_t i = 0; dot != NULL && i < sizeof(families) / sizeof(families[0]); ++i) {
        const size_t length = strlen(families[i]);
        /* (the extension, then the name's end or " (deleted)") */
        if (strncmp(dot, families[i], length) == 0 &&
            (dot[length] == '\0' || strcmp(dot + length, DELETED_SUFFIX) == 0)) {
            snprintf(room, size, "(%s files)", families[i]);
            return room;
        }
    }
    return name;
}

/* The part named `name`, made if there is room, else the last one (the others'). */
static int part_named(KhdaysMemoryParts *m, const char *name)
{
    for (int i = 0; i < m->count; ++i) {
        if (strcmp(m->parts[i].name, name) == 0) {
            return i;
        }
    }
    if (m->count == KHDAYS_MEMORY_PARTS - 1) {
        name = KHDAYS_MEMORY_PARTS_OTHERS;
    } else if (m->count == KHDAYS_MEMORY_PARTS) {
        return KHDAYS_MEMORY_PARTS - 1;
    }
    snprintf(m->parts[m->count].name, sizeof(m->parts[m->count].name), "%s", name);
    return m->count++;
}

/* A mapping's first line ("start-end perms offset dev inode   name"): its name, a file's without its folders,
 * or NULL when the line is not one. */
static const char *mapping_name(char *line)
{
    unsigned long long start, end, offset, inode;
    unsigned major, minor;
    char perms[8];
    int at = 0;
    char *name, *slash;
    size_t length;
    if (sscanf(line, "%llx-%llx %7s %llx %x:%x %llu %n", &start, &end, perms, &offset, &major, &minor, &inode, &at) <
            7 ||
        at == 0) {
        return NULL;
    }
    name = line + at;
    length = strlen(name);
    while (length > 0 && (name[length - 1] == ' ' || name[length - 1] == '\r')) {
        name[--length] = '\0';
    }
    if (length == 0) {
        return KHDAYS_MEMORY_PARTS_UNNAMED;
    }
    slash = name[0] == '/' ? strrchr(name, '/') : NULL;
    return slash != NULL && slash[1] != '\0' ? slash + 1 : name;
}

static void take_line(KhdaysMemoryParts *m)
{
    const char *name;
    if (strncmp(m->line, RSS_FIELD, sizeof(RSS_FIELD) - 1) == 0) {
        if (m->current >= 0) {
            m->parts[m->current].held_kb += strtoull(m->line + sizeof(RSS_FIELD) - 1, NULL, 10);
        }
        return;
    }
    /* a field's line starts with a capital; a mapping's with its address */
    if (m->line[0] >= 'A' && m->line[0] <= 'Z') {
        return;
    }
    name = mapping_name(m->line);
    if (name != NULL && m->too_long) {
        name = KHDAYS_MEMORY_PARTS_LONG; /* its file's name is past the line's room: not named by a folder's */
    }
    if (name != NULL) {
        char family[24], cut[KHDAYS_MEMORY_PART_NAME];
        /* (its family from the whole name, then cut to a part's room: the names are compared as kept) */
        snprintf(cut, sizeof(cut), "%s", family_of(name, family, sizeof(family)));
        m->current = part_named(m, cut);
        ++m->parts[m->current].mappings;
    }
}

void khdays_memory_parts_take(KhdaysMemoryParts *m, const char *bytes, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        if (bytes[i] == '\n') {
            m->line[m->length] = '\0';
            take_line(m);
            m->length = 0;
            m->too_long = 0;
        } else if (m->length + 1 < sizeof(m->line)) {
            m->line[m->length++] = bytes[i];
        } else {
            m->too_long = 1;
        }
    }
}

static int most_first(const void *a, const void *b)
{
    const KhdaysMemoryPart *x = (const KhdaysMemoryPart *)a, *y = (const KhdaysMemoryPart *)b;
    return x->held_kb > y->held_kb ? -1 : x->held_kb < y->held_kb;
}

void khdays_memory_parts_end(KhdaysMemoryParts *m)
{
    qsort(m->parts, (size_t)m->count, sizeof(m->parts[0]), most_first);
    m->current = -1;
}
