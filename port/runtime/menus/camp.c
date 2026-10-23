/* The camp menus (camp.h): Mission Mode's (scene 19, ov008) and the story's (ov025), the same code
 * under other names. The port reads what each screen shows (options, status numbers, members, locked
 * options, Preferences' tabs) and steers Preferences with the game's own presses. Every address and
 * offset, and how each was found: docs/RUNTIME_MAP.md section 16.4. */
#include "camp.h"

#include "../input/input.h"
#include "../core/io.h"
#include "../core/session_log.h"
#include "../core/window.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);
extern int GameState_GetField(int a, int b);
extern int Ov008_GetLocalPlayerIndex(void);
extern void Ov008_RouteCommittedPageState(void);
extern void Ov025_GetIdleHandler(void);
extern int Ov025_GetLocalPlayerCharacter(int unused);
extern int Ov008_SelectTierIfActive(int list, unsigned tab, int c);
extern int Ov025_SelectTierIfActive(int list, unsigned tab, int c);
extern int GetFrameRateMode(void);
extern int GameState_IsFlagSet(int flag);
extern int Ov008_IsSessionReady(void);
int khdays_language_wanted(void); /* language.c */
u32 khdays_events_vblank_count(void);
int khdays_overlay_loaded(int id); /* overlays.c */

/* the menu is gone when the camp's update did not run for longer than the game's slowest pace (a step
 * every three VBlanks, with three players or more) */
#define SEEN_FRAMES 3u
#define SCREEN_STATE 0x959cu
#define ENTRY 0x88u
#define DESCRIPTION 0x5c4u
#define NOTICE 0x5c0u /* the line over the menu: a root_&.s record (see camp.h) */
#define NOTICE_FIRST 0x16
#define NOTICE_LAST 0x1c
#define WAITING 9               /* Mission Mode's entry while the group waits for the mission */
#define MISSION_ACCEPTED 0x200c /* the game state's flag Ov008_DrawMenuValue reads */
#define WAIT_FOR_MISSION 0x20   /* root_&.s: "stay waiting for your mission" */
#define FIRST_OPTION 101
#define ANSWER_YES 7
#define ANSWER_NO 8
#define QUIT_QUESTION 0x15     /* root_&.s: "Quit the game?" */
#define TITLE_SCREEN 6         /* the options' last, whose question it is */
#define GAME_STATE 0x0204be18u /* gGameState: the game's state, a reference */
#define MUNNY 0x8u
#define HEARTS 0xcu
#define ITEM_COUNTS 0x810u
#define SESSION 0x02090f24u
#define ROWS 0x4acu
#define ROW_SIZE 6u
#define ROW_CHARACTER 2u
#define NAMES_PRESENT 0x434u
#define NAMES 0x436u
#define NAME_LENGTH 11u
#define HEAP 0x02090f00u /* the camp's heap: a reference */
#define MASK 0x2cu
#define RECORDS 0x2eu
#define RECORD_SIZE 6u
#define RECORD_PAGE 3u
#define PAGE_GONE 7           /* Ov008_BuildSlotMatchMask(7): left out of the members shown */
#define STORY_QUESTION 0x5c0u /* the story's: its question open (Ov025_Hub_SetSubMenu) */
#define ROXAS 12              /* mlt_&.s's order */
#define ENTRY_LIST 0x4a38u    /* the context's entries (see above) */
#define ENTRY_ID 0xcu
#define ENTRY_ALTERNATE 0x84u /* bit 0: locked, its alternate (dimmed) set shown */
#define CONFIG_SCREEN 5       /* the Preferences page (see above) */
#define PREFERENCES 4         /* its option: the options' fifth in both camps */
#define CONFIG_TAB 0x0u
#define CONFIG_LIST 0x2u
#define CONFIG_ENTRY 0x4cu
#define CONFIG_VALUES 0x4eu
#define ENTRY_SIZE 0x14u
#define LIST_SIZE 0x12u
#define LISTS 3 /* Ov008_GetPageTableEntry's three */
#define PAGE_SIZE 4u
#define TITLE_SETTINGS 0    /* cfg_&.s: "Adjust your settings." */
#define CAMP_REPEAT 0x963eu /* the camp's KeyRepeat (savelist.c's) */
#define REPEAT_DELAY 2u
#define REPEAT_INTERVAL 4u

