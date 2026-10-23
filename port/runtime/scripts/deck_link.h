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
    int level, casts;         /* a magic panel's first magic (+0x19, +0x1a): what a magic panel set on this one
                               * adds to its level and its casts (Magic LV2 2 and 1, Doublecast 0 and 2) */
} KhdaysLinkPanel;
/* Item `id`'s panel; 0 when it is none: the camp's Panels do not list it (Ov008_IsGridItemEligible: the
 * Slot Releaser, the badges ...), it is a synthesis material (category 2), or the records cannot be read. */
int khdays_link_panel(int id, KhdaysLinkPanel *out);

/* The highest level of magic `magic` (its battle index, 1-15: message database 0x13's record, MsgDb_DecodeDb13's
 * +0x24); 0 when the records cannot be read. */
int khdays_link_magic_cap(int magic);

/* Multi-cell panel `p` laid at cell `at` (its own cell): `under` gets `at` on each cell it covers. 0 when one is
 * off its page, at or past `open`, or under another already. */
static inline int khdays_link_cover(const KhdaysLinkPanel *p, int at, int open, short *under)
{
    const int page = at / KHDAYS_LINK_DECK_PAGE, left = at % KHDAYS_LINK_DECK_COLS - p->col,
              top = at % KHDAYS_LINK_DECK_PAGE / KHDAYS_LINK_DECK_COLS - p->row;
    for (int bit = 0; bit < 64; ++bit) {
        const int col = left + bit % 8, row = top + bit / 8;
        int cell;
        if (!(p->cells >> bit & 1u)) {
            continue;
        }
        if (col < 0 || col >= KHDAYS_LINK_DECK_COLS || row < 0 || row >= KHDAYS_LINK_DECK_ROWS) {
            return 0;
        }
        cell = page * KHDAYS_LINK_DECK_PAGE + row * KHDAYS_LINK_DECK_COLS + col;
        if (cell >= open || under[cell] >= 0) {
            return 0;
        }
        under[cell] = (short)at;
    }
    return 1;
}

#endif
