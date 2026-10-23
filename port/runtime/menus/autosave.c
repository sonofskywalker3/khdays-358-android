/* The autosave (autosave.h). A mission's results recorded as cleared (Ov005_CommitMissionResults: the
 * story's with flag 0x2086, a ranked one otherwise; never after a give-up, flag 0x2087) owe one. After
 * the results the game takes the player to a camp: Mission Mode's (ov008, scene 19), or in the story
 * the field's camp (ov025: the field loads it for its phase 11, Ov002_LoadSubFlowOverlay, while the
 * "back from a mission" flags 0x18bd/0x18c9 that Ov005_UpdateMenuExitTransition set are on). The first
 * time a camp's menu waits for the player, the camp's own save writes the file the setting names: the
 * game slot the page view puts that file in, the save screen's state a block of the game's heap with
 * the highest save counter of all the files (as the port's lists give it, savelist.c), then
 * BeginSaveToSlot, PollSaveCardOp until the card is done and LatchTick, as the camp's save screen runs
 * them (Ov008_SaveMenuConfirm, Ov008_SaveMenuTick). The whole write is made at one Pad_Sample, waiting
 * for the card as the game's own synchronous card reads do (CARDi_WaitAsync): nothing of the game runs
 * between the state's signing and its two copies. docs/RUNTIME_MAP.md section 15.4, "The autosave". */
#include "autosave.h"
#include "savelist.h"
#include "saveslots.h"

#include "../core/session_log.h"
#include "../core/window.h"

#include "khdays_ds_symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int Ov008_BeginSaveToSlot(KhdaysRef menu, u32 slot);
extern int Ov008_PollSaveCardOp(KhdaysRef menu, int slot, int phase, int arg);
extern void Ov008_LatchTick(void);
extern int Ov025_BeginSaveToSlot(KhdaysRef menu, u32 slot);
extern int Ov025_PollSaveCardOp(KhdaysRef menu, int slot, int phase, int arg);
extern void Ov025_LatchTick(void);
extern int CARDi_WaitAsync(void);
extern int CARDi_TryWaitAsync(void);
extern int Sleep_Block(void);
extern int Sleep_Unblock(void);
extern int GameState_GetField(int field, int bits);
extern void GameState_SetField(int field, int bits, int value);
extern int GameState_IsFlagSet(int flag);
extern KhdaysRef NNS_FndAllocFromDefaultExpHeapEx(unsigned int size, int align);
extern int NNSi_FndFreeFromDefaultHeap(KhdaysRef block);

/* host/app/profile.h's KHDAYS_AUTOSAVE_* */
enum { MODE_OFF, MODE_FILE99, MODE_PLAYED };

/* the results' configuration, data_ov005_0205b85c (Ov005_CommitMissionResults' Ov005Config) */
#define RESULTS 0x0205b85cu
#define RESULTS_REWARD_MODE 0xcu /* u16: 255 the story's, 8 and the rest ranked */
#define RESULTS_RANK 0x10u
#define RESULTS_OPTION 0x64u /* set: the mission only marked seen */
#define FLAG_CLEARED 0x2086  /* the story's: without it the routine only marks the mission seen */
#define FLAG_GIVEN_UP 0x2087 /* Ov002_RetireSubFlow's give-up */
#define SCENE_CURRENT 0x8u
#define TITLE_SCENE 1
/* the save screen's state as the save code reads it: its slots' rolled faces (+0x28), its card operation
 * (+0x238), its save counter (+0x1ef8), the two flags it keeps while it writes (+0x1efc) */
#define MENU_SIZE 0x1f00u
#define MENU_COUNTER 0x1ef8u
#define FIELD_SAVE_SLOT 0xc98 /* the game slot saved to last (BeginSaveToSlot) */
#define POLL_PENDING (-1)
#define POLL_DONE 0

typedef struct Camp {
    int overlay;
    int (*begin)(KhdaysRef menu, u32 slot);
    int (*poll)(KhdaysRef menu, int slot, int phase, int arg);
    void (*latch)(void);
} Camp;
static const Camp camps[] = {
    {8, Ov008_BeginSaveToSlot, Ov008_PollSaveCardOp, Ov008_LatchTick},
    {25, Ov025_BeginSaveToSlot, Ov025_PollSaveCardOp, Ov025_LatchTick},
};

