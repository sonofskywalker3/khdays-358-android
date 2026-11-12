/* What a program's memory is made of, from Linux's /proc/self/smaps: the memory held (Rss) and the count of
 * its mappings added up by the mappings' names, for the session log's memory line when the game's memory has
 * grown (session_log.c). An Android program maps hundreds of the system's files, so the families that say
 * nothing apart are added up as one: the Java runtime's own ("[anon:dalvik-...]" as "[anon:dalvik]") and the
 * files of KHDAYS_MEMORY_PARTS_FAMILIES' extensions ("(.oat files)"); libraries and devices keep their names. Nothing here allocates: it is read when there may be no memory left to give. */
#ifndef KHDAYS_RUNTIME_CORE_MEMORY_PARTS_H
#define KHDAYS_RUNTIME_CORE_MEMORY_PARTS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_MEMORY_PARTS 384    /* names kept apart; the ones past them are added up as the last */
#define KHDAYS_MEMORY_PART_NAME 48 /* a name's room: a file's name without its folders, cut at this */
#define KHDAYS_MEMORY_PARTS_UNNAMED "(unnamed)"
#define KHDAYS_MEMORY_PARTS_OTHERS "(others)"
#define KHDAYS_MEMORY_PARTS_DALVIK "[anon:dalvik]"
#define KHDAYS_MEMORY_PARTS_LONG "(long paths)" /* files whose path passes a line's room */

typedef struct KhdaysMemoryPart {
    char name[KHDAYS_MEMORY_PART_NAME];
    unsigned long long held_kb;
    unsigned mappings;
} KhdaysMemoryPart;

typedef struct KhdaysMemoryParts {
    KhdaysMemoryPart parts[KHDAYS_MEMORY_PARTS];
    int count;
    int current;    /* the part of the mapping being read, -1 before the first */
    char line[320]; /* the line being read, over two calls when it is cut between them */
    size_t length;
    int too_long; /* the line passed the room: its rest is skipped */
} KhdaysMemoryParts;

void khdays_memory_parts_begin(KhdaysMemoryParts *m);

/* The next `size` bytes of smaps, cut anywhere. */
void khdays_memory_parts_take(KhdaysMemoryParts *m, const char *bytes, size_t size);

/* After the last bytes: the parts put in order, the one holding most first. */
void khdays_memory_parts_end(KhdaysMemoryParts *m);

#ifdef __cplusplus
}
#endif

#endif
