/* Gameplay mods' scripts (runtime/scripts) against a game of the test's own: SHA-256, manifests, the
 * load order, the sandbox, a stopped mod beside a running one, an endless loop paused, the events and
 * the state names. Each case writes its mods under a folder of its own, removed at the end. */
#include "../runtime/core/sha256.h"
#include "../runtime/scripts/folders.h"
#include "../runtime/scripts/game_link.h"
#include "../runtime/scripts/manifest.h"
#include "../runtime/scripts/mod.h"
#include "../runtime/scripts/order.h"
#include "../runtime/scripts/queue.h"
#include "../runtime/scripts/scripts.h"
#include "../runtime/scripts/view.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                            \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* ---- the game, as the test has it ---------------------------------------------------------------- */

static unsigned fake_scene = 1, fake_frame, fake_state = 0x02100000u;
static unsigned char fake_items[KHDAYS_LINK_ITEMS];
static unsigned char fake_flags[KHDAYS_LINK_FLAGS / 8];
static int fake_munny, fake_guest;

void khdays_log(const char *format, ...)
{
    (void)format;
}

void khdays_link_folders(const char **mods, const char **user)
{
    *mods = "unused";
    *user = "unused";
}

unsigned khdays_link_frame(void)
{
    return fake_frame;
}

int khdays_link_paused(void)
{
    return 0;
}

unsigned khdays_link_scene(unsigned *argument)
{
    if (argument != NULL) {
        *argument = 7;
    }
    return fake_scene;
}

unsigned khdays_link_state(void)
{
    return fake_state;
}

int khdays_link_munny(void)
{
    return fake_munny;
}

void khdays_link_set_munny(int value)
{
    fake_munny = value;
}

int khdays_link_items(unsigned char *counts)
{
    if (fake_state == 0) {
        return 0;
    }
    memcpy(counts, fake_items, sizeof(fake_items));
    return 1;
}

int khdays_link_flag(int n)
{
    return (fake_flags[n / 8] >> (n % 8)) & 1;
}

/* The port's autosave hears the mission's end too (runtime/menus/autosave.h): not under test here. */
void khdays_autosave_mission_end(void) {}

void khdays_link_set_flag(int n, int on)
{
    if (on) {
        fake_flags[n / 8] |= (unsigned char)(1 << (n % 8));
    } else {
        fake_flags[n / 8] &= (unsigned char)~(1 << (n % 8));
    }
}

void khdays_link_set_item(int id, int count)
{
    fake_items[id] = (unsigned char)count;
    if (count > 0) {
        khdays_link_set_flag(0x4db + id, 1);
    }
}

int khdays_link_mission(void)
{
    return fake_scene == 2 ? 15 : -1;
}

int khdays_link_position(float at[3])
{
    at[0] = 1.5f;
    at[1] = 0;
    at[2] = -2;
    return fake_scene == 2;
}

static int fake_hp = 40, fake_hp_max = 50;

int khdays_link_player_hp(int *hp, int *max)
{
    *hp = fake_hp;
    *max = fake_hp_max;
    return fake_scene == 2;
}

int khdays_link_set_player_hp(int hp)
{
    fake_hp = hp > fake_hp_max ? fake_hp_max : hp;
    return fake_scene == 2;
}

void khdays_link_entity_hp(const void *entity, int *hp, int *max)
{
    const int *e = entity;
    *hp = e[0];
    *max = e[1];
}

int khdays_link_is_player(const void *entity)
{
    return ((const int *)entity)[2];
}

void khdays_link_trace_frame(void) {}

void khdays_link_trace_hp(const void *entity, int before)
{
    (void)entity;
    (void)before;
}

static char fake_sound[64], fake_notice[64];
static unsigned int fake_mods_fingerprint, fake_mods_count;
static unsigned char fake_mods_list[256];

void khdays_net_set_mods(unsigned int fingerprint, const unsigned char *list, unsigned int size, unsigned int count)
{
    fake_mods_fingerprint = fingerprint;
    fake_mods_count = count;
    memset(fake_mods_list, 0, sizeof(fake_mods_list));
    memcpy(fake_mods_list, list, size < sizeof(fake_mods_list) ? size : sizeof(fake_mods_list));
}

const char *khdays_link_play_sound(const char *sequence_archive, const char *entry)
{
    if (strcmp(sequence_archive, "COMMON_SYS") != 0) {
        return "unknown sound";
    }
    snprintf(fake_sound, sizeof(fake_sound), "%s/%s", sequence_archive, entry);
    return NULL;
}

