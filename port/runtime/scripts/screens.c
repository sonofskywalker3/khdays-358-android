/* A mod's screens and title entries (screens.h). */
#include "screens.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#define BUTTON_A 0x0001u
#define BUTTON_B 0x0002u
#define BUTTON_UP 0x0040u
#define BUTTON_DOWN 0x0080u
#define BUTTON_Y 0x0800u
#define PORT_OWNER (-1) /* the port's own list of the mods' entries */
#define LANGUAGES 6

typedef struct Entry {
    int owner, handle;
    char *labels[LANGUAGES];
} Entry;

static SDL_Mutex *lock;
static KhdaysScreen shown;
static int shown_owner, shown_handle;
static Entry *entries;
static int entry_count, entry_room;
static KhdaysScreenAnswer *answers;
static int answer_count, answer_room, answer_next;
static char *prompt;           /* the title's prompt's label while it shows ("" for several entries) */
static uint16_t held;          /* the buttons held at the last Pad_Sample */
static uint16_t blocked;       /* buttons the game does not get until let go (they answered a screen) */
static int shown_language = 1; /* the game's, for the window's thread */

static char *copy(const char *s)
{
    char *out;
    if (s == NULL) {
        return NULL;
    }
    out = malloc(strlen(s) + 1);
    if (out != NULL) {
        strcpy(out, s);
    }
    return out;
}

void khdays_screens_lock(void)
{
    if (lock == NULL) {
        lock = SDL_CreateMutex();
    }
    SDL_LockMutex(lock);
}

void khdays_screens_unlock(void)
{
    SDL_UnlockMutex(lock);
}

static void clear_screen(void)
{
    free(shown.title);
    free(shown.text);
    free(shown.picture);
    free(shown.layout);
    for (int i = 0; i < shown.row_count; ++i) {
#pragma warning(suppress : 6001) /* every row below the count was written before the count moved */
        free(shown.rows[i].label);
#pragma warning(suppress : 6001)
        free(shown.rows[i].value);
    }
    free(shown.rows);
    {
        const unsigned version = shown.version;
        memset(&shown, 0, sizeof(shown));
        shown.version = version + 1;
    }
    shown_owner = shown_handle = 0;
}

void khdays_screens_show(const KhdaysScreen *screen, int owner, int handle)
{
    khdays_screens_lock();
    clear_screen();
    shown.kind = screen->kind;
    shown.title = copy(screen->title);
    shown.text = copy(screen->text);
    shown.picture = copy(screen->picture);
    shown.layout = copy(screen->layout);
    shown.rows = screen->row_count > 0 ? malloc((size_t)screen->row_count * sizeof(*shown.rows)) : NULL;
    shown.row_count = 0;
    for (int i = 0; shown.rows != NULL && i < screen->row_count; ++i) {
        shown.rows[i].label = copy(screen->rows[i].label);
        shown.rows[i].value = copy(screen->rows[i].value);
        shown.row_count = i + 1;
    }
    shown.cursor = screen->cursor >= 0 && screen->cursor < shown.row_count ? screen->cursor : 0;
    shown_owner = owner;
    shown_handle = handle;
    khdays_screens_unlock();
}

void khdays_screens_close(int handle)
{
    khdays_screens_lock();
    if (shown.kind != KHDAYS_SCREEN_NONE && shown_handle == handle) {
        clear_screen();
    }
    khdays_screens_unlock();
}

int khdays_screens_shown(void)
{
    return shown.kind != KHDAYS_SCREEN_NONE ? shown_handle : 0;
}

void khdays_screens_forget_owner(int owner)
{
    int kept = 0;
    khdays_screens_lock();
    if (shown.kind != KHDAYS_SCREEN_NONE && shown_owner == owner) {
        clear_screen();
    }
    for (int i = 0; i < entry_count; ++i) {
        if (entries[i].owner == owner) {
            for (int l = 0; l < LANGUAGES; ++l) {
                free(entries[i].labels[l]);
            }
        } else {
            entries[kept++] = entries[i];
        }
    }
    entry_count = kept;
    khdays_screens_unlock();
    kept = 0;
    for (int i = answer_next; i < answer_count; ++i) {
        if (answers[i].owner != owner) {
            answers[kept++] = answers[i];
        }
    }
    answer_count = kept;
    answer_next = 0;
}

