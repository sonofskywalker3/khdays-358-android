/* The scene map's text (runtime/movies/scene_map.c): entries of the three key forms, comments, `off`,
 * malformed lines among good ones, an entry replacing one of the same key, lookups; scenes skipped
 * after a movie's video and the movies' own entries (`until`). */
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
    /* skipped scenes and the movies' entries */
    const char *movies = "EV_S#168 skip hd830 ; hd830 shows it\n"
                         "mv#833 hd833[:128.4] ; to its fade to black\n"
                         "mv#848 hd833[173.2:] hd848\n"
                         "mv#802 off\n"
                         "EV_S#182 hd833[129.4:173.2] ; a scene's part\n"
                         "EV_S#9 skip\n"       /* 6: no video named */
                         "mv#833 skip hd833\n" /* 7: a movie is not skipped */
                         "mv#834 hd834[5:2]\n" /* 8: ends before it starts */
                         "mv#835 hd835[:x]\n"  /* 9: not seconds */
                         "mv#836 hd836[1.5]\n" /* 10: no colon */
                         "mv#837 hd837[-1:]\n" /* 11: before the video's start */
                         "mnl#2 hd003[1:3]\n"; /* 12: a monologue's voice is whole */

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

    report_count = 0;
    CHECK(khdays_scene_map_read(&map, movies, length(movies), "movies", report) == 5);
    CHECK(report_count == 7 && reported[0] == 6 && reported[1] == 7 && reported[2] == 8 && reported[3] == 9 &&
          reported[4] == 10 && reported[5] == 11 && reported[6] == 12);
    e = khdays_scene_map_find(&map, "EV_S#168");
    CHECK(e != NULL && e->skip && !e->off && e->stem_count == 1 && strcmp(e->stems[0], "hd830") == 0 &&
          strcmp(e->evidence, "hd830 shows it") == 0);
    e = khdays_scene_map_find(&map, "mv#833");
    CHECK(e != NULL && !e->skip && e->stem_count == 1 && strcmp(e->stems[0], "hd833") == 0 && e->from != NULL &&
          e->until != NULL && e->from[0] == 0.0 && e->until[0] == 128.4 &&
          strcmp(e->evidence, "to its fade to black") == 0);
    e = khdays_scene_map_find(&map, "EV_S#182");
    CHECK(e != NULL && !e->skip && e->stem_count == 1 && strcmp(e->stems[0], "hd833") == 0 && e->from != NULL &&
          e->until != NULL && e->from[0] == 129.4 && e->until[0] == 173.2);
    e = khdays_scene_map_find(&map, "mv#848");
    CHECK(e != NULL && e->stem_count == 2 && strcmp(e->stems[0], "hd833") == 0 && strcmp(e->stems[1], "hd848") == 0 &&
          e->from != NULL && e->until != NULL && e->from[0] == 173.2 && e->until[0] == 0.0 && e->from[1] == 0.0 &&
          e->until[1] == 0.0);
    e = khdays_scene_map_find(&map, "mv#802");
    CHECK(e != NULL && e->off);
    e = khdays_scene_map_find(&map, "EV_S#4");
    CHECK(e != NULL && e->from == NULL && e->until == NULL); /* whole videos */
    CHECK(khdays_scene_map_find(&map, "EV_S#9") == NULL && khdays_scene_map_find(&map, "mv#834") == NULL &&
          khdays_scene_map_find(&map, "mv#835") == NULL && khdays_scene_map_find(&map, "mnl#2") == NULL);

    khdays_scene_map_free(&map);
    CHECK(map.count == 0 && map.entries == NULL);
    if (failures == 0) {
        printf("scenes_test: all passed\n");
    }
    return failures != 0;
}