const char *khdays_link_show_text(const unsigned short *text, int length)
{
    if (fake_scene != 2) {
        return "not in the field";
    }
    if (fake_notice[0] != 0) {
        return "busy";
    }
    for (int i = 0; i < length && i < (int)sizeof(fake_notice) - 1; ++i) {
        fake_notice[i] = text[i] < 0x80 ? (char)text[i] : '?';
    }
    return NULL;
}

int khdays_link_menu_open(int menu)
{
    (void)menu;
    return 0;
}

int khdays_link_guest(void)
{
    return fake_guest;
}

static void reset_game(void)
{
    fake_scene = 1;
    fake_frame = 0;
    fake_state = 0x02100000u;
    fake_munny = 0;
    fake_guest = 0;
    fake_hp = 40;
    fake_sound[0] = fake_notice[0] = 0;
    memset(fake_items, 0, sizeof(fake_items));
    memset(fake_flags, 0, sizeof(fake_flags));
}

/* ---- the cases' folders ----------------------------------------------------------------------------- */

static char root[512];

static void put(const char *relative, const char *text)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", root, relative);
    CHECK(khdays_folders_write(path, text, strlen(text)));
}

/* A mod folder with its mod.ini and main script. */
static void mod_folder(const char *folder, const char *id, const char *kind, const char *main_lua)
{
    char path[512], ini[512];
    snprintf(ini, sizeof(ini), "id = %s\nname = %s\nversion = 1.0\nkind = %s\napi = 1\n", id, id, kind);
    snprintf(path, sizeof(path), "mods/%s/mod.ini", folder);
    put(path, ini);
    snprintf(path, sizeof(path), "mods/%s/scripts/main.lua", folder);
    put(path, main_lua);
}

static void remove_all(const char *path)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        return;
    }
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        int n = 0;
        char **names = SDL_GlobDirectory(path, NULL, 0, &n);
        for (int i = 0; names != NULL && i < n; ++i) {
            char child[1024];
            snprintf(child, sizeof(child), "%s/%s", path, names[i]);
            remove_all(child);
        }
        SDL_free(names);
    }
    SDL_RemovePath(path);
}

static void begin_case(const char *name)
{
    snprintf(root, sizeof(root), "script_test_%s", name);
    remove_all(root);
    reset_game();
}

static void start(void)
{
    char mods[600], user[600];
    snprintf(mods, sizeof(mods), "%s/mods", root);
    snprintf(user, sizeof(user), "%s/saves", root);
    khdays_scripts_start_in(mods, user);
}

static void end_case(void)
{
    khdays_scripts_stop();
    remove_all(root);
}

static KhdaysMod *mod_by_folder(const char *folder)
{
    for (int i = 0; i < khdays_scripts_mod_count(); ++i) {
        if (strcmp(khdays_scripts_mod(i)->folder, folder) == 0) {
            return khdays_scripts_mod(i);
        }
    }
    return NULL;
}

static void frames(int n)
{
    for (int i = 0; i < n; ++i) {
        ++fake_frame;
        khdays_scripts_frame();
    }
}

static int log_has(const char *text)
{
    char path[600];
    char *log;
    int found;
    snprintf(path, sizeof(path), "%s/saves/logs/mods.log", root);
    log = khdays_folders_read(path, NULL);
    found = log != NULL && strstr(log, text) != NULL;
    free(log);
    return found;
}

/* ---- the cases ----------------------------------------------------------------------------------- */

static void hex(const unsigned char *d, char *out)
{
    for (int i = 0; i < 32; ++i) {
        snprintf(out + i * 2, 3, "%02x", d[i]);
    }
}

static void test_sha256(void)
{
    unsigned char d[32];
    char h[65];
    const char *million = NULL;
    khdays_sha256("", 0, d);
    hex(d, h);
    CHECK(strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);
    khdays_sha256("abc", 3, d);
    hex(d, h);
    CHECK(strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    khdays_sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, d);
    hex(d, h);
    CHECK(strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);
    {
        KhdaysSha256 s;
        char a[1000];
        memset(a, 'a', sizeof(a));
        khdays_sha256_begin(&s);
        for (int i = 0; i < 1000; ++i) {
            khdays_sha256_add(&s, a, sizeof(a));
        }
        khdays_sha256_end(&s, d);
        hex(d, h);
        million = "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
        CHECK(strcmp(h, million) == 0);
    }
}

