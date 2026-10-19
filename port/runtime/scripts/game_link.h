/* The game's side of the mods' interface: every name the interface gives reads, changes or calls the
 * game through these, by the decompilation's names (game_link.c). The tests give their own. */
#ifndef KHDAYS_SCRIPTS_GAME_LINK_H
#define KHDAYS_SCRIPTS_GAME_LINK_H

#define KHDAYS_LINK_ITEMS 0x8d0  /* gGameState->aItemCount's entries */
#define KHDAYS_LINK_FLAGS 0x4000 /* flags from *gGameState + 0x10 up to aItemCount at +0x810 */
#define KHDAYS_LINK_MUNNY_MAX 999999
#define KHDAYS_LINK_GIVEN_UP 0x2087 /* set by Ov002_RetireSubFlow on giving up (runtime/menus/retry.c) */

enum KhdaysLinkMenu { KHDAYS_LINK_PAUSE, KHDAYS_LINK_CAMP };

/* Where the mods are and the player's saves folder (their data, the mods' log). */
void khdays_link_folders(const char **mods, const char **user);

/* The game's frame count (the object list's), and whether it is paused (gPauseMode). */
unsigned khdays_link_frame(void);
int khdays_link_paused(void);

/* The current scene (gSceneCtl) and its argument. */
unsigned khdays_link_scene(unsigned *argument);

/* The game's state (gGameState's reference), 0 while there is none: the state names and the item
 * actions need it. */
unsigned khdays_link_state(void);

int khdays_link_munny(void);
void khdays_link_set_munny(int value);

/* The item counts (KHDAYS_LINK_ITEMS bytes into `counts`); 0 without the game's state. */
int khdays_link_items(unsigned char *counts);
/* An item's count set, with its owned flag when above 0 (Ov008_AddItemCount's rule). */
void khdays_link_set_item(int id, int count);

int khdays_link_flag(int n);
void khdays_link_set_flag(int n, int on);

/* The current mission (data_0204c23c) while the field is the scene; -1 otherwise. */
int khdays_link_mission(void);

/* The local player's battle actor is GetEntryField20ByIndex(Session_GetLocalPlayerIndex()): the field
 * players' table (*(data_ov022_020b2e78 + 4), 0xc bytes an entry, +4 a record whose +0x20 is the actor),
 * read with the battle (ov022) loaded. Its position is at +0x48c (20.12 fixed point), its HP at +0x12
 * and maximum at +0x16 (Ov022_ActorSetHp's entity). Each returns 0 without the actor. */
int khdays_link_position(float at[3]);
int khdays_link_player_hp(int *hp, int *max);
/* Through Ov022_ActorSetHp, as the game's own hits and cures do. */
int khdays_link_set_player_hp(int hp);

/* An entity Ov022_ActorSetHp was given: its HP and maximum (nHp +0x12, nHpMax +0x16). */
void khdays_link_entity_hp(const void *entity, int *hp, int *max);

/* Whether that entity is the local player's actor. */
int khdays_link_is_player(const void *entity);

/* KHDAYS_TRACE_MODS: what the interface's open questions are answered from, on stderr: each HP
 * change's entity against the field's players, the game over's flags, the frame points per scene. */
void khdays_link_trace_frame(void);
void khdays_link_trace_hp(const void *entity, int before);

/* The game's actions, each through its own routine; NULL when done, else the reason it was not. */

/* PlaySound(archive, entry), the sequence archive's entry found by its names in the sound archive. */
const char *khdays_link_play_sound(const char *sequence_archive, const char *entry);

/* Ov002_ShowNoticeText with `text` (`length` UTF-16 units) in a block of the game's heap: the field's
 * notice, of which the game takes the first only ("busy" after it). */
const char *khdays_link_show_text(const unsigned short *text, int length);

/* Whether a menu is open: the pause, the camp. */
int khdays_link_menu_open(int menu);

/* A guest in a multiplayer session: the host alone changes the game. */
int khdays_link_guest(void);

#endif