/* A camp: its overlay, its object's update while a screen runs, its table A's index (table B's
 * the next word) and the variable whose second word is its context. */
typedef struct Camp {
    int overlay;
    void (*idle)(void);
    u32 screen_index;
    u32 camp;
    int story;
    /* its Preferences page's tables (see above) and its tabs' titles */
    u32 entries, lists, pages;
    int (*tab_title)(int list, unsigned tab, int c);
} Camp;

static const Camp camps[] = {
    {8, Ov008_RouteCommittedPageState, 0x02090f0cu, 0x02090f04u, 0, 0x02090598u, 0x0209051cu, 0x02090504u,
     Ov008_SelectTierIfActive},
    {25, Ov025_GetIdleHandler, 0x020b574cu, 0x020b5744u, 1, 0x020b4f64u, 0x020b4ee8u, 0x020b4ed0u,
     Ov025_SelectTierIfActive},
};
#define CAMPS ((int)(sizeof(camps) / sizeof(camps[0])))

/* The story's options: the menu's entry ids top to bottom and each one's description; then the two of
 * Holo-Missions (Normal Missions, Challenges) and of Files (Roxas's Diary, Enemy Profiles), which the
 * game's list shows in its place. */
static const int STORY_ENTRIES[KHDAYS_CAMP_OPTIONS] = {1, 2, 3, 4, 5, 6, 9};
static const int STORY_DESCRIPTIONS[KHDAYS_CAMP_OPTIONS] = {9, 10, 12, 15, 11, 16, 17};
static const int STORY_SUB_ENTRIES[2][2] = {{10, 11}, {12, 13}};
static const int STORY_SUB_DESCRIPTIONS[2][2] = {{18, 19}, {13, 14}};
/* the panel's animated offsets (Ov008_StatusPanel) */
#define OFFSET_Q12 0x1dcu
#define OFFSETS 0x1e0u

static SRWLOCK lock = SRWLOCK_INIT;
static KhdaysCampView shown;
static u32 seen_at;
static int seen;
/* the game's thread's own */
static KhdaysCampField fields[KHDAYS_CAMP_FIELDS];
static int trace = -1;
/* the Preferences page as last read, for the buttons (the game's thread): each setting's tab and
 * entry, each tab's entries the cursor stops on */
static struct {
    int on;
    u32 state, camp, seen_at; /* read last at that VBlank: gone after SEEN_FRAMES without */
    int tabs;
    int tab_of[KHDAYS_CAMP_SETTINGS], entry_of[KHDAYS_CAMP_SETTINGS];
    int stops[KHDAYS_CAMP_SECTIONS][KHDAYS_CAMP_SETTINGS], stop_count[KHDAYS_CAMP_SECTIONS];
    KhdaysCampPage page;
} config;
static volatile LONG page_drawn;
static struct {
    int on, tab, entry, rest, presses;
    u16 held;
    int dir, repeat_count;
    u32 repeat_from;
} steer;
static int last_screen = -1, last_page = -1, last_entry = -1, last_tab = -1, last_config_entry = -1, last_notice = -1;
static int last_member_page[KHDAYS_CAMP_PLACES] = {-2, -2, -2, -2};

static u32 rd32(u32 address)
{
    return *(const volatile u32 *)KHDAYS_W(address);
}

static u16 rd16(u32 address)
{
    return *(const volatile u16 *)KHDAYS_W(address);
}

static u8 rd8(u32 address)
{
    return *(const volatile u8 *)KHDAYS_W(address);
}

static int in_ram(u32 address)
{
    return khdays_game_ram(address); /* main RAM or the game's heaps */
}

static int tracing(void)
{
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_CAMP") != NULL;
    }
    return trace;
}

/* The game's fade: the upper screen's master brightness, the lower's while it is dark (the
 * one-screen picture's default, KOB_DEFAULT). */
