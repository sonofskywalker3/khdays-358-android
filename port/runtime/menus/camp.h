/* The camp menus (camp.c), Mission Mode's and the story's, for the port to draw on one screen
 * (host/screens/camp_screen.c, a mod's layouts/camp/<lang>/camp.txt). The game's own menu runs
 * underneath (its options, cursor, sounds and pages); the port shows its menu page while the game is
 * on it, its Preferences page in the menu's place, and the game's screens everywhere else. */
#ifndef KHDAYS_CAMP_H
#define KHDAYS_CAMP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_CAMP_OPTIONS 7   /* each camp's seven: Mission Mode's entries 101-107, the story's 1-6 and 9 */
#define KHDAYS_CAMP_FIELDS 0x15 /* the status panel's fields (Ov008_DrawStatusField's 0-0x14) */
#define KHDAYS_CAMP_CHARACTERS 19
#define KHDAYS_CAMP_PLACES 4 /* a group's: the game's four */
#define KHDAYS_CAMP_NAME 12  /* a player's name: the game's 11 UTF-16 characters and a NUL */
/* where a member is (the game's pages: the member's record +3): the menu, an option's page (1-6:
 * the options' order), or ready (8: it accepted the mission) */
enum { KHDAYS_CAMP_IN_MENU = 0, KHDAYS_CAMP_READY = 8 };
#define KHDAYS_CAMP_SETTINGS 25 /* the Preferences page's table: the game's 25 entries */
#define KHDAYS_CAMP_VALUES 3    /* a setting's values: an entry holds three */
#define KHDAYS_CAMP_SECTIONS 5  /* the page's tabs: a list of them holds five */

/* A setting of the Preferences page (an entry of the game's table, its words cfg_&.s records):
 * its name and help, its values' names and helps, the one chosen; the game's flag `enabled` off,
 * the game shows its name and values as "?" (record 3). */
typedef struct KhdaysCampSetting {
    int enabled;
    int name, help;
    int values;
    int value_name[KHDAYS_CAMP_VALUES], value_help[KHDAYS_CAMP_VALUES];
    int value;
} KhdaysCampSetting;

/* The Preferences page: its sections (the game's tabs, those the game gives one title together:
 * a cfg_&.s record each, their settings in setting[] from `first`), the one the game's tab is in,
 * the setting the game's cursor is on. */
typedef struct KhdaysCampPage {
    int sections;
    int title[KHDAYS_CAMP_SECTIONS];
    int first[KHDAYS_CAMP_SECTIONS], count[KHDAYS_CAMP_SECTIONS];
    int section;
    int settings;
    KhdaysCampSetting setting[KHDAYS_CAMP_SETTINGS];
    int chosen;
} KhdaysCampPage;

/* A field of the status panel as the game last drew it: its values (fields 0x11 and 0x12 have two;
 * 0xc is Q12, 0x13 a record of status_&.s). */
typedef struct KhdaysCampField {
    int has;
    uint32_t value[2];
} KhdaysCampField;

/* A member of the group (a place of the session): whether one is there, where it is
 * (KHDAYS_CAMP_IN_MENU, an option's page 1-6 or KHDAYS_CAMP_READY), its character, its player's name
 * (empty when the game has none). */
typedef struct KhdaysCampMember {
    int present;
    int page;
    int character;
    uint16_t name[KHDAYS_CAMP_NAME];
} KhdaysCampMember;

typedef struct KhdaysCampView {
    int shown;
    int story;                       /* the story's camp (ov025): its options, its day; no group */
    int sub;                         /* the story's: 0 its options, 1 Holo-Missions' two, 2 Files' two (selected
                           * the one of them the cursor is on) */
    int day;                         /* the story's day (the game state's field 9) */
    int difficulty;                  /* the story's (the game state's 0x40a, two bits: 0-2 Beginner, Standard, Proud) */
    int language;                    /* the game's (1-5) */
    int16_t master;                  /* the game's fade: its brightness, -16 black to 16 white */
    int selected;                    /* the option selected, 0 on (the game's entry - 101); -1 none (waiting) */
    int description;                 /* the record of root_&.s the game shows for it, -1 none */
    int waiting;                     /* Mission Mode's: the mission accepted, the group waits for it (the game's
                           * entry 9): no option selected, all dimmed */
    int notice;                      /* Mission Mode's: the line the DS shows over its menu, a root_&.s record
                           * (0x16-0x1c: Ov008_Menu_UpdateDirectionalPrompt -- whether the host is
                           * choosing a mission, START to join or to begin, ...), -1 none */
    int question;                    /* Title Screen's question open (quit the game?): its answers Yes and No */
    int locked[KHDAYS_CAMP_OPTIONS]; /* the rows shown the game has locked (it shows them dimmed and
                                      * its cursor passes them) */
    int yes;                         /* the answer the cursor is on: 1 Yes, 0 No */
    int config;                      /* the Preferences page is on (screen 5): `page` instead of the menu's */
    KhdaysCampPage page;
    uint32_t munny, hearts;
    int self;      /* this copy's place */
    int character; /* its character, 0-18 (mlt_&.s's order), -1 unknown */
    KhdaysCampField field[KHDAYS_CAMP_FIELDS];
    KhdaysCampMember member[KHDAYS_CAMP_PLACES];
} KhdaysCampView;

/* The game's thread: an object's update ran (diag.c, its native address). */
void khdays_camp_update_ran(uintptr_t update);
/* The game's thread: the status panel draws a field (prepare.py's hook in Ov008_DrawStatusField;
 * `values` the game's own, two words). */
void khdays_camp_field(void *panel, int field, unsigned int delta, int style, void *values, int animate);
/* Any thread: the menu as last published. */
void khdays_camp_view(KhdaysCampView *out);
/* The game's thread: the loaded camp's screen (its table A's index: 0 the menu, 1 Holo-Missions,
 * 5 Preferences, -1 between two; the others an option's page), *story whether it is the story's
 * camp; KHDAYS_CAMP_NONE with no camp loaded. */
#define KHDAYS_CAMP_NONE (-2)
int khdays_camp_screen(int *story);
/* HD routing for the camp's four handles and the shops' three, never a shared NFTR file. */
void khdays_camp_font_scope(uint32_t font, char *scope, int size);
/* The window's thread: whether the port drew the Preferences page this frame (host/screens/camp_screen.c):
 * only then the page's directions and L and R are the port's (khdays_camp_buttons). */
void khdays_camp_set_page_drawn(int drawn);
/* The game's thread, at Pad_Sample (input.c): the buttons the game gets. On the Preferences page
 * the port's sections join the game's tabs: UP and DOWN go over a section's settings, L and R
 * from section to section, the port pressing the game's own buttons to take its cursor there;
 * the rest are the player's. */
uint16_t khdays_camp_buttons(uint16_t buttons);

#ifdef __cplusplus
}
#endif

#endif
