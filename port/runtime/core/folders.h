/* Where the mods and the HD videos are read from: "mods" and "data/cutscenes" in the working folder,
 * unless the player chose folders of their own for them (Android: the setup screen or the settings
 * menu; a memory card's folders, say). The mods folder chosen is the mods folder itself, the one that
 * holds a folder per mod. The HD folder chosen holds the videos in its data/cutscenes/ (the PC
 * installer's folder copied whole) or else in its cutscenes/. */
#ifndef KHDAYS_RUNTIME_CORE_FOLDERS_H
#define KHDAYS_RUNTIME_CORE_FOLDERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The folders chosen (absolute paths), as the game starts, before anything is read; NULL or "" for
 * none. One that cannot be read leaves the app's own. */
void khdays_folders_choose_mods(const char *folder);
void khdays_folders_choose_hd(const char *folder);

/* "mods", or the mods folder the player chose (no slash at the end). */
const char *khdays_mods_folder(void);

/* "data/cutscenes", or the chosen HD folder's data/cutscenes or cutscenes (no slash at the end). */
const char *khdays_cutscenes_folder(void);

#ifdef __cplusplus
}
#endif

#endif