void khdays_screens_add_entry(int owner, int handle, char *const labels[LANGUAGES])
{
    if (entry_count == entry_room) {
        const int room = entry_room ? entry_room * 2 : 4;
        Entry *grown = realloc(entries, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        entries = grown;
        entry_room = room;
    }
    khdays_screens_lock();
    entries[entry_count].owner = owner;
    entries[entry_count].handle = handle;
    for (int l = 0; l < LANGUAGES; ++l) {
        entries[entry_count].labels[l] = copy(labels[l]);
    }
    ++entry_count;
    khdays_screens_unlock();
}

int khdays_screens_entry_count(void)
{
    return entry_count;
}

static const char *label_of(const Entry *e, int language)
{
    if (language > 0 && language < LANGUAGES && e->labels[language] != NULL) {
        return e->labels[language];
    }
    for (int l = 0; l < LANGUAGES; ++l) {
        if (e->labels[l] != NULL) {
            return e->labels[l];
        }
    }
    return "";
}

static void answer(int kind, int owner, int handle, int index)
{
    if (answer_count == answer_room) {
        const int room = answer_room ? answer_room * 2 : 8;
        KhdaysScreenAnswer *grown = realloc(answers, (size_t)room * sizeof(*grown));
        if (grown == NULL) {
            return;
        }
        answers = grown;
        answer_room = room;
    }
    answers[answer_count].kind = kind;
    answers[answer_count].owner = owner;
    answers[answer_count].handle = handle;
    answers[answer_count].index = index;
    ++answer_count;
}

int khdays_screens_take(KhdaysScreenAnswer *out)
{
    if (answer_next >= answer_count) {
        answer_count = answer_next = 0;
        return 0;
    }
    *out = answers[answer_next++];
    return 1;
}

/* The port's list of the mods' entries. */
static void show_entries(int language)
{
    KhdaysScreen list;
    KhdaysScreenRow *rows = calloc((size_t)entry_count, sizeof(*rows));
    if (rows == NULL) {
        return;
    }
    memset(&list, 0, sizeof(list));
    list.kind = KHDAYS_SCREEN_LIST;
    list.title = "Mods";
    for (int i = 0; i < entry_count; ++i) {
        rows[i].label = (char *)label_of(&entries[i], language);
    }
    list.rows = rows;
    list.row_count = entry_count;
    khdays_screens_show(&list, PORT_OWNER, 0);
    free(rows);
}

/* A press on the screen that shows: a list's cursor moved, or the screen answered and closed (a list
 * chosen with A or left with B, a page or a result closed with either). */
static void press(uint16_t pressed)
{
    const int owner = shown_owner, handle = shown_handle, cursor = shown.cursor, rows = shown.row_count;
    const int list = shown.kind == KHDAYS_SCREEN_LIST;
    if (list && rows > 0 && (pressed & (BUTTON_UP | BUTTON_DOWN))) {
        khdays_screens_lock();
        shown.cursor = (cursor + rows + ((pressed & BUTTON_DOWN) ? 1 : -1)) % rows;
        ++shown.version;
        khdays_screens_unlock();
        return;
    }
    if (!(pressed & (BUTTON_A | BUTTON_B))) {
        return;
    }
    blocked |= pressed & (BUTTON_A | BUTTON_B);
    khdays_screens_lock();
    clear_screen();
    khdays_screens_unlock();
    if (owner == PORT_OWNER) {
        if ((pressed & BUTTON_A) && cursor >= 0 && cursor < entry_count) {
            answer(KHDAYS_ANSWER_ENTRY, entries[cursor].owner, entries[cursor].handle, 0);
        }
    } else if (list && rows > 0 && (pressed & BUTTON_A)) {
        answer(KHDAYS_ANSWER_CHOOSE, owner, handle, cursor);
    } else if (list) {
        answer(KHDAYS_ANSWER_CANCEL, owner, handle, 0);
    } else {
        answer(KHDAYS_ANSWER_CLOSE, owner, handle, 0);
    }
}

uint16_t khdays_screens_buttons(uint16_t buttons, int title_idle, int language)
{
    const uint16_t pressed = (uint16_t)(buttons & ~held);
    char *now = NULL;
    held = buttons;
    blocked &= buttons; /* let go: the game's again */
    if (shown.kind != KHDAYS_SCREEN_NONE) {
        press(pressed);
        blocked |= buttons; /* what was held while it showed stays the screen's until let go */
        return 0;
    }
    if (title_idle && entry_count > 0) {
        now = copy(entry_count == 1 ? label_of(&entries[0], language) : "");
        if (pressed & BUTTON_Y) {
            blocked |= BUTTON_Y;
            if (entry_count == 1) {
                answer(KHDAYS_ANSWER_ENTRY, entries[0].owner, entries[0].handle, 0);
            } else {
                show_entries(language);
            }
        }
    }
    khdays_screens_lock();
    free(prompt);
    prompt = now;
    shown_language = language;
    khdays_screens_unlock();
    return (uint16_t)(buttons & ~blocked);
}

const KhdaysScreen *khdays_screens_current(void)
{
    return &shown;
}

int khdays_screens_language(void)
{
    return shown_language;
}

const char *khdays_screens_prompt(void)
{
    return shown.kind == KHDAYS_SCREEN_NONE ? prompt : NULL;
}