static volatile LONG mode = MODE_FILE99;
static int owed;
static int tracing = -1;

/* the notices not told yet (the HUD's notice takes them, ui/autosave_notice.h) */
typedef struct Notice {
    int file, ok;
} Notice;
static SRWLOCK lock = SRWLOCK_INIT;
static Notice *notices;
static int notice_count, notice_room;

/* KHDAYS_AUTOSAVE_TEST, for scripted runs that cannot clear a mission: every result recorded owes the
 * autosave, and so does the game's leaving the title, once; "played" or "off" the setting too (a run
 * without a window has the default) */
static int testing(void)
{
    static int test = -1;
    if (test < 0) {
        const char *value = getenv("KHDAYS_AUTOSAVE_TEST");
        test = value != NULL;
        if (test && strcmp(value, "played") == 0) {
            InterlockedExchange(&mode, MODE_PLAYED);
        } else if (test && strcmp(value, "off") == 0) {
            InterlockedExchange(&mode, MODE_OFF);
        }
    }
    return test;
}

static int trace(void)
{
    if (tracing < 0) {
        tracing = getenv("KHDAYS_TRACE_AUTOSAVE") != NULL;
    }
    return tracing;
}

void khdays_autosave_set_mode(int value)
{
    InterlockedExchange(&mode, value);
}

int khdays_autosave_results_cleared(void)
{
    const int reward_mode = *(volatile u16 *)KHDAYS_W(RESULTS + RESULTS_REWARD_MODE);
    const int rank = *(volatile int *)KHDAYS_W(RESULTS + RESULTS_RANK);
    const int option = *(volatile int *)KHDAYS_W(RESULTS + RESULTS_OPTION);
    if (GameState_IsFlagSet(FLAG_GIVEN_UP)) {
        return 0;
    }
    switch (reward_mode) {
    case 255: return option == 0 && GameState_IsFlagSet(FLAG_CLEARED);
    case 8: return option == 0 && rank >= 0 && rank <= 3;
    default: return rank >= 0 && rank <= 2;
    }
}

void khdays_autosave_mission_end(void)
{
    const int cleared = khdays_autosave_results_cleared() || testing();
    if (trace()) {
        fprintf(stderr, "autosave: results recorded, %s (setting %ld)\n", cleared ? "cleared" : "not cleared", mode);
    }
    if (cleared && mode != MODE_OFF) {
        owed = 1;
        khdays_log("autosave: a mission cleared, the autosave waits for a camp's menu");
    }
}

