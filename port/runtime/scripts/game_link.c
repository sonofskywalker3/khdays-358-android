/* The game's side of the mods' interface (game_link.h), by the decompilation's names. */
#include "game_link.h"

#include "../core/window.h"
#include "../core/events.h"
#include "../../host/app/instance.h"
#include "../menus/camp.h"
#include "../sound/sound_bridge.h"
#include "khdays_ds_symbols.h"

#include <stdio.h>

extern int GameState_IsFlagSet(int flag);
extern int GameState_SetFlag(int flag);
extern void GameState_ClearFlag(int flag);
extern unsigned int Session_GetLocalPlayerIndex(void);
extern int GetEntryField20ByIndex(int idx);
extern int PlaySound(int sequence_archive, int entry);
extern KhdaysRef NNSi_FndAllocFromDefaultExpHeap(unsigned int size);
extern void NNSi_FndFreeFromDefaultHeap(KhdaysRef block);
extern void Ov002_ShowNoticeText(KhdaysRef text);
extern void Ov022_ActorSetHp(KhdaysRef pEnt, int nValue); /* the game's pointers are references (window.h) */
int khdays_overlay_loaded(int id);                        /* runtime/core/overlays.c */

#define FIELD_SCENE 2u
#define OV_BATTLE 22
#define OV_FIELD 2
#define CAPTIONS 0x0207f62cu /* data_ov002_0207f62c: [1] the field's captions (Ov002_ShowNoticeText) */
#define CAPTION_LINE 0xb8u   /* the notice held, 0 while none */
#define OBJ_FRAME 0x8u       /* gObjSystem: [2] the game's frame count */
#define SCENE_CURRENT 0x8u   /* gSceneCtl: current id, pending +0xc, argument +0x10 */
#define SCENE_ARGUMENT 0x10u
#define STATE_MUNNY 0xcu      /* GameState: nMunny (Ov008_CanConfirmShopRow) */
#define STATE_ITEMS 0x810u    /* GameState: aItemCount */
#define FLAG_ITEM_OWNED 0x4db /* Ov008_AddItemCount */
#define PLAYERS 0x020b2e7cu   /* *: the field's players, 0xc bytes each; +4 a record whose +0x20 is the actor */
#define ACTOR_POSITION 0x48cu
#define ENT_OWNER 0x8u /* Ov022_ActorSetHp's entity: nOwner, nId +9, nHp +0x12, nHpMax +0x16 */
#define ENT_ID 0x9u
#define ENT_HP 0x12u
#define ENT_HP_MAX 0x16u
#define OV_GAME_OVER 27
#define QUIT_FLAG 0x2087 /* set by Ov002_RetireSubFlow on giving up (runtime/menus/retry.c) */

static unsigned rd32(KhdaysRef at)
{
    return *(const volatile unsigned *)KHDAYS_W(at);
}

void khdays_link_folders(const char **mods, const char **user)
{
    *mods = "mods";
    *user = khdays_host_user_folder();
}

unsigned khdays_link_frame(void)
{
    return rd32(KHDAYS_DS_gObjSystem + OBJ_FRAME);
}

int khdays_link_paused(void)
{
    /* main's loop: mode 1 runs the callbacks only, the objects stand still */
    return *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_gPauseMode) == 1;
}

unsigned khdays_link_scene(unsigned *argument)
{
    if (argument != NULL) {
        *argument = rd32(KHDAYS_DS_gSceneCtl + SCENE_ARGUMENT);
    }
    return rd32(KHDAYS_DS_gSceneCtl + SCENE_CURRENT);
}

unsigned khdays_link_state(void)
{
    const unsigned state = rd32(KHDAYS_DS_gGameState);
    return khdays_game_ram(state) ? state : 0;
}

int khdays_link_munny(void)
{
    const unsigned state = khdays_link_state();
    return state != 0 ? (int)rd32(state + STATE_MUNNY) : 0;
}

void khdays_link_set_munny(int value)
{
    const unsigned state = khdays_link_state();
    if (state != 0) {
        *(volatile unsigned *)KHDAYS_W(state + STATE_MUNNY) = (unsigned)value;
    }
}

int khdays_link_items(unsigned char *counts)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return 0;
    }
    for (int i = 0; i < KHDAYS_LINK_ITEMS; ++i) {
        counts[i] = *(const volatile unsigned char *)KHDAYS_W(state + STATE_ITEMS + (unsigned)i);
    }
    return 1;
}

void khdays_link_set_item(int id, int count)
{
    const unsigned state = khdays_link_state();
    if (state == 0) {
        return;
    }
    *(volatile unsigned char *)KHDAYS_W(state + STATE_ITEMS + (unsigned)id) = (unsigned char)count;
    if (count > 0) {
        GameState_SetFlag(FLAG_ITEM_OWNED + id);
    }
}

int khdays_link_flag(int n)
{
    return GameState_IsFlagSet(n) != 0;
}

void khdays_link_set_flag(int n, int on)
{
    if (on) {
        GameState_SetFlag(n);
    } else {
        GameState_ClearFlag(n);
    }
}

int khdays_link_mission(void)
{
    if (khdays_link_scene(NULL) != FIELD_SCENE) {
        return -1;
    }
    return *(const volatile unsigned short *)KHDAYS_W(KHDAYS_DS_data_0204c23c);
}