static void test_manifest(void)
{
    KhdaysManifest m;
    char reason[200];
    const char *good = "\xef\xbb\xbf# a mod\nid = a.b-c_1\nname = A mod\nversion = 1.2\nauthor = me\n"
                       "kind = gameplay\napi = 1\nextra = kept\n";
    CHECK(khdays_manifest_parse(good, strlen(good), &m, reason, sizeof(reason)));
    CHECK(strcmp(m.id, "a.b-c_1") == 0 && strcmp(m.name, "A mod") == 0 && strcmp(m.main, "scripts/main.lua") == 0);
    CHECK(m.kind == KHDAYS_MOD_GAMEPLAY && m.api == 1 && strcmp(m.author, "me") == 0);
    khdays_manifest_free(&m);
    {
        static const struct {
            const char *text, *reason;
        } bad[] = {
            {"name = x\nversion = 1\nkind = visual\napi = 1\n", "mod.ini: id missing or not valid"},
            {"id = a b\nname = x\nversion = 1\nkind = visual\napi = 1\n", "mod.ini: id missing or not valid"},
            {"id = a\nversion = 1\nkind = visual\napi = 1\n", "mod.ini: name missing or not valid"},
            {"id = a\nname = x\nkind = visual\napi = 1\n", "mod.ini: version missing or not valid"},
            {"id = a\nname = x\nversion = 1\napi = 1\n", "mod.ini: kind missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = both\napi = 1\n", "mod.ini: kind missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = one\n", "mod.ini: api missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 1\nmain = ../x.lua\n",
             "mod.ini: main missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 1\nmain = scripts/../../x.lua\n",
             "mod.ini: main missing or not valid"},
            {"id = a\nname = x\nversion = 1\nkind = visual\napi = 1\nmain = C:/x.lua\n",
             "mod.ini: main missing or not valid"},
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            CHECK(!khdays_manifest_parse(bad[i].text, strlen(bad[i].text), &m, reason, sizeof(reason)));
            if (strcmp(reason, bad[i].reason) != 0) {
                fprintf(stderr, "case %u: '%s'\n", (unsigned)i, reason);
                ++failures;
            }
        }
    }
}

static void test_order(void)
{
    KhdaysOrder o;
    const char *found[] = {"Alpha", "Beta", "New"};
    char *text;
    khdays_order_parse(&o, "Beta = on\nGone = off\nAlpha = off\n");
    khdays_order_merge(&o, found, 3);
    text = khdays_order_text(&o);
    CHECK(text != NULL && strcmp(text, "Beta = on\nAlpha = off\nNew = on\nGone = off\n") == 0);
    free(text);
    khdays_order_move(&o, 3, -1); /* New (entry 3, after Gone) before Alpha: Gone is skipped, absent */
    text = khdays_order_text(&o);
    CHECK(text != NULL && strcmp(text, "Beta = on\nNew = on\nAlpha = off\nGone = off\n") == 0);
    free(text);
    khdays_order_free(&o);
}

static void test_no_mods(void)
{
    char log[600];
    begin_case("none");
    put("mods/Content only/textures/readme.txt", "a content mod");
    start();
    frames(3);
    CHECK(!khdays_scripts_any());
    snprintf(log, sizeof(log), "%s/saves/logs/mods.log", root);
    CHECK(!khdays_folders_is_file(log));
    end_case();
}

static void test_sandbox(void)
{
    begin_case("sandbox");
    mod_folder("Box", "test.box", "gameplay",
               "local ok = io == nil and package == nil and debug == nil and dofile == nil and loadfile == nil\n"
               "ok = ok and os.execute == nil and os.remove == nil and os.getenv == nil and os.time ~= nil\n"
               "ok = ok and load(string.dump(function() end)) == nil\n"
               "ok = ok and load('return 41 + 1')() == 42\n"
               "ok = ok and not pcall(require, '../x') and not pcall(require, 'a..b')\n"
               "ok = ok and require('lib.util').twice(4) == 8 and require('lib.util').loads == 1\n"
               "ok = ok and not pcall(string.rep, 'x', 100 * 1024 * 1024)\n"
               "ok = ok and not pcall(setmetatable, {}, { __gc = function() end })\n"
               "if ok then game.set_munny(1) end\n"
               "print('sandbox', ok)\n");
    put("mods/Box/scripts/lib/util.lua",
        "local n = (package_loads or 0) + 1\nreturn { twice = function(x) return x * 2 end, loads = n }\n");
    start();
    CHECK(fake_munny == 1);
    CHECK(mod_by_folder("Box") != NULL && mod_by_folder("Box")->state == KHDAYS_MOD_LOADED);
    CHECK(log_has("test.box print scripts/main.lua:10 sandbox\ttrue"));
    end_case();
}