static void tell(int file, int ok)
{
    AcquireSRWLockExclusive(&lock);
    if (notice_count == notice_room) {
        const int room = notice_room != 0 ? notice_room * 2 : 4;
        Notice *more = (Notice *)realloc(notices, (size_t)room * sizeof(*more));
        if (more != NULL) {
            notices = more;
            notice_room = room;
        }
    }
    if (notice_count < notice_room) {
        notices[notice_count].file = file;
        notices[notice_count].ok = ok;
        ++notice_count;
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_autosave_take_notice(int *file, int *ok)
{
    int taken = 0;
    AcquireSRWLockExclusive(&lock);
    if (notice_count > 0) {
        *file = notices[0].file;
        *ok = notices[0].ok;
        memmove(notices, notices + 1, (size_t)(notice_count - 1) * sizeof(*notices));
        --notice_count;
        taken = 1;
    }
    ReleaseSRWLockExclusive(&lock);
    return taken;
}

/* The file the setting names: file 99; or the file being played, a game never saved the first file
 * without a save (none: -1). The guest game has none. */
static int target_file(void)
{
    const int played = khdays_savelist_played_file();
    if (played == KHDAYS_PLAYED_GUEST) {
        return -1;
    }
    if (mode == MODE_FILE99) {
        return KHDAYS_SLOT_COUNT - 1;
    }
    if (played >= 0) {
        return played;
    }
    for (int f = 0; f < KHDAYS_SLOT_COUNT; ++f) {
        if (khdays_slots_state(f, NULL) == KHDAYS_SLOT_EMPTY) {
            return f;
        }
    }
    return -1;
}

static void write_file(const Camp *c, int file)
{
    const int last = KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS;
    KhdaysSlotsView kept;
    KhdaysRef menu;
    u32 counter;
    const int saved_last = GameState_GetField(FIELD_SAVE_SLOT, 2);
    int top, slot, result;
    /* the page with the file on the row the game saved to last, where the files allow: the game's
     * cursor starts there when the file is played again (savelist.c, pages) */
    top = file - saved_last;
    top = top < 0 ? 0 : top > last ? last : top;
    slot = file - top;
    menu = NNS_FndAllocFromDefaultExpHeapEx(MENU_SIZE, 4);
    if (menu == 0) {
        khdays_log("autosave: no room in the game's heap for the save screen's state: file %d not written", file + 1);
        tell(file, 0);
        return;
    }
    memset(KHDAYS_W(menu), 0, MENU_SIZE);
    counter = khdays_slots_highest_counter();
    *(volatile u32 *)KHDAYS_W(menu + MENU_COUNTER) = counter;
    khdays_slots_keep_view(&kept);
    khdays_slots_show_page(top);
    Sleep_Block(); /* as the save screen's confirm does before it saves */
    c->begin(menu, (u32)slot);
    do {
        CARDi_WaitAsync();
        result = c->poll(menu, slot, 0, 0);
    } while (result == POLL_PENDING);
    if (result == POLL_DONE) {
        c->latch(); /* the play time counted on from now, as after the screen's save */
    }
    Sleep_Unblock(); /* as the screen does once its save is read back */
    if (mode == MODE_FILE99) {
        /* the camps' save screens start their cursor on the slot saved to last (field 0xc98): on the
         * file being played still, as if the autosave had not been made (the saved file keeps its own) */
        GameState_SetField(FIELD_SAVE_SLOT, 2, saved_last);
    }
    khdays_slots_put_view(&kept);
    NNSi_FndFreeFromDefaultHeap(menu);
    if (result == POLL_DONE && mode == MODE_PLAYED) {
        khdays_savelist_set_played_file(file);
    }
    khdays_log("autosave: ov%03d's save into file %d (game slot %d, save counter %u): %s", c->overlay, file + 1,
               slot + 1, counter + 1, result == POLL_DONE ? "written" : "failed, the file as it was");
    if (trace()) {
        fprintf(stderr, "autosave: file %d %s\n", file + 1, result == POLL_DONE ? "written" : "failed");
    }
    tell(file, result == POLL_DONE);
}

void khdays_autosave_frame(void)
{
    static int traced_screen = -2, traced_entry = -2, traced_card = -1, tested;
    int overlay, file, screen, entry, card;
    if (testing() && !tested && *(volatile u32 *)KHDAYS_W(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) > TITLE_SCENE) {
        tested = owed = 1;
    }
    if (!owed) {
        return;
    }
    if (mode == MODE_OFF || *(volatile u32 *)KHDAYS_W(KHDAYS_DS_gSceneCtl + SCENE_CURRENT) == TITLE_SCENE) {
        owed = 0; /* switched off, or the game left for the title */
        khdays_log("autosave: no longer owed (%s)", mode == MODE_OFF ? "switched off" : "back on the title");
        return;
    }
    overlay = khdays_savelist_camp_menu(&screen, &entry);
    card = CARDi_TryWaitAsync();
    if (trace() && (screen != traced_screen || entry != traced_entry || card != traced_card)) {
        fprintf(stderr, "autosave: owed; camp ov%03d's screen %d, entry %d, card %s\n", overlay, screen, entry,
                card ? "idle" : "busy");
        traced_screen = screen;
        traced_entry = entry;
        traced_card = card;
    }
    if (overlay == 0 || !card) {
        return;
    }
    owed = 0;
    file = target_file();
    if (file < 0) {
        const char *why = khdays_savelist_played_file() == KHDAYS_PLAYED_GUEST ? "the guest game has no file"
                                                                               : "no file without a save";
        khdays_log("autosave: %s: nothing written", why);
        if (trace()) {
            fprintf(stderr, "autosave: %s: nothing written\n", why);
        }
        return;
    }
    for (size_t i = 0; i < sizeof(camps) / sizeof(camps[0]); ++i) {
        if (camps[i].overlay == overlay) {
            write_file(&camps[i], file);
        }
    }
}
