/* The game's events as the mods hear them (api_events.c). */
#ifndef KHDAYS_SCRIPTS_EVENTS_H
#define KHDAYS_SCRIPTS_EVENTS_H

struct lua_State;

enum KhdaysEventId {
    KHDAYS_EVENT_FRAME,
    KHDAYS_EVENT_SCENE,
    KHDAYS_EVENT_TITLE,
    KHDAYS_EVENT_MISSION_START,
    KHDAYS_EVENT_MISSION_END,
    KHDAYS_EVENT_HP,
    KHDAYS_EVENT_ITEM,
    KHDAYS_EVENT_MUNNY,
    KHDAYS_EVENT_MENU_OPEN,
    KHDAYS_EVENT_MENU_CLOSE,
    KHDAYS_EVENT_SAVE,
    KHDAYS_EVENT_COUNT
};

/* An event's details; which fields mean what is each event's own (api_events.c). */
typedef struct KhdaysEvent {
    int id;
    int a, b, c, d;
    const char *text; /* a constant string */
} KhdaysEvent;

/* Delivers the event to every mod listening, in load order; while a script runs, after it. */
void khdays_script_events_raise(const KhdaysEvent *event);

/* Around a script the port runs outside an event (a main script, an option's code): events raised
 * meanwhile wait for its end. */
void khdays_script_events_hold(void);
void khdays_script_events_release(void);

/* The frame point's own events: the frame, and what changed since the last frame (scene, items,
 * munny, menus). */
void khdays_script_events_frame(void);

/* Forgets the last frame's state (a reload of every mod, the game's state made anew). */
void khdays_script_events_reset(void);

/* mod.on and mod.off, for the mod table (api_mod.c). */
int khdays_script_events_on(struct lua_State *L);
int khdays_script_events_off(struct lua_State *L);

/* Whether any mod listens to the event. */
int khdays_script_events_wanted(int id);

#endif