static void test_faults(void)
{
    KhdaysMod *m;
    begin_case("faults");
    mod_folder("A broken", "test.broken", "gameplay",
               "mod.on('frame', function(e)\n  if e.frame == 2 then local x = nil; x.y = 1 end\nend)\n");
    mod_folder("B counter", "test.counter", "gameplay",
               "mod.on('frame', function() game.set_munny(game.munny + 1) end)\n");
    mod_folder("C loop", "test.loop", "gameplay", "mod.on('frame', function() while true do end end)\n");
    mod_folder("D caught loop", "test.caught", "gameplay",
               "mod.on('frame', function()\n  while true do pcall(function() while true do end end) end\nend)\n");
    put("mods/E newer/mod.ini", "id = test.newer\nname = Newer\nversion = 1\nkind = gameplay\napi = 2\n");
    put("mods/F same id/mod.ini", "id = test.counter\nname = Twin\nversion = 1\nkind = gameplay\napi = 1\n");
    put("mods/G no main/mod.ini", "id = test.nomain\nname = No main\nversion = 1\nkind = gameplay\napi = 1\n");
    put("mods/H syntax/mod.ini", "id = test.syntax\nname = Syntax\nversion = 1\nkind = gameplay\napi = 1\n");
    put("mods/H syntax/scripts/main.lua", "this is not lua\n");
    start();
    frames(4);
    m = mod_by_folder("A broken");
    CHECK(m != NULL && m->state == KHDAYS_MOD_STOPPED && m->reason != NULL &&
          strstr(m->reason, "scripts/main.lua:2: attempt to index") == m->reason);
    CHECK(fake_munny == 4); /* B ran every frame, A's error notwithstanding */
    m = mod_by_folder("C loop");
    CHECK(m != NULL && m->state == KHDAYS_MOD_PAUSED);
    m = mod_by_folder("D caught loop");
    CHECK(m != NULL && m->state == KHDAYS_MOD_PAUSED);
    m = mod_by_folder("E newer");
    CHECK(m != NULL && m->state == KHDAYS_MOD_REFUSED &&
          strcmp(m->reason, "needs mod interface 2, this port has 1") == 0);
    m = mod_by_folder("F same id");
    CHECK(m != NULL && m->state == KHDAYS_MOD_REFUSED && strcmp(m->reason, "same id as B counter") == 0);
    m = mod_by_folder("G no main");
    CHECK(m != NULL && m->state == KHDAYS_MOD_REFUSED &&
          strcmp(m->reason, "main script scripts/main.lua not found") == 0);
    m = mod_by_folder("H syntax");
    CHECK(m != NULL && m->state == KHDAYS_MOD_STOPPED);
    CHECK(log_has("test.broken error scripts/main.lua:2 attempt to index"));
    CHECK(log_has("test.broken stopped - the error above"));
    CHECK(log_has("test.loop paused - over its time allowance"));
    end_case();
}

static void test_order_file(void)
{
    begin_case("order");
    mod_folder("One", "test.one", "gameplay", "game.set_munny(game.munny * 10 + 1)\n");
    mod_folder("Two", "test.two", "gameplay", "game.set_munny(game.munny * 10 + 2)\n");
    mod_folder("Three", "test.three", "gameplay", "game.set_munny(game.munny * 10 + 3)\n");
    put("saves/mods/order.ini", "Two = on\nOne = off\n");
    start();
    CHECK(fake_munny == 23); /* Two, then Three (new, after the listed), One off */
    CHECK(mod_by_folder("One")->state == KHDAYS_MOD_OFF);
    end_case();
}

