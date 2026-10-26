/* Where the mods and the HD videos are read from: "mods" and "data/cutscenes" in the working folder,
 * unless the player chose a folder of their own for them (Android: the setup screen; a memory card's
 * folder, say). Then its mods/ are the mods, and its data/cutscenes/ (the PC installer's folder copied
 * whole) or else cutscenes/ the HD videos. */
#ifndef KHDAYS_RUNTIME_CORE_FOLDERS_H
#define KHDAYS_RUNTIME_CORE_FOLDERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The chosen folder (an absolute path), as the game starts, before anything is read; NULL or "" for
 * none. One that cannot be read leaves the app's own. */
void khdays_folders_choose(const char *folder);

/* "mods", or the chosen folder's mods/ (no slash at the end). */
const char *khdays_mods_folder(void);

/* "data/cutscenes", or the chosen folder's cutscenes/ (no slash at the end). */
const char *khdays_cutscenes_folder(void);

#ifdef __cplusplus
}
#endif

#endif
