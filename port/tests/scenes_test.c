/* The scene map's text (runtime/movies/scene_map.c): entries of the three key forms, comments, `off`,
 * malformed lines among good ones, an entry replacing one of the same key, lookups. */
#include "../runtime/movies/scene_map.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "scenes_test:%d: failed: %s\n", __LINE__, #c);                                             \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static int reported[16], report_count;

static void report(const char *file, int line, const char *why)
{
    (void)file;
    (void)why;
    if (report_count < 16) {
        reported[report_count++] = line;
    }
}

static size_t length(const char *s)
{
    return strlen(s);
}

int main(void)
{
    KhdaysSceneMap map = {0};
    const KhdaysSceneEntry *e;
    const char *port = "# the port's\n"
                       "\n"
                       "EV_S#196 hd002 ; pictures: the castle\n"
                       "EV_S#0 hd003\r\n"
                       "  EV_S#169   hd131a  hd132  ; lines, split # a comment\n"
                       "mi/0105/11:_e hd010 # a comment after a space\n"
                       "EV_S#3 off ; no theater scene\n"
                       "EVS#4 hd004\n"      /* line 8: not a script */
                       "EV_S#x hd004\n"     /* 9: not an index */
                       "EV_S#5\n"           /* 10: nothing to play */
                       "EV_S#6 hd/006\n"    /* 11: not a file's name */
                       "EV_S#7 hd007 off\n" /* 12: off among videos */
                       "mi/0105:_e hd010\n" /* 13: no script name */
                       "mnl#1 hd003 ; a monologue\n"
                       "mnl#a hd003\n"    /* 15: not a record */
                       "EV_TT#61 hd007c"; /* the last line without its end */
    const char *mod = "EV_S#0 off ; the mod keeps the DS's room\n"
                      "EV_S#4 hd004 hd004b\n";

    CHECK(khdays_scene_map_read(&map, port, length(port), "port", report) == 7);
    CHECK(map.count == 7);
    CHECK(report_count == 7);
    CHECK(report_count >= 7 && reported[0] == 8 && reported[1] == 9 && reported[2] == 10 && reported[3] == 11 &&
          reported[4] == 12 && reported[5] == 13 && reported[6] == 15);

    e = khdays_scene_map_find(&map, "EV_S#196");
    CHECK(e != NULL && !e->off && e->stem_count == 1 && strcmp(e->stems[0], "hd002") == 0 &&
          strcmp(e->evidence, "pictures: the castle") == 0);
    e = khdays_scene_map_find(&map, "EV_S#0");
    CHECK(e != NULL && e->stem_count == 1 && strcmp(e->stems[0], "hd003") == 0 && strcmp(e->evidence, "") == 0);
    e = khdays_scene_map_find(&map, "EV_S#169");
    CHECK(e != NULL && e->stem_count == 2 && strcmp(e->stems[0], "hd131a") == 0 && strcmp(e->stems[1], "hd132") == 0 &&
          strcmp(e->evidence, "lines, split") == 0);
    e = khdays_scene_map_find(&map, "mi/0105/11:_e");
    CHECK(e != NULL && e->stem_count == 1 && strcmp(e->stems[0], "hd010") == 0);
    e = khdays_scene_map_find(&map, "EV_S#3");
    CHECK(e != NULL && e->off && e->stem_count == 0 && strcmp(e->evidence, "no theater scene") == 0);
    e = khdays_scene_map_find(&map, "EV_TT#61");
    CHECK(e != NULL && e->stem_count == 1 && strcmp(e->stems[0], "hd007c") == 0);
    e = khdays_scene_map_find(&map, "mnl#1");
    CHECK(e != NULL && e->stem_count == 1 && strcmp(e->stems[0], "hd003") == 0 &&
          strcmp(e->evidence, "a monologue") == 0);
    CHECK(khdays_scene_map_find(&map, "EV_S#4") == NULL);
    CHECK(khdays_scene_map_find(&map, "EV_S#7") == NULL);

    /* a mod's entries: one replaces the port's, one adds */
    report_count = 0;
    CHECK(khdays_scene_map_read(&map, mod, length(mod), "mod", report) == 2);
    CHECK(report_count == 0);
    CHECK(map.count == 8);
    e = khdays_scene_map_find(&map, "EV_S#0");
    CHECK(e != NULL && e->off && e->stem_count == 0);
    e = khdays_scene_map_find(&map, "EV_S#4");
    CHECK(e != NULL && e->stem_count == 2 && strcmp(e->stems[1], "hd004b") == 0);

    khdays_scene_map_free(&map);
    CHECK(map.count == 0 && map.entries == NULL);
    if (failures == 0) {
        printf("scenes_test: all passed\n");
    }
    return failures != 0;
}
