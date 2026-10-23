/* A mod's mission.start (runtime/scripts/api_run.c): from Mission Mode's camp the port presses for the
 * player what starts the camp's first listed mission -- Holo-Missions, the mission, its Yes, START --
 * holding the player's own buttons back meanwhile, and the field is built with the mod's mission file
 * instead (Ov002_ConstructGameplayScene's record, as KHDAYS_TEST_EVENT does). The camp's pages, its
 * teardown and the field's start are the game's own. docs/RUNTIME_MAP.md, "Starting a mission". */
#ifndef KHDAYS_MISSION_START_H
#define KHDAYS_MISSION_START_H

#include <stdint.h>

/* Asks for mission file `number` (mi/mi/<number>) to be started from the camp shown; NULL when on its
 * way, else why not. */
const char *khdays_mission_start_request(int number);

/* The game's thread, at Pad_Sample (input.c): the buttons the game gets while a start is on its way. */
uint16_t khdays_mission_start_buttons(uint16_t buttons);

/* Ov002_ConstructGameplayScene's record (scene_trace.c's join): the asked mission put in its place. */
void khdays_mission_start_record(void *record);

#endif