static int16_t brightness(void)
{
    const u16 upper = IO16(0x006c), lower = IO16(0x106c);
    const u16 bright = (lower >> 14) == 2 ? lower : upper;
    const int mode = bright >> 14, factor = (bright & 0x1f) > 16 ? 16 : bright & 0x1f;
    return (int16_t)(mode == 1 ? factor : mode == 2 ? -factor : 0);
}

/* ---- The status panel ------------------------------------------------------------------------- */

void khdays_camp_field(void *panel, int field, unsigned int delta, int style, void *values, int animate)
{
    const u32 *v = (const u32 *)values;
    const u8 *p = (const u8 *)panel;
    u32 a, b = 0;
    (void)style;
    if (field < 0 || field >= KHDAYS_CAMP_FIELDS || v == NULL) {
        return;
    }
    a = v[0];
    switch (field) {
    case 7:
    case 9:
    case 0xb:
    case 0xd: {
        /* the value, with the panel's animated offset while it moves; capped as drawn */
        static const int offset_of[0xe] = {[7] = 0, [9] = 1, [0xb] = 2, [0xd] = 3};
        if (animate) {
            a += (u32)(int)*(const s16 *)(p + OFFSETS + 2u * (u32)offset_of[field]);
        }
        if ((int)a > (field == 0xd ? 0xff : 999)) {
            a = field == 0xd ? 0xff : 999;
        }
        break;
    }
    case 0xc:
        if (animate) {
            a += *(const u32 *)(p + OFFSET_Q12);
        }
        if ((int)a > 0x64000) {
            a = 0x64000;
        }
        a = (u32)((int)a >> 12);
        break;
    case 0x11: {
        /* the pair, its second capped while the game's count is over (Ov008_DrawStatusField) */
        const u32 state = rd32(GAME_STATE);
        b = v[1];
        if (in_ram(state) && rd8(state + ITEM_COUNTS + 1u) + 0xfu > 0x78u) {
            b = 0x78;
        }
        break;
    }
    case 0x12: b = v[1]; break;
    default: break;
    }
    fields[field].has = 1;
    fields[field].value[0] = a;
    fields[field].value[1] = b;
    if (tracing()) {
        khdays_log("camp: field 0x%x = %u %u (delta %u, raw 0x%08x 0x%08x, animate %d)", field, a, b, delta, v[0], v[1],
                   animate);
    }
}

/* ---- The game's menu -------------------------------------------------------------------------- */

/* The group's members, as Ov008_Menu_RefreshSlotPanel shows them. */
static void read_members(KhdaysCampView *v)
{
    const u32 heap = rd32(HEAP), session = rd32(SESSION);
    u32 mask;
    u16 named = 0;
    if (!in_ram(heap)) {
        return;
    }
    mask = rd16(heap + MASK);
    if (in_ram(session)) {
        named = rd16(session + NAMES_PRESENT);
    }
    for (int i = 0; i < KHDAYS_CAMP_PLACES; ++i) {
        KhdaysCampMember *m = &v->member[i];
        const u32 record = heap + RECORDS + (u32)i * RECORD_SIZE;
        const int page = rd8(record + RECORD_PAGE);
        m->present = ((mask >> i) & 1u) && page != PAGE_GONE;
        m->page = page; /* as the DS's tag shows it (Ov008_Menu_RefreshSlotPanel: the record's +3 alone) */
        m->character = -1;
        if (in_ram(session)) {
            const int c = (int)(s8)rd8(session + ROWS + (u32)i * ROW_SIZE + ROW_CHARACTER);
            m->character = c >= 0 && c < KHDAYS_CAMP_CHARACTERS ? c : -1;
            if ((named >> i) & 1u) {
                for (u32 k = 0; k < NAME_LENGTH; ++k) {
                    m->name[k] = rd16(session + NAMES + ((u32)i * NAME_LENGTH + k) * 2u);
                }
            }
        }
        if (tracing() && (m->present ? m->page : -1) != last_member_page[i]) {
            khdays_log("camp: member %d %s, page %d, character %d", i, m->present ? "there" : "not there", m->page,
                       m->character);
        }
        last_member_page[i] = m->present ? m->page : -1;
    }
}

/* Title Screen's question, its answers 7 and 8: the option stays Title Screen, the question in
 * its description's place. */
