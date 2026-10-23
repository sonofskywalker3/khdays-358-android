/* A mod's mission.start (mission_start.h). The presses are the ones a player makes in Mission Mode's
 * camp (the mission walk's): A on Holo-Missions (the menu's first option), A on the list's mission (its
 * details), A (the game's "Start this mission?"), LEFT to its Yes, A, then START once the camp shows the
 * mission accepted. Each waits for the camp to show the page it presses on (camp.h's view of the game's
 * own menu); the player's buttons are the game's again once the field is asked for or the start gives
 * up. KHDAYS_TRACE_MISSION_START=1 logs each step. */
#include "mission_start.h"

#include "camp.h"
#include "../core/rom.h"
#include "../core/window.h"

#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <stdlib.h>

extern int Session_IsActive(void);

#define SCENE_CURRENT 0x8u
#define CAMP_SCENE 19u   /* Mission Mode's camp */
#define RECORD_MISSION 2 /* the field record's mission file (u16; data_0204c240 when no argument) */
#define BUTTON_A 0x0001u
#define BUTTON_START 0x0008u
#define BUTTON_LEFT 0x0020u
#define BUTTON_UP 0x0040u
#define TAP 2       /* game frames a press is held */
#define SETTLE 45   /* game frames a page is given after a press */
#define AGAIN 90    /* game frames a page is awaited before its presses are made again */
#define ARRIVE 90   /* game frames before the first press */
#define GIVE_UP 900 /* game frames before a start that does not get through gives up */

enum { IDLE, TO_TOP, OPEN_LIST, DETAILS, ASK, YES, ACCEPTED, LEAVING };

static struct {
    int number;     /* the mission file asked for */
    int step;       /* the presses' step */
    unsigned clock; /* game frames since asked */
    unsigned at;    /* the next press not before */
    unsigned since; /* the step began */
    uint16_t tap;   /* the press being held */
    int hold;       /* its frames left */
} start;

static int tracing = -1;

static void say(const char *what)
{
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_MISSION_START") != NULL;
    }
    if (tracing) {
        fprintf(stderr, "mission-start: frame %u, mission %d: %s\n", start.clock, start.number, what);
    }
}

static unsigned scene(void)
{
    return *(const volatile unsigned *)KHDAYS_W(KHDAYS_DS_gSceneCtl + SCENE_CURRENT);
}

const char *khdays_mission_start_request(int number)
{
    char path[32];
    unsigned offset, size;
    int story = 0;
    if (number < 1 || number > 9999) {
        return "unknown mission";
    }
    snprintf(path, sizeof(path), "mi/mi/%04d", number);
    if (!khdays_rom_find(path, &offset, &size)) {
        return "unknown mission";
    }
    if (scene() != CAMP_SCENE || khdays_camp_screen(&story) == KHDAYS_CAMP_NONE || story) {
        return "not in a camp";
    }
    if (Session_IsActive()) {
        return "not in multiplayer yet";
    }
    if (start.step != IDLE) {
        return "not now";
    }
    start.number = number;
    start.step = TO_TOP;
    start.clock = start.since = 0;
    start.at = ARRIVE; /* a camp just come to takes no presses while it fades in */
    start.hold = 0;
    say("asked");
    return NULL;
}

static void step_to(int step, unsigned wait)
{
    start.step = step;
    start.since = start.clock;
    start.at = start.clock + wait;
}

static void press(uint16_t button)
{
    start.tap = button;
    start.hold = TAP;
}

uint16_t khdays_mission_start_buttons(uint16_t buttons)
{
    KhdaysCampView v;
    int story = 0, screen;
    if (start.step == IDLE) {
        return buttons;
    }
    ++start.clock;
    if (start.hold > 0) {
        --start.hold;
        return start.tap;
    }
    if (start.step == LEAVING) {
        if (scene() != CAMP_SCENE) {
            if (start.clock - start.since > GIVE_UP) {
                say("no field came");
                start.step = IDLE;
            }
            return buttons; /* the camp left: its field's record ends the start */
        }
        if (start.clock - start.since > AGAIN) {
            say("START not taken: again");
            step_to(TO_TOP, 0);
        }
        return 0;
    }
    if (scene() != CAMP_SCENE || start.clock > GIVE_UP) {
        say(scene() != CAMP_SCENE ? "the camp left another way" : "given up");
        start.step = IDLE;
        return buttons;
    }
    if (start.clock < start.at) {
        return 0; /* the player's buttons held back while the presses play */
    }
    screen = khdays_camp_screen(&story);
    khdays_camp_view(&v);
    switch (start.step) {
    case TO_TOP:
        if (screen != 0 || !v.shown) {
            break; /* a page or a transition: wait for the menu */
        }
        if (v.waiting) {
            say("a mission already accepted: START");
            press(BUTTON_START);
            step_to(LEAVING, SETTLE);
        } else if (v.selected > 0) {
            press(BUTTON_UP);
            step_to(TO_TOP, 8);
        } else if (v.selected == 0) {
            say("Holo-Missions");
            press(BUTTON_A);
            step_to(OPEN_LIST, SETTLE);
        }
        break;
    case OPEN_LIST:
        if (screen == 1) {
            say("the list: its mission");
            press(BUTTON_A);
            step_to(DETAILS, SETTLE);
        } else if (screen == 0 && start.clock - start.since > AGAIN) {
            say("the list did not open: again");
            step_to(TO_TOP, 0);
        }
        break;
    case DETAILS:
        say("the details: start it?");
        press(BUTTON_A);
        step_to(ASK, SETTLE);
        break;
    case ASK:
        press(BUTTON_LEFT);
        step_to(YES, 12);
        break;
    case YES:
        say("yes");
        press(BUTTON_A);
        step_to(ACCEPTED, SETTLE);
        break;
    case ACCEPTED:
        /* back on the menu, the mission accepted ("MISIÓN ACEPTADA", START to begin; alone the camp sets
         * no flag of it: a START that does not take sends the presses round again) */
        if (screen == 0) {
            say("accepted: START");
            press(BUTTON_START);
            step_to(LEAVING, SETTLE);
        } else if (screen == 1 && start.clock - start.since > AGAIN) {
            say("not accepted: asked again");
            step_to(DETAILS, 0);
        }
        break;
    default: break;
    }
    return start.hold > 0 ? start.tap : 0;
}

void khdays_mission_start_record(void *record)
{
    if (start.step != LEAVING) {
        if (start.step != IDLE) {
            say("a field before the presses ended: not this one");
            start.step = IDLE;
        }
        return;
    }
    *(uint16_t *)((uint8_t *)record + RECORD_MISSION) = (uint16_t)start.number;
    say("the field opens it");
    start.step = IDLE;
}
