/* The theater's HD scenes in place of the game's event scenes: the scene map, and which
 * scene the field's event runs. */
#ifndef KHDAYS_SCENES_H
#define KHDAYS_SCENES_H

#include <stdint.h>

#include "scene_map.h"

/* The map: the port's (port/movies/hd_scenes.txt, compiled in) and a mod's cutscenes/scenes.txt over
 * it; read once, on first use. */
const KhdaysSceneMap *khdays_scenes_map(void);

/* The scene whose skippable stretch the event's machine has just begun: `commands` the reference of
 * its running script's steps (the depth's entry +0xc). Writes its key (EV_S#4) into `key` and returns
 * 1, or 0 when the script is none of the ROM's event archives' entries. */
int khdays_scenes_recognise(uint32_t commands, char *key, int size);

#endif