static int answer(int entry, KhdaysCampView *v)
{
    if (entry != ANSWER_YES && entry != ANSWER_NO) {
        return 0;
    }
    v->question = 1;
    v->yes = entry == ANSWER_YES;
    v->selected = TITLE_SCREEN;
    v->description = QUIT_QUESTION;
    return 1;
}

/* The option selected and its description: Mission Mode's entries 101-107 (the description the
 * game set, +0x5c4), the story's ids 1-6 and 9 and its options' own while no question is open;
 * either's question's answers; 0 otherwise (the game's screen shows). */
/* Mission Mode's description as Ov008_DrawMenuValue draws it: its record, but "stay waiting for
 * your mission" while the mission is accepted and the session not ready. */
static int mission_description(u32 state)
{
    if (GameState_IsFlagSet(MISSION_ACCEPTED) && !Ov008_IsSessionReady()) {
        return WAIT_FOR_MISSION;
    }
    return (int)rd16(state + DESCRIPTION);
}

static int selection(const Camp *c, u32 state, KhdaysCampView *v)
{
    const int entry = (int)rd32(state + ENTRY);
    if (!c->story) {
        const int notice = (int)rd16(state + NOTICE);
        v->notice = notice >= NOTICE_FIRST && notice <= NOTICE_LAST ? notice : -1;
        if (answer(entry, v)) {
            return 1;
        }
        if (entry == WAITING) {
            v->waiting = 1;
            v->selected = -1;
            v->description = mission_description(state);
            return 1;
        }
        if (entry < FIRST_OPTION || entry >= FIRST_OPTION + KHDAYS_CAMP_OPTIONS) {
            return 0;
        }
        v->selected = entry - FIRST_OPTION;
        v->description = mission_description(state);
        return 1;
    }
    if (rd32(state + STORY_QUESTION) != 0) {
        return answer(entry, v);
    }
    for (int i = 0; i < KHDAYS_CAMP_OPTIONS; ++i) {
        if (STORY_ENTRIES[i] == entry) {
            v->selected = i;
            v->description = STORY_DESCRIPTIONS[i];
            return 1;
        }
    }
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < 2; ++i) {
            if (STORY_SUB_ENTRIES[s][i] == entry) {
                v->sub = s + 1;
                v->selected = i;
                v->description = STORY_SUB_DESCRIPTIONS[s][i];
                return 1;
            }
        }
    }
    return 0;
}

/* A tab's title (see above): its heading's name, or the game's for its page. */
static int tab_title(const Camp *c, int list, int tab)
{
    const u32 page = c->pages + PAGE_SIZE * rd8(c->lists + LIST_SIZE * (u32)list + 3u + (u32)tab);
    const u32 first = c->entries + ENTRY_SIZE * rd16(page);
    if (rd8(first + 7u) == 0) {
        return rd8(first + 2u);
    }
    if (list != 0) {
        return c->tab_title(list, (unsigned)tab, 0);
    }
    /* the story's: the title of the same page in a list that has titles */
    for (int l = 1; l < LISTS; ++l) {
        const u32 other = c->lists + LIST_SIZE * (u32)l;
        for (int t = 0; t < rd8(other + 2u) && t < KHDAYS_CAMP_SECTIONS; ++t) {
            if (rd8(other + 3u + (u32)t) == rd8(c->lists + LIST_SIZE * (u32)list + 3u + (u32)tab)) {
                return c->tab_title(l, (unsigned)t, 0);
            }
        }
    }
    return TITLE_SETTINGS;
}

