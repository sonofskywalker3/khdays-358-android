/* The mods the game reads: every folder in the mods folder, scripted (a mod.ini) or not, in load order.
 * The order and which are on are saves/mods/order.ini's, a `<folder> = on|off` line per folder
 * (scripts/order.h); a folder not listed yet comes after the listed ones, on. A mod is also off, by hand
 * and whatever the file says, when its folder's name begins with '.' or when it holds a file named
 * .ignore. The VFS reads the on mods in this order (the first with a file wins), and so do the sounds
 * and the scripts. */
#ifndef KHDAYS_MODS_LIST_H
#define KHDAYS_MODS_LIST_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysModEntry {
    const char *folder; /* its name in the mods folder */
    const char *path;   /* <mods folder>/<folder> */
    int scripted;       /* it has a mod.ini: runtime/scripts loads it */
    int on;             /* order.ini's (on when not listed) */
    int ignored;        /* off by hand: a .ignore file, or a name beginning with '.' */
} KhdaysModEntry;

/* Lists `mods_path`'s folders and reads `user`/mods/order.ini (`user`: the player's saves folder; NULL
 * for none), replacing what was known. Each mod is told in the session log. */
void khdays_mods_scan(const char *mods_path, const char *user);

int khdays_mods_count(void);
const KhdaysModEntry *khdays_mods_at(int index);
const KhdaysModEntry *khdays_mods_by_folder(const char *folder);

/* Whether the mod's files are read: on and not ignored. */
int khdays_mods_active(const KhdaysModEntry *entry);

/* Turns a mod on or off, or moves it one place earlier (`by` -1) or later (+1): order.ini is written
 * and the change told (khdays_mods_on_change). */
void khdays_mods_set_on(const char *folder, int on);
void khdays_mods_move(const char *folder, int by);

/* `changed` is called after a scan, a turn or a move: the host gives the VFS the active mods again
 * (core/rom.cpp). */
void khdays_mods_on_change(void (*changed)(void));

/* The active mods' paths in load order: `paths` receives at most `room` of them. Returns how many
 * there are. */
int khdays_mods_active_paths(const char **paths, int room);

#ifdef __cplusplus
}
#endif

#endif
