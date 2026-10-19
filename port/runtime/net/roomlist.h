/* The port's list of rooms (roomlist.c): every room with its name, players, ping and lock, a search,
 * and a created room's name and password, drawn by the port (host/screens/room_screen.c) or by the
 * game's own screens (ui/rooms2d.c). The game's list runs underneath: the port takes its cursor to a
 * room and hands it A, and the joining, members' screen and errors are the game's own
 * (docs/MULTIPLAYER.md, "The list of rooms"). */
#ifndef KHDAYS_ROOMLIST_H
#define KHDAYS_ROOMLIST_H

#include "net_session.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { KHDAYS_ROOMS_NONE, KHDAYS_ROOMS_LIST, KHDAYS_ROOMS_CREATE, KHDAYS_ROOMS_PASSWORD, KHDAYS_ROOMS_MEMBERS };
/* the list's first rows, before its rooms: Create a Group, the search */
#define KHDAYS_ROOMS_FIXED 2
enum { KHDAYS_ROOMROW_FIELD, KHDAYS_ROOMROW_ROOM, KHDAYS_ROOMROW_ACTION, KHDAYS_ROOMROW_NONE, KHDAYS_ROOMROW_MEMBER };
enum { KHDAYS_ROOMFIELD_SEARCH, KHDAYS_ROOMFIELD_NAME, KHDAYS_ROOMFIELD_PASSWORD };
enum { KHDAYS_ROOMACTION_CREATE, KHDAYS_ROOMACTION_JOIN };

typedef struct KhdaysRoomRow {
    int kind;               /* KHDAYS_ROOMROW_* */
    int which;              /* a field's (KHDAYS_ROOMFIELD_*), an action's (KHDAYS_ROOMACTION_*) or a
                              * member's place (0 the host) */
    KhdaysNetRoomView room; /* a room's; a member's: players 1 when there, its ping_ms */
    int level;              /* a member's level (the game's own), 0 when not known */
    const char *text;       /* a room's or member's name, or a field's text (UTF-8; in the view's own
                              * memory) */
} KhdaysRoomRow;

typedef struct KhdaysRoomView {
    int screen;         /* KHDAYS_ROOMS_*: NONE when the game's list is not on */
    int look_game;      /* without the HD pieces: the game's own screens draw it (ui/rooms2d.c),
                              * the rows in the game's order, the last (an action) on its button */
    int top;            /* then the row on the game's first plate */
    int language;       /* the game's (1-5) */
    int cursor;         /* the row selected */
    int editing;        /* the selected field takes what is typed */
    int wrong_password; /* the last join was refused for its password */
    const char *mods_needed; /* the last join was refused for other gameplay mods: the host's ("name
                              * version, ..."; empty otherwise), and how many its answer left out */
    int mods_more;
    int16_t master;     /* the game's fade: its brightness, -16 black to 16 white */
    int row_count;
    KhdaysRoomRow *rows;
    /* the members' screen: the room's name (as this copy chose or joined it; empty when not
     * known), whether this copy hosts it, its own place, and whether the host can go on (two
     * players at least: the game's Proceed) */
    const char *title;
    int host, self, can_proceed;
} KhdaysRoomView;

/* The game's thread: an object's update ran (diag.c, its native address). */
void khdays_roomlist_update_ran(uintptr_t update);
/* The game's thread, once a frame (input.c): the buttons the game gets (the port's list takes
 * them while it shows). */
uint16_t khdays_roomlist_buttons(uint16_t buttons);

/* Any thread: the list as last published, held until khdays_roomlist_release (short: one draw). */
const KhdaysRoomView *khdays_roomlist_hold(void);
void khdays_roomlist_release(void);

/* The window's thread: whether a field takes what is typed now; text typed (UTF-8); and the
 * keys that end or change it. */
enum { KHDAYS_EDIT_ERASE, KHDAYS_EDIT_DONE, KHDAYS_EDIT_CANCEL, KHDAYS_EDIT_CLEAR };
int khdays_roomlist_editing(void);
void khdays_roomlist_type(const char *utf8);
void khdays_roomlist_edit(int key);

#ifdef __cplusplus
}
#endif

#endif