static void test_events_and_state(void)
{
    int entity[3] = {30, 50, 1};
    begin_case("events");
    mod_folder("Ears", "test.ears", "gameplay",
               "mod.on('title', function() game.set_flag(1, true) end)\n"
               "mod.on('scene', function(e)\n"
               "  if e.from == 1 and e.to == 2 and e.argument == 7 then game.set_flag(2, true) end\nend)\n"
               "mod.on('item', function(e)\n"
               "  if e.item == 5 and e.count == 3 and e.change == 3 then game.set_flag(3, true) end\n"
               "  if e.item == 9 then game.set_flag(9, true) end\nend)\n"
               "mod.on('munny', function(e)\n"
               "  if e.change == 10 and e.total == 10 then game.set_munny(e.total + e.change) end\nend)\n"
               "mod.on('hp', function(e)\n"
               "  if e.who == 'player' and e.before == 50 and e.after == 30 and e.max == 50 then\n"
               "    game.set_flag(4, true)\n  end\nend)\n"
               "mod.on('mission_end', function(e)\n"
               "  if e.mission == 15 and not e.given_up and e.rank == 2 and e.complete == true then\n"
               "    game.set_flag(5, true)\n  end\nend)\n"
               "local h = mod.on('frame', function() game.set_flag(6, not game.flag(6)) end)\n"
               "mod.on('frame', function(e) if e.frame == 4 then mod.off(h) end end)\n"
               "mod.on('save', function() game.set_flag(7, true) end)\n"
               "mod.on('mission_start', function(e) if e.mission == 15 then game.set_flag(8, true) end end)\n");
    mod_folder("Hands", "test.hands", "gameplay",
               "local a, b = game.set_item(0x8d0, 1)\n"
               "if a == nil and b == 'unknown item' then game.set_flag(20, true) end\n"
               "game.set_item(4, 150)\n"
               "if game.item(4) == 99 and game.flag(0x4db + 4) then game.set_flag(21, true) end\n"
               "game.set_munny(2000000)\n"
               "if game.munny == 999999 then game.set_flag(22, true) end\n"
               "game.set_munny(0)\n"
               "if mission.id == nil and scene.id == 1 and player.position() == nil and player.hp == nil and\n"
               "   select(2, player.set_hp(5)) == 'no player' then game.set_flag(23, true) end\n"
               "local f, why = game.flag(0x4000)\n"
               "if f == nil and why == 'unknown flag' then game.set_flag(24, true) end\n");
    mod_folder("Looks", "test.looks", "visual",
               "local a, b = game.set_munny(5)\n"
               "if a == nil and b == 'visual mod' then print('refused') end\n");
    start();
    frames(1); /* the title, and the items' first copy */
    CHECK(khdays_link_flag(1));
    CHECK(khdays_link_flag(20));
    CHECK(khdays_link_flag(21));
    CHECK(khdays_link_flag(22));
    CHECK(khdays_link_flag(23));
    CHECK(khdays_link_flag(24));
    CHECK(log_has("test.looks print scripts/main.lua:2 refused"));
    fake_items[9] = 4; /* in the title: a save loaded, nothing obtained */
    frames(1);
    CHECK(!khdays_link_flag(9));
    fake_scene = 2;
    frames(1);
    CHECK(khdays_link_flag(2));
    fake_items[5] = 3;
    fake_munny = 10;
    frames(1);
    CHECK(khdays_link_flag(3));
    CHECK(fake_munny == 20);
    khdays_scripts_hp(entity, 50);
    CHECK(khdays_link_flag(4));
    khdays_scripts_mission_end(15, 2, 1);
    CHECK(khdays_link_flag(5));
    khdays_scripts_mission_start(); /* no field made: not a mission's start */
    CHECK(!khdays_link_flag(8));
    khdays_scripts_field_begins();
    khdays_scripts_mission_start();
    CHECK(khdays_link_flag(8));
    khdays_link_set_flag(8, 0);
    khdays_scripts_mission_start(); /* the same mission's next run */
    CHECK(!khdays_link_flag(8));
    khdays_scripts_saved();
    CHECK(khdays_link_flag(7));
    {
        /* flag 6 toggles each frame until the frame 4 handler takes it off */
        int at4;
        frames(1);
        at4 = khdays_link_flag(6);
        frames(2);
        CHECK(khdays_link_flag(6) == at4);
    }
    end_case();
}

static void test_player(void)
{
    begin_case("player");
    mod_folder("Infinite", "test.infinite", "gameplay",
               "mod.on('frame', function()\n"
               "  local hp, max = player.hp, player.max_hp\n"
               "  if hp ~= nil and hp < max then player.set_hp(max + 100) end\nend)\n");
    start();
    frames(1);
    CHECK(fake_hp == 40); /* the title: no player */
    fake_scene = 2;
    frames(1);
    CHECK(fake_hp == 50); /* clamped to the maximum */
    end_case();
}