/* The Preferences page from the game's state and tables: 0 when it is not as expected. */
static int read_config(const Camp *c, u32 state, KhdaysCampView *v)
{
    KhdaysCampPage *pg = &v->page;
    const int list = rd16(state + CONFIG_LIST), tab = (s16)rd16(state + CONFIG_TAB);
    const int entry = (s16)rd16(state + CONFIG_ENTRY);
    u32 tabs;
    int last_title = -1;
    if (list < 0 || list >= LISTS) {
        return 0;
    }
    tabs = rd8(c->lists + LIST_SIZE * (u32)list + 2u);
    if (tabs == 0 || tabs > KHDAYS_CAMP_SECTIONS || tab < 0 || (u32)tab >= tabs) {
        return 0;
    }
    memset(pg, 0, sizeof(*pg));
    pg->chosen = -1;
    config.tabs = (int)tabs;
    for (u32 t = 0; t < tabs; ++t) {
        const u32 page = c->pages + PAGE_SIZE * rd8(c->lists + LIST_SIZE * (u32)list + 3u + t);
        const int first = rd16(page), count = rd16(page + 2u);
        const int title = tab_title(c, list, (int)t);
        config.stop_count[t] = 0;
        if (first < 0 || first + count > KHDAYS_CAMP_SETTINGS) {
            return 0;
        }
        if (title != last_title) {
            pg->title[pg->sections] = title;
            pg->first[pg->sections] = pg->settings;
            ++pg->sections;
            last_title = title;
        }
        if ((int)t == tab) {
            pg->section = pg->sections - 1;
        }
        for (int e = first; e < first + count; ++e) {
            const u32 at = c->entries + ENTRY_SIZE * (u32)e;
            KhdaysCampSetting *s = &pg->setting[pg->settings];
            const int values = rd8(at + 7u);
            if (values == 0) {
                continue; /* a heading: the section's title */
            }
            s->enabled = rd8(at + 6u) != 0;
            s->name = rd8(at + 2u);
            s->help = rd8(at + 3u);
            s->values = values < KHDAYS_CAMP_VALUES ? values : KHDAYS_CAMP_VALUES;
            for (int i = 0; i < s->values; ++i) {
                s->value_name[i] = rd8(at + 8u + 4u * (u32)i + 2u);
                s->value_help[i] = rd8(at + 8u + 4u * (u32)i + 3u);
            }
            s->value = (s16)rd16(state + CONFIG_VALUES + 2u * (u32)e);
            config.tab_of[pg->settings] = (int)t;
            config.entry_of[pg->settings] = e;
            config.stops[t][config.stop_count[t]++] = e;
            if ((int)t == tab && e == entry) {
                pg->chosen = pg->settings;
            }
            ++pg->settings;
            ++pg->count[pg->sections - 1];
        }
    }
    return 1;
}

/* Whether the entry `id` of the context's list is locked (see above): 0 when it is not there. */
static int entry_locked(u32 camp, int id)
{
    const u32 list = camp + ENTRY_LIST;
    const u32 link = rd16(list + 0xau), count = rd16(list + 8u); /* its numObjects */
    u32 at = rd32(list);
    for (u32 n = 0; n < count && in_ram(at); ++n) {
        if ((int)rd32(at + ENTRY_ID) == id) {
            return (rd8(at + ENTRY_ALTERNATE) & 1u) != 0;
        }
        at = rd32(at + link + 4u);
    }
    return 0;
}

/* The rows shown's locks: the story's options or the two of its option open, Mission Mode's. */
static void read_locks(const Camp *c, u32 camp, KhdaysCampView *v)
{
    const int rows = v->sub != 0 ? 2 : KHDAYS_CAMP_OPTIONS;
    for (int i = 0; i < rows; ++i) {
        const int id = !c->story ? FIRST_OPTION + i : v->sub != 0 ? STORY_SUB_ENTRIES[v->sub - 1][i] : STORY_ENTRIES[i];
        v->locked[i] = entry_locked(camp, id);
    }
    if (tracing()) {
        static int last = -1;
        int bits = 0;
        for (int i = 0; i < rows; ++i) {
            bits |= v->locked[i] << i;
        }
        if (bits != last) {
            khdays_log("camp: ov%03d locked options %02x (bit 0 the first row)", c->overlay, (unsigned)bits);
            last = bits;
        }
    }
}

/* While the port takes the game's cursor to a setting (khdays_camp_buttons), the page shows that
 * setting as the chosen one: not the tabs and settings the game's cursor passes on its way. */
static void show_steer_target(KhdaysCampPage *pg)
{
    for (int n = 0; n < pg->settings; ++n) {
        if (config.tab_of[n] == steer.tab && config.entry_of[n] == steer.entry) {
            pg->chosen = n;
            for (int k = 0; k < pg->sections; ++k) {
                if (n >= pg->first[k] && n < pg->first[k] + pg->count[k]) {
                    pg->section = k;
                }
            }
            return;
        }
    }
}

