/* The scene map (port/movies/hd_scenes.txt and a mod's cutscenes/scenes.txt): which of the theater's
 * HD videos play in place of a game's event scene, by the scene's script. Text in, entries out; no
 * game or file access (scenes.c reads the files). */
#ifndef KHDAYS_SCENE_MAP_H
#define KHDAYS_SCENE_MAP_H

#include <stddef.h>

typedef struct KhdaysSceneEntry {
    char *key;    /* EV_S#4, mi/0105/11:_e, mnl#1 */
    int off;      /* the game's own scene */
    char **stems; /* the videos in order (data/cutscenes/cinematics/<stem>.mp4) */
    int stem_count;
    char *evidence; /* the text after ';', "" when none */
} KhdaysSceneEntry;

typedef struct KhdaysSceneMap {
    KhdaysSceneEntry *entries;
    int count, capacity;
} KhdaysSceneMap;

/* Reads `text` (`size` bytes, not NUL-ended) into `map`: an entry with a key already there replaces
 * it. A malformed line is skipped and reported through `report` (the file's name and the line's
 * number), when given. Returns the entries read. */
typedef void (*KhdaysSceneReport)(const char *file, int line, const char *why);
int khdays_scene_map_read(KhdaysSceneMap *map, const char *text, size_t size, const char *file,
                          KhdaysSceneReport report);

/* The entry of `key`, or NULL. */
const KhdaysSceneEntry *khdays_scene_map_find(const KhdaysSceneMap *map, const char *key);

void khdays_scene_map_free(KhdaysSceneMap *map);

#endif