static void test_options(void)
{
    KhdaysModsView view;
    char path[600], *kept;
    memset(&view, 0, sizeof(view));
    begin_case("options");
    mod_folder("Options", "test.options", "gameplay",
               "local t = mod.option{ key = 'hp', kind = 'toggle', label = { en = 'Infinite HP', es = 'PV' },\n"
               "  on_change = function(v) game.set_flag(1, v) end }\n"
               "local c = mod.option{ key = 'speed', kind = 'choice', label = 'Speed', values = { 'a', 'b', 'c' },\n"
               "  default = 2, on_change = function(v) game.set_munny(v) end }\n"
               "local n = mod.option{ key = 'boost', kind = 'number', label = 'Boost', min = 0, max = 10, step = 2,\n"
               "  default = 4 }\n"
               "mod.option{ key = 'potion', kind = 'button', label = 'Give',\n"
               "  on_change = function() game.set_item(2, game.item(2) + 1) end }\n"
               "local bad = 0\n"
               "for _, o in ipairs({ { key = 'hp', kind = 'toggle', label = 'x' },\n"
               "                     { key = 'k', kind = 'nope', label = 'x' },\n"
               "                     { key = 'k', kind = 'choice', label = 'x', values = { 'one' } },\n"
               "                     { key = 'k', kind = 'number', label = 'x', min = 0, max = 1, step = 0.5 },\n"
               "                     { key = 'k', kind = 'toggle' } }) do\n"
               "  if not pcall(mod.option, o) then bad = bad + 1 end\n"
               "end\n"
               "if bad == 5 then game.set_flag(2, true) end\n"
               "if t.value == false and c.value == 2 then game.set_flag(4, true) end\n"
               "if t.value == true and c.value == 3 then game.set_flag(5, true) end\n"
               "if n.value == 4 then game.set_flag(6, true) end\n"
               "n:set(7)\n"
               "if n.value == 8 then game.set_flag(3, true) end\n");
    start();
    CHECK(khdays_link_flag(2) && khdays_link_flag(4) && khdays_link_flag(6) && !khdays_link_flag(5));
    CHECK(khdays_link_flag(3)); /* 7 on steps of 2 from 0: 8 */
    CHECK(khdays_scripts_view(&view) && view.count == 1 && view.mods[0].option_count == 4);
    CHECK(view.count == 1 && strcmp(khdays_scripts_text(view.mods[0].options[0].label, 5), "PV") == 0 &&
          strcmp(khdays_scripts_text(view.mods[0].options[0].label, 2), "Infinite HP") == 0);
    khdays_scripts_ask(KHDAYS_ASK_OPTION, "test.options", "hp", 1);
    khdays_scripts_ask(KHDAYS_ASK_OPTION, "test.options", "speed", 3);
    khdays_scripts_ask(KHDAYS_ASK_OPTION, "test.options", "speed", 9); /* not one of its values: dropped */
    khdays_scripts_ask(KHDAYS_ASK_BUTTON, "test.options", "potion", 0);
    frames(1);
    CHECK(khdays_link_flag(1) && fake_munny == 3 && fake_items[2] == 1);
    CHECK(khdays_scripts_view(&view) && view.mods[0].options[1].value == 3);
    snprintf(path, sizeof(path), "%s/saves/mods/test.options/options.ini", root);
    kept = khdays_folders_read(path, NULL);
    CHECK(kept != NULL && strstr(kept, "hp = 1\n") != NULL && strstr(kept, "speed = 3\n") != NULL &&
          strstr(kept, "boost = 8\n") != NULL && strstr(kept, "potion") == NULL);
    free(kept);
    /* the next start: the values kept; one out of its range back to its default */
    khdays_scripts_stop();
    put("saves/mods/test.options/options.ini", "hp = 1\nspeed = 3\nboost = 99\n");
    reset_game();
    start();
    CHECK(khdays_link_flag(5) && !khdays_link_flag(4)); /* the kept values */
    CHECK(khdays_link_flag(6));                         /* boost's 99 out of range: its default */
    khdays_scripts_view_free(&view);
    end_case();
}

static void test_menu_asks(void)
{
    KhdaysModsView view;
    char path[600], *order;
    memset(&view, 0, sizeof(view));
    begin_case("asks");
    mod_folder("A", "test.a", "gameplay", "game.set_munny(game.munny + 1)\n");
    mod_folder("B", "test.b", "gameplay", "mod.on('frame', function() while true do end end)\n");
    start();
    frames(1);
    CHECK(mod_by_folder("B")->state == KHDAYS_MOD_PAUSED);
    khdays_scripts_ask(KHDAYS_ASK_MOVE, "B", NULL, -1);
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "A", NULL, 0);
    frames(1);
    CHECK(khdays_scripts_mod(0) == mod_by_folder("B") && mod_by_folder("A")->state == KHDAYS_MOD_OFF);
    snprintf(path, sizeof(path), "%s/saves/mods/order.ini", root);
    order = khdays_folders_read(path, NULL);
    CHECK(order != NULL && strcmp(order, "B = on\nA = off\n") == 0);
    free(order);
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "A", NULL, 1);
    frames(1);
    CHECK(mod_by_folder("A")->state == KHDAYS_MOD_LOADED && fake_munny == 2); /* its main ran again */
    CHECK(khdays_scripts_view(&view) && view.count == 2 && strcmp(view.mods[0].folder, "B") == 0 &&
          view.mods[0].state == KHDAYS_MOD_PAUSED);
    khdays_scripts_ask(KHDAYS_ASK_RESUME, "test.b", NULL, 0);
    frames(1); /* resumed, and paused again by its loop */
    CHECK(mod_by_folder("B")->state == KHDAYS_MOD_PAUSED);
    CHECK(log_has("test.b resumed - by the player"));
    khdays_scripts_view_free(&view);
    end_case();
}

