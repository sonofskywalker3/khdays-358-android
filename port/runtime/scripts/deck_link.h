/* The deck, the game's side (api_deck.c has the mods' side). The tests give their own. */
#ifndef KHDAYS_SCRIPTS_DECK_LINK_H
#define KHDAYS_SCRIPTS_DECK_LINK_H

/* The deck (gGameState +0xee0, Ov008_LoadGridFromSave): 3 pages of 8 rows of 5 cells, each panel's
 * item id at its own cell and 0 elsewhere. Open are the first cells in page, row and column order:
 * item 1's copies + 15, at most all (Ov008_GridMenuInitStep). */
#define KHDAYS_LINK_DECK_COLS 5
#define KHDAYS_LINK_DECK_ROWS 8
#define KHDAYS_LINK_DECK_PAGE (KHDAYS_LINK_DECK_COLS * KHDAYS_LINK_DECK_ROWS)
#define KHDAYS_LINK_DECK_CELLS (3 * KHDAYS_LINK_DECK_PAGE)
#define KHDAYS_LINK_DECK_OPENER 1 /* the item whose copies open cells */
#define KHDAYS_LINK_DECK_OPEN 15  /* the cells open without it */

/* The deck's cells (KHDAYS_LINK_DECK_CELLS); 0 without the game's state. */
int khdays_link_deck(unsigned short *cells);
/* Whether the deck may change now: NULL, else why not (a camp's page keeps its own copy of the deck and
 * writes it back as it closes, Ov008_DestroyGridMenu). */
const char *khdays_link_deck_busy(void);
void khdays_link_set_deck(const unsigned short *cells);

/* A panel as the game's records have it (message database 0x15, MsgDb_LoadRecord15; its shape from
 * 0x1b, MsgDb_FetchEntryPair0x1b). */
typedef struct KhdaysLinkPanel {
    int category;
    int kind;
    int shape;                /* -1: none (a link panel, set on another panel's cells) */
    unsigned long long cells; /* the shape's: bit column + 8 x row */
    int col, row;             /* the panel's own cell in its shape */
} KhdaysLinkPanel;
/* Item `id`'s panel; 0 when it is none: the camp's Panels do not list it (Ov008_IsGridItemEligible: the
 * Slot Releaser, the badges ...), it is a synthesis material (category 2), or the records cannot be read. */
int khdays_link_panel(int id, KhdaysLinkPanel *out);

#endif