static int read_menu(const Camp *c, KhdaysCampView *v)
{
    const int screen = (int)rd32(c->screen_index), page = (int)rd32(c->screen_index + 4u);
    const u32 camp = rd32(c->camp + 4u);
    u32 state = 0, game = rd32(GAME_STATE);
    int entry = -1;
    memset(v, 0, sizeof(*v));
    v->notice = -1;
    if (in_ram(camp)) {
        state = rd32(camp + SCREEN_STATE);
    }
    if (screen == 0 && in_ram(state)) {
        entry = (int)rd32(state + ENTRY);
    }
    if (tracing() && (screen != last_screen || page != last_page || entry != last_entry)) {
        khdays_log("camp: ov%03d screen %d, page %d, entry %d, %s %d", c->overlay, screen, page, entry,
                   c->story ? "question" : "description",
                   screen == 0 && in_ram(state)
                       ? (int)(c->story ? rd32(state + STORY_QUESTION) : rd16(state + DESCRIPTION))
                       : -1);
    }
    last_screen = screen;
    last_page = page;
    last_entry = entry;
    if (tracing() && !c->story && screen == 0 && in_ram(state) && (int)rd16(state + NOTICE) != last_notice) {
        last_notice = (int)rd16(state + NOTICE);
        khdays_log("camp: ov%03d notice %d", c->overlay, last_notice);
    }
    if (tracing() && screen == CONFIG_SCREEN && in_ram(state) &&
        ((s16)rd16(state + CONFIG_TAB) != last_tab || (s16)rd16(state + CONFIG_ENTRY) != last_config_entry)) {
        last_tab = (s16)rd16(state + CONFIG_TAB);
        last_config_entry = (s16)rd16(state + CONFIG_ENTRY);
        khdays_log("camp: ov%03d preferences, list %u, tab %d, entry %d, its value %d", c->overlay,
                   rd16(state + CONFIG_LIST), last_tab, last_config_entry,
                   last_config_entry >= 0 && last_config_entry < KHDAYS_CAMP_SETTINGS
                       ? (s16)rd16(state + CONFIG_VALUES + 2u * (u32)last_config_entry)
                       : -1);
    }
    config.on = 0;
    if (screen == CONFIG_SCREEN && page == 0 && in_ram(state) && in_ram(game) && read_config(c, state, v)) {
        v->config = 1;
        v->selected = PREFERENCES;
        config.on = 1;
        config.seen_at = khdays_events_vblank_count();
        config.state = state;
        config.camp = camp;
        config.page = v->page;
        if (steer.on) {
            show_steer_target(&v->page);
        }
    } else if (screen != 0 || page != 0 || !in_ram(state) || !in_ram(game) || !selection(c, state, v)) {
        return 0;
    }
    v->shown = 1;
    read_locks(c, camp, v);
    v->story = c->story;
    v->language = khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage();
    v->master = brightness();
    v->munny = rd32(game + MUNNY);
    v->hearts = rd32(game + HEARTS);
    v->character = -1;
    if (c->story) {
        /* the story's: Roxas, unless the game gives its slot's character (Ov025_GetLocalPlayerCharacter) */
        const int ch = Ov025_GetLocalPlayerCharacter(0);
        v->day = GameState_GetField(0, 9);
        v->difficulty = GameState_GetField(0x40a, 2);
        v->character = ch > 0 && ch < KHDAYS_CAMP_CHARACTERS ? ch : ROXAS;
    } else {
        const u32 session = rd32(SESSION);
        v->self = Ov008_GetLocalPlayerIndex() & 0xff;
        if (in_ram(session) && v->self < 4) {
            const int ch = (int)(s8)rd8(session + ROWS + (u32)v->self * ROW_SIZE + ROW_CHARACTER);
            v->character = ch >= 0 && ch < KHDAYS_CAMP_CHARACTERS ? ch : -1;
        }
        read_members(v);
    }
    memcpy(v->field, fields, sizeof(fields));
    return 1;
}

