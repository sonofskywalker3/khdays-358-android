/* A mod's screens and title entries (api_screens.c), shared between the game's thread -- the mods make
 * them, the presses move them at Pad_Sample -- and the window's, which draws them
 * (host/screens/mod_screen.c). One screen shows at a time; while it shows the game gets no presses.
 * The title's mods' prompt shows while the title's menu waits for the player and some mod made an
 * entry: its Y opens the entries (one alone is chosen at once). docs/SCRIPTING.md, "Screens". */
#ifndef KHDAYS_SCRIPTS_SCREENS_H
#define KHDAYS_SCRIPTS_SCREENS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum KhdaysScreenKind { KHDAYS_SCREEN_NONE, KHDAYS_SCREEN_LIST, KHDAYS_SCREEN_PAGE, KHDAYS_SCREEN_RESULT };

typedef struct KhdaysScreenRow {
    char *label, *value; /* UTF-8; value NULL in a list */
} KhdaysScreenRow;

/* What shows: the window's thread reads it between khdays_screens_lock and _unlock. */
typedef struct KhdaysScreen {
    int kind;      /* KhdaysScreenKind; NONE when nothing shows */
    char *title;   /* UTF-8 */
    char *text;    /* a page's, lines by '\n' */
    char *picture; /* a page's PNG (a path of the mod's folder), NULL without */
    char *layout;  /* the mod's layout file (a path), NULL for the port's own */
    KhdaysScreenRow *rows;
    int row_count;
    int cursor;       /* a list's row chosen */
    unsigned version; /* changed with every change of the screen (its pictures read again) */
} KhdaysScreen;

/* What a mod gets back at the frame point (khdays_screens_take). */
enum KhdaysScreenAnswerKind { KHDAYS_ANSWER_CHOOSE, KHDAYS_ANSWER_CANCEL, KHDAYS_ANSWER_CLOSE, KHDAYS_ANSWER_ENTRY };

typedef struct KhdaysScreenAnswer {
    int kind;   /* KhdaysScreenAnswerKind */
    int owner;  /* the screen's or entry's owner's number (api_screens.c's) */
    int handle; /* the screen's, or the entry's */
    int index;  /* CHOOSE: the row, 0-based */
} KhdaysScreenAnswer;

/* The game's thread: shows a screen (its strings copied; the previous one, if any, gone without an
 * answer); closes the one that shows if it is `handle`'s. */
void khdays_screens_show(const KhdaysScreen *screen, int owner, int handle);
void khdays_screens_close(int handle);
/* The one that shows: its handle, 0 for none. */
int khdays_screens_shown(void);
/* Every screen and entry of an owner gone (the mod stopped). */
void khdays_screens_forget_owner(int owner);

/* The game's thread: the title's entries (labels by language, [0] for any; copied). */
void khdays_screens_add_entry(int owner, int handle, char *const labels[6]);
int khdays_screens_entry_count(void);

/* The game's thread, at Pad_Sample (input.c): the buttons the game gets; `title_idle` whether the
 * title's menu waits for the player (its prompt shows then). */
uint16_t khdays_screens_buttons(uint16_t buttons, int title_idle, int language);

/* The game's thread: the next answer, 1 while there is one. */
int khdays_screens_take(KhdaysScreenAnswer *out);

/* The window's thread. */
void khdays_screens_lock(void);
void khdays_screens_unlock(void);
const KhdaysScreen *khdays_screens_current(void); /* under the lock */
/* The prompt's label (UTF-8) while the title's prompt shows, else NULL (under the lock). */
const char *khdays_screens_prompt(void);
/* The game's language as the game's thread last gave it (1-5; under the lock). */
int khdays_screens_language(void);

#ifdef __cplusplus
}
#endif

#endif