static void test_actions(void)
{
    begin_case("actions");
    mod_folder("Acts", "test.acts", "gameplay",
               "local function says(want, a, b) if a == nil and b == want then return true end\n"
               "  print('unexpected', a, b) return false end\n"
               "local n = 0\n"
               "local function ok(c) if c then n = n + 1 end end\n"
               "mod.on('frame', function(e)\n"
               "  if e.frame ~= 1 then return end\n"
               "  ok(game.give_item(3) and game.item(3) == 1 and game.flag(0x4db + 3))\n"
               "  ok(game.give_item(3, 500) and game.item(3) == 99)\n"
               "  ok(game.take_item(3, 40) and game.item(3) == 59 and game.take_item(3, 500) and game.item(3) == 0)\n"
               "  ok(says('unknown item', game.give_item(0x8d0)))\n"
               "  ok(not pcall(game.give_item, 3, -1))\n"
               "  ok(game.play_sound('COMMON_SYS', 'se_sys_open'))\n"
               "  ok(says('unknown sound', game.play_sound('NOPE', 'x')))\n"
               "  ok(says('not in the field', game.show_text('Hello')))\n"
               "  print('frame 1', n)\n"
               "end)\n"
               "mod.on('scene', function(e)\n"
               "  if e.to == 2 then\n"
               "    ok(game.show_text('Hola, \xc3\xb1"
               "and\xc3\xba') and says('busy', game.show_text('again')))\n"
               "    ok(says('not a text the game can show', game.show_text('bad\\1')))\n"
               "    print('field', n)\n"
               "  end\n"
               "end)\n");
    mod_folder("Looks", "test.looks", "visual",
               "mod.on('frame', function(e)\n"
               "  if e.frame == 1 then\n"
               "    local a, b = game.give_item(3)\n"
               "    print(a == nil and b == 'visual mod', game.play_sound('COMMON_SYS', 'se_sys_close'))\n"
               "  end\n"
               "end)\n");
    start();
    frames(1);
    CHECK(log_has("test.acts print scripts/main.lua:15 frame 1\t8"));
    CHECK(log_has("test.looks print scripts/main.lua:4 true\ttrue"));
    CHECK(strcmp(fake_sound, "COMMON_SYS/se_sys_close") == 0);
    fake_scene = 2;
    frames(1);
    CHECK(log_has("test.acts print scripts/main.lua:21 field\t10"));
    CHECK(strcmp(fake_notice, "Hola, ?and?") == 0);
    end_case();
}

/* Every notice waiting, dropped. */
static void drop_notices(void)
{
    int kind;
    char *mod, *text;
    while (khdays_scripts_take_notice(&kind, &mod, &text)) {
        free(mod);
        free(text);
    }
}

static void test_files_and_notices(void)
{
    char path[600], *text;
    int kind;
    char *mod, *said;
    begin_case("files");
    drop_notices();
    put("mods/Files/data/shipped.txt", "from the mod");
    mod_folder("Files", "test.files", "gameplay",
               "local n = 0\n"
               "local function ok(c) if c then n = n + 1 else print('not ok') end end\n"
               "ok(mod.read('shipped.txt') == 'from the mod')\n"
               "ok(mod.write('notes/kept.txt', 'kept'))\n"
               "ok(mod.read('notes/kept.txt') == 'kept')\n"
               "for _, bad in ipairs({ '../x', '/x', 'C:/x', 'a/../../x', 'a\\\\b', '', 'a//b', 'a/' }) do\n"
               "  local a, why = mod.write(bad, 'x')\n"
               "  ok(a == nil and why == \"not a path of the mod's\")\n"
               "  ok(mod.read(bad) == nil)\n"
               "end\n"
               "ok(select(2, mod.read('missing.txt')) == 'not found')\n"
               "mod.notice('hello there')\n"
               "print('files', n)\n");
    mod_folder("Broken", "test.broken", "gameplay", "mod.on('frame', function() error('on purpose') end)\n");
    start();
    CHECK(log_has("test.files print scripts/main.lua:13 files\t20"));
    snprintf(path, sizeof(path), "%s/saves/mods/test.files/notes/kept.txt", root);
    text = khdays_folders_read(path, NULL);
    CHECK(text != NULL && strcmp(text, "kept") == 0);
    free(text);
    CHECK(khdays_scripts_take_notice(&kind, &mod, &said) && kind == KHDAYS_NOTICE_SAID &&
          strcmp(mod, "test.files") == 0 && strcmp(said, "hello there") == 0);
    free(mod);
    free(said);
    frames(1);
    CHECK(khdays_scripts_take_notice(&kind, &mod, &said) && kind == KHDAYS_NOTICE_STOPPED &&
          strcmp(mod, "test.broken") == 0 && strstr(said, "on purpose") != NULL);
    free(mod);
    free(said);
    end_case();
}