/* The local player's battle actor, 0 without one. */
static unsigned player_actor(void)
{
    unsigned actor;
    if (!khdays_overlay_loaded(OV_BATTLE) || !khdays_game_ram(rd32(PLAYERS))) {
        return 0;
    }
    actor = (unsigned)GetEntryField20ByIndex((int)Session_GetLocalPlayerIndex());
    return khdays_game_ram(actor) ? actor : 0;
}

int khdays_link_position(float at[3])
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    for (int i = 0; i < 3; ++i) {
        at[i] = (float)(int)rd32(actor + ACTOR_POSITION + 4u * (unsigned)i) / 4096.0f;
    }
    return 1;
}

int khdays_link_player_hp(int *hp, int *max)
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    khdays_link_entity_hp(KHDAYS_W(actor), hp, max);
    return 1;
}

int khdays_link_set_player_hp(int hp)
{
    const unsigned actor = player_actor();
    if (actor == 0) {
        return 0;
    }
    Ov022_ActorSetHp(actor, hp);
    return 1;
}

void khdays_link_entity_hp(const void *entity, int *hp, int *max)
{
    const unsigned char *e = entity;
    *hp = *(const unsigned short *)(e + ENT_HP);
    *max = *(const unsigned short *)(e + ENT_HP_MAX);
}

int khdays_link_is_player(const void *entity)
{
    const unsigned actor = player_actor();
    return actor != 0 && KHDAYS_REF(entity) == actor;
}

static int in_field(void)
{
    return khdays_link_scene(NULL) == FIELD_SCENE && khdays_overlay_loaded(OV_FIELD);
}

const char *khdays_link_play_sound(const char *sequence_archive, const char *entry)
{
    const KhdaysSoundArchive *archive = khdays_sound_bridge_archive();
    int number, index;
    if (archive == NULL || !khdays_sound_archive_find_entry(archive, sequence_archive, entry, &number, &index)) {
        return "unknown sound";
    }
    PlaySound(number, index);
    return NULL;
}

const char *khdays_link_show_text(const unsigned short *text, int length)
{
    unsigned captions;
    KhdaysRef block;
    if (!in_field()) {
        return "not in the field";
    }
    captions = rd32(CAPTIONS + 4u);
    if (!khdays_game_ram(captions)) {
        return "not in the field";
    }
    if (rd32(captions + CAPTION_LINE) != 0) {
        return "busy";
    }
    block = NNSi_FndAllocFromDefaultExpHeap((unsigned)(length + 1) * 2u);
    if (!khdays_game_ram(block)) {
        return "no room in the game's heap";
    }
    for (int i = 0; i < length; ++i) {
        ((volatile unsigned short *)KHDAYS_W(block))[i] = text[i];
    }
    ((volatile unsigned short *)KHDAYS_W(block))[length] = 0;
    Ov002_ShowNoticeText(block); /* it keeps a copy of its own */
    NNSi_FndFreeFromDefaultHeap(block);
    return NULL;
}

int khdays_link_menu_open(int menu)
{
    if (menu == KHDAYS_LINK_PAUSE) {
        return khdays_link_scene(NULL) == FIELD_SCENE &&
               *(const volatile unsigned char *)KHDAYS_W(KHDAYS_DS_gPauseMode) != 0;
    }
    {
        int story;
        return khdays_camp_screen(&story) != KHDAYS_CAMP_NONE;
    }
}

int khdays_link_guest(void)
{
    /* the game's own test for the host: player 0 (Ov022_ActorSetHp, Ov022_ApplyDamageAndFlagHit) */
    return Session_GetLocalPlayerIndex() != 0;
}

void khdays_link_trace_frame(void)
{
    static unsigned scene = ~0u, points, vblanks_at;
    static int game_over;
    const unsigned now = khdays_link_scene(NULL), vblanks = khdays_events_vblank_count();
    const int over = khdays_overlay_loaded(OV_GAME_OVER);
    ++points;
    if (now != scene) {
        if (scene != ~0u) {
            fprintf(stderr, "mods-trace: scene %u: %u frame points over %u vblanks\n", scene, points,
                    vblanks - vblanks_at);
        }
        scene = now;
        points = 0;
        vblanks_at = vblanks;
    }
    if (over && !game_over && khdays_link_state() != 0) {
        fprintf(stderr, "mods-trace: game over: flag 0x2087 %d, mission %u\n", GameState_IsFlagSet(QUIT_FLAG) != 0,
                *(const volatile unsigned short *)KHDAYS_W(KHDAYS_DS_data_0204c23c));
    }
    game_over = over;
}

void khdays_link_trace_hp(const void *entity, int before)
{
    const unsigned char *e = entity;
    int hp, max;
    khdays_link_entity_hp(entity, &hp, &max);
    fprintf(stderr,
            "mods-trace: hp: entity %08x owner %u id %u flags %08x%08x, %d -> %d of %d; local player %u, its actor "
            "%08x\n",
            KHDAYS_REF(entity), e[ENT_OWNER], e[ENT_ID], *(const unsigned *)(e + 4), *(const unsigned *)e, before, hp,
            max, Session_GetLocalPlayerIndex(), player_actor());
}