void khdays_camp_update_ran(uintptr_t update)
{
    KhdaysCampView v;
    const Camp *c = NULL;
    int loaded = 0;
    for (int i = 0; i < CAMPS; ++i) {
        if (khdays_overlay_loaded(camps[i].overlay)) {
            loaded = 1;
            if (update == (uintptr_t)camps[i].idle) {
                c = &camps[i];
            }
        }
    }
    if (!loaded && (seen || fields[0x13].has || fields[7].has)) {
        memset(fields, 0, sizeof(fields)); /* a camp of another time draws its own */
    }
    if (c == NULL) {
        if (seen && khdays_events_vblank_count() - seen_at > SEEN_FRAMES) {
            seen = 0;
            AcquireSRWLockExclusive(&lock);
            memset(&shown, 0, sizeof(shown));
            ReleaseSRWLockExclusive(&lock);
        }
        return;
    }
    read_menu(c, &v);
    seen = 1;
    seen_at = khdays_events_vblank_count();
    AcquireSRWLockExclusive(&lock);
    shown = v;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_camp_view(KhdaysCampView *out)
{
    AcquireSRWLockShared(&lock);
    *out = shown;
    ReleaseSRWLockShared(&lock);
}

int khdays_camp_screen(int *story)
{
    for (int i = 0; i < CAMPS; ++i) {
        if (khdays_overlay_loaded(camps[i].overlay)) {
            *story = camps[i].story;
            return (int)rd32(camps[i].screen_index);
        }
    }
    *story = 0;
    return KHDAYS_CAMP_NONE;
}

void khdays_camp_font_scope(uint32_t font, char *scope, int size)
{
    if (size <= 0)
        return;
    scope[0] = 0;
    const struct {
        int overlay;
        u32 global;
    } shops[] = {{8, 0x02090facu}, {26, 0x02091368u}};
    for (int i = 0; i < 2; ++i) {
        if (!khdays_overlay_loaded(shops[i].overlay))
            continue;
        const u32 context = rd32(shops[i].global);
        if (in_ram(context) && font >= context + 0xc13cu && font <= context + 0xc154u &&
            (font - context - 0xc13cu) % 12u == 0) {
            snprintf(scope, (size_t)size, "shop");
            return;
        }
    }
    for (int i = 0; i < CAMPS; ++i) {
        if (!khdays_overlay_loaded(camps[i].overlay))
            continue;
        const u32 context = rd32(camps[i].camp + 4);
        if (!in_ram(context) || font < context + 0x9680u || font > context + 0x96a4u ||
            (font - context - 0x9680u) % 12u != 0)
            continue;
        const int screen = (int)rd32(camps[i].screen_index);
        if (screen == 7)
            snprintf(scope, (size_t)size, "camp.%s.screen.7.mode.%u", camps[i].story ? "story" : "mission",
                     rd32(context + 0x9768u));
        else
            snprintf(scope, (size_t)size, "camp.%s.screen.%d", camps[i].story ? "story" : "mission", screen);
        return;
    }
    /* Standalone camp save uses the same four slots in ov009's context. */
    if (khdays_overlay_loaded(9)) {
        const u32 context = rd32(0x020563e8u);
        if (in_ram(context) && font >= context + 0x9680u && font <= context + 0x96a4u &&
            (font - context - 0x9680u) % 12u == 0)
            snprintf(scope, (size_t)size, "camp.save");
    }
}

/* ---- The Preferences page's buttons ----------------------------------------------------------- */

void khdays_camp_set_page_drawn(int drawn)
{
    InterlockedExchange(&page_drawn, drawn ? 1 : 0);
}

/* The direction this frame: a press, then the camp's key repeat (savelist.c's). */
static int page_direction(u16 buttons)
{
    const int dir = (buttons & KHDAYS_KEY_UP) ? -1 : (buttons & KHDAYS_KEY_DOWN) ? 1 : 0;
    const u32 now = khdays_events_vblank_count();
    const int mode = GetFrameRateMode();
    const u32 speed = mode == 0 ? 2u : mode == 1 ? 3u : 1u;
    const u32 repeat = config.camp + CAMP_REPEAT;
    const u32 delay = in_ram(config.camp) ? (u32)rd16(repeat + REPEAT_DELAY) * speed : 0;
    const u32 interval = in_ram(config.camp) ? (u32)rd16(repeat + REPEAT_INTERVAL) * speed : 0;
    u32 elapsed;
    if (dir == 0) {
        steer.dir = 0;
        return 0;
    }
    if (dir != steer.dir) {
        steer.dir = dir;
        steer.repeat_from = now;
        steer.repeat_count = 0;
        return dir;
    }
    elapsed = now - steer.repeat_from;
    if (interval == 0 || elapsed < delay) {
        return 0;
    }
    elapsed -= delay;
    if (elapsed >= interval * (u32)steer.repeat_count) {
        steer.repeat_count = (int)(elapsed / interval + 1u);
        return dir;
    }
    return 0;
}

/* Where an entry is among its tab's stops, -1 none. */
static int stop_index(int tab, int entry)
{
    for (int i = 0; i < config.stop_count[tab]; ++i) {
        if (config.stops[tab][i] == entry) {
            return i;
        }
    }
    return -1;
}

static void steer_to(int setting)
{
    steer.on = 1;
    steer.tab = config.tab_of[setting];
    steer.entry = config.entry_of[setting];
    steer.rest = 0;
    /* the farthest way: round the tabs, then round a tab's stops */
    steer.presses = config.tabs + KHDAYS_CAMP_SETTINGS + 2;
}

/* A press of the game's on the way (a frame between two), 0 there or resting. */
static u16 steer_step(void)
{
    const int tab = (s16)rd16(config.state + CONFIG_TAB), entry = (s16)rd16(config.state + CONFIG_ENTRY);
    if ((tab == steer.tab && entry == steer.entry) || steer.presses <= 0 || tab < 0 || tab >= config.tabs) {
        steer.on = 0;
        return 0;
    }
    if (steer.rest) {
        steer.rest = 0;
        return 0;
    }
    steer.rest = 1;
    --steer.presses;
    if (tab != steer.tab) {
        const int d = (steer.tab - tab + config.tabs) % config.tabs;
        return d <= config.tabs / 2 ? KHDAYS_KEY_R : KHDAYS_KEY_L;
    }
    {
        const int n = config.stop_count[tab], at = stop_index(tab, entry), to = stop_index(tab, steer.entry);
        if (n == 0 || at < 0 || to < 0) {
            steer.on = 0;
            return 0;
        }
        return (to - at + n) % n <= n / 2 ? KHDAYS_KEY_DOWN : KHDAYS_KEY_UP;
    }
}

uint16_t khdays_camp_buttons(uint16_t buttons)
{
    const u16 pressed = (u16)(buttons & ~steer.held);
    const KhdaysCampPage *pg = &config.page;
    u16 out = buttons;
    steer.held = buttons;
    if (!config.on || khdays_events_vblank_count() - config.seen_at > SEEN_FRAMES ||
        InterlockedCompareExchange(&page_drawn, 0, 0) == 0 || pg->chosen < 0) {
        steer.on = 0;
        steer.dir = 0;
        return buttons;
    }
    out = (u16)(buttons & ~(KHDAYS_KEY_UP | KHDAYS_KEY_DOWN | KHDAYS_KEY_L | KHDAYS_KEY_R));
    if (steer.on) {
        /* on the way the player's buttons wait */
        return steer_step();
    }
    {
        const int s = pg->section, first = pg->first[s], count = pg->count[s];
        const int dir = page_direction(buttons);
        int to = -1;
        if (dir != 0 && count > 0) {
            to = first + (pg->chosen - first + dir + count) % count; /* round the section */
        } else if (pressed & (KHDAYS_KEY_L | KHDAYS_KEY_R)) {
            const int next = (s + ((pressed & KHDAYS_KEY_R) ? 1 : pg->sections - 1)) % pg->sections;
            to = pg->count[next] > 0 ? pg->first[next] : -1;
        }
        if (to >= 0 && to != pg->chosen) {
            steer_to(to);
            return steer_step();
        }
    }
    return out;
}
