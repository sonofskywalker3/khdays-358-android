/* khdays-memory-parts-test: /proc/self/smaps added up by the mappings' names (runtime/core/memory_parts.c). */
#include "../runtime/core/memory_parts.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static const char SMAPS[] =
    "12c00000-12e00000 rw-p 00000000 00:00 0                                  [anon:scudo:primary]\n"
    "Size:               2048 kB\n"
    "Rss:                1000 kB\n"
    "Pss:                1000 kB\n"
    "VmFlags: rd wr mr mw me ac\n"
    "7a00000000-7a00020000 rw-s 00000000 00:10 33                             /dev/kgsl-3d0\n"
    "Rss:                 128 kB\n"
    "7a10000000-7a10004000 rw-p 00000000 00:00 0 \n"
    "Rss:                  16 kB\n"
    "7a20000000-7a20040000 rw-p 00000000 00:00 0                              [anon:scudo:primary]\n"
    "Rss:                 200 kB\n"
    "7a30000000-7a30100000 r-xp 00001000 fd:05 4711                           /data/app/~~x==/base.apk!/lib/libmain.so\n"
    "Rss:                 512 kB\n";

static const KhdaysMemoryPart *named(const KhdaysMemoryParts *m, const char *name)
{
    for (int i = 0; i < m->count; ++i) {
        if (strcmp(m->parts[i].name, name) == 0) {
            return &m->parts[i];
        }
    }
    return NULL;
}

int main(void)
{
    static KhdaysMemoryParts whole, cut, many;
    const KhdaysMemoryPart *p;
    /* added up by name, the most held first; a file by its name alone, a mapping without a name as such */
    khdays_memory_parts_begin(&whole);
    khdays_memory_parts_take(&whole, SMAPS, sizeof(SMAPS) - 1);
    khdays_memory_parts_end(&whole);
    CHECK(whole.count == 4);
    CHECK(strcmp(whole.parts[0].name, "[anon:scudo:primary]") == 0);
    CHECK(whole.parts[0].held_kb == 1200 && whole.parts[0].mappings == 2);
    CHECK(strcmp(whole.parts[1].name, "libmain.so") == 0 && whole.parts[1].held_kb == 512);
    p = named(&whole, "/dev/kgsl-3d0");
    CHECK(p == NULL); /* (a file: without its folders) */
    p = named(&whole, "kgsl-3d0");
    CHECK(p != NULL && p->held_kb == 128 && p->mappings == 1);
    p = named(&whole, KHDAYS_MEMORY_PARTS_UNNAMED);
    CHECK(p != NULL && p->held_kb == 16 && p->mappings == 1);

    /* the same given a few bytes at a time */
    khdays_memory_parts_begin(&cut);
    for (size_t at = 0; at < sizeof(SMAPS) - 1; at += 7) {
        const size_t rest = sizeof(SMAPS) - 1 - at;
        khdays_memory_parts_take(&cut, SMAPS + at, rest < 7 ? rest : 7);
    }
    khdays_memory_parts_end(&cut);
    CHECK(cut.count == whole.count);
    for (int i = 0; i < whole.count && i < cut.count; ++i) {
        CHECK(strcmp(cut.parts[i].name, whole.parts[i].name) == 0 && cut.parts[i].held_kb == whole.parts[i].held_kb &&
              cut.parts[i].mappings == whole.parts[i].mappings);
    }

    /* more names than there is room for: the ones past it added up as the others */
    khdays_memory_parts_begin(&many);
    for (int i = 0; i < KHDAYS_MEMORY_PARTS + 10; ++i) {
        char text[160];
        const int n = snprintf(text, sizeof(text), "%x-%x rw-p 00000000 00:00 0    [anon:part%d]\nRss:    4 kB\n",
                               0x1000 * (i + 1), 0x1000 * (i + 2), i);
        khdays_memory_parts_take(&many, text, (size_t)n);
    }
    khdays_memory_parts_end(&many);
    CHECK(many.count == KHDAYS_MEMORY_PARTS);
    p = named(&many, KHDAYS_MEMORY_PARTS_OTHERS);
    CHECK(p != NULL && p->mappings == 11 && p->held_kb == 44);

    /* a file's name with a space and "(deleted)" kept; a path past the line's room is not named by its folder */
    {
        static KhdaysMemoryParts odd;
        static char text[700];
        size_t at;
        khdays_memory_parts_begin(&odd);
        at = (size_t)snprintf(text, sizeof(text),
                              "1000-2000 rw-p 00000000 fd:05 7    /data/a b/my file.bin (deleted)\n"
                              "Rss:    8 kB\n2000-3000 r--p 00000000 fd:05 8    /");
        memset(text + at, 'd', 400);
        at += 400;
        at += (size_t)snprintf(text + at, sizeof(text) - at, "/x/name.so\nRss:   12 kB\n");
        khdays_memory_parts_take(&odd, text, at);
        khdays_memory_parts_end(&odd);
        p = named(&odd, "my file.bin (deleted)");
        CHECK(p != NULL && p->held_kb == 8);
        p = named(&odd, KHDAYS_MEMORY_PARTS_LONG);
        CHECK(p != NULL && p->held_kb == 12 && odd.count == 2);
    }
    return failures != 0;
}
