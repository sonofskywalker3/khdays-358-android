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
    name = slash != NULL && slash[1] != '\0' ? slash + 1 : name;
    if (strlen(name) >= KHDAYS_MEMORY_PART_NAME) {
        name[KHDAYS_MEMORY_PART_NAME - 1] = '\0';
    }
    return name;
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
        m->current = part_named(m, name);
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