static void test_room_fingerprint(void)
{
    unsigned int first, second;
    begin_case("rooms");
    mod_folder("Look", "test.look", "visual", "print('a visual mod')\n");
    start();
    CHECK(fake_mods_fingerprint == 0 && fake_mods_count == 0); /* visual mods are each player's own */
    khdays_scripts_stop();
    mod_folder("Play", "test.play", "gameplay", "print('a gameplay mod')\n");
    start();
    first = fake_mods_fingerprint;
    CHECK(first != 0 && fake_mods_count == 1 && fake_mods_list[0] == 9 &&
          memcmp(fake_mods_list + 1, "test.play", 9) == 0 && fake_mods_list[10] == 3 &&
          memcmp(fake_mods_list + 11, "1.0", 3) == 0);
    khdays_scripts_stop();
    start();
    CHECK(fake_mods_fingerprint == first); /* the same mods, the same fingerprint */
    khdays_scripts_stop();
    put("mods/Play/scripts/main.lua", "print('changed')\n");
    start();
    second = fake_mods_fingerprint;
    CHECK(second != 0 && second != first); /* a script changed: another fingerprint */
    khdays_scripts_ask(KHDAYS_ASK_ENABLE, "Play", NULL, 0);
    frames(1);
    CHECK(fake_mods_fingerprint == 0 && fake_mods_count == 0);
    end_case();
}

static void test_reload(void)
{
    char path[600];
    begin_case("reload");
    mod_folder("Live", "test.live", "gameplay",
               "local o = mod.option{ key = 'n', kind = 'number', label = 'N', min = 0, max = 10 }\n"
               "o:set(5)\n"
               "game.set_munny(game.munny + 1)\n");
    start();
    CHECK(fake_munny == 1);
    /* the studio asks: loaded again, its main run again, its option's value kept */
    snprintf(path, sizeof(path), "%s/saves/mods/reload.txt", root);
    CHECK(khdays_folders_write(path, "test.live\n", 10));
    frames(30);
    CHECK(fake_munny == 2 && !khdays_folders_is_file(path));
    CHECK(log_has("test.live reloaded - its files changed or it was asked to"));
    /* its script changed: loaded again by itself */
    SDL_Delay(20); /* a newer write time */
    put("mods/Live/scripts/main.lua", "game.set_munny(game.munny + 10)\n");
    frames(30);
    CHECK(fake_munny == 12);
    /* a stopped mod comes back once fixed */
    put("mods/Live/scripts/main.lua", "mod.on('frame', function() error('x') end)\n");
    frames(30);
    frames(1);
    CHECK(mod_by_folder("Live")->state == KHDAYS_MOD_STOPPED);
    SDL_Delay(20);
    put("mods/Live/scripts/main.lua", "game.set_munny(100)\n");
    frames(30);
    CHECK(mod_by_folder("Live")->state == KHDAYS_MOD_LOADED && fake_munny == 100);
    end_case();
}

static void test_guest(void)
{
    begin_case("guest");
    mod_folder("Host only", "test.host", "gameplay",
               "local a, b = game.set_munny(5)\nif a == nil and b == 'guest' then print('refused') end\n");
    fake_guest = 1;
    start();
    CHECK(fake_munny == 0);
    CHECK(log_has("test.host print scripts/main.lua:2 refused"));
    end_case();
}

#define RUN(test) (fprintf(stderr, "%s\n", #test), test())

int main(void)
{
    RUN(test_sha256);
    RUN(test_manifest);
    RUN(test_order);
    RUN(test_no_mods);
    RUN(test_sandbox);
    RUN(test_faults);
    RUN(test_order_file);
    RUN(test_events_and_state);
    RUN(test_player);
    RUN(test_options);
    RUN(test_menu_asks);
    RUN(test_actions);
    RUN(test_files_and_notices);
    RUN(test_room_fingerprint);
    RUN(test_reload);
    RUN(test_guest);
    if (failures != 0) {
        fprintf(stderr, "script_test: %d failures\n", failures);
        return 1;
    }
    printf("script_test: all passed\n");
    return 0;
}
