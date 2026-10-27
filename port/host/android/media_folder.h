/* The player's own folders for the mods and for the HD files (Android): chosen on the setup screen
 * (setup/SetupActivity.kt, MediaFolder.kt) or from the settings menu, kept in the app's private storage
 * (mods_folder.txt, hd_folder.txt), read as the game starts (runtime/core/folders.h). */
#ifndef KHDAYS_HOST_ANDROID_MEDIA_FOLDER_H
#define KHDAYS_HOST_ANDROID_MEDIA_FOLDER_H

#ifdef __cplusplus
extern "C" {
#endif

/* The folders chosen (absolute paths): "" for none (the app's own folder). The mods folder is the one
 * that holds a folder per mod; the HD folder holds data/cutscenes or cutscenes. */
const char *khdays_media_folder_mods(void);
const char *khdays_media_folder_hd(void);

/* Opens the setup screen over the game (the ROM, the HD files, the folders). 0 when it could not be. */
int khdays_media_folder_open_setup(void);

/* Opens the setup screen on one of its actions, straight away, back to the game once it is done:
 * "rom" (another ROM file), "hd_import" (HD files copied from a folder), "hd_folder" (the HD files'
 * own folder), "hd_app" (the app's again), "mods_folder", "mods_app". 0 when it could not be. */
int khdays_media_folder_act(const char *action);

/* What the setup screen would say of the game's files, a line each: "game=<ready or not>",
 * "hd=<the HD files>", "hd_folder=<path or empty>", "mods_folder=<path or empty>", "mods=<the mods
 * found>". Returns 0 (and an empty text) when it cannot be asked. */
int khdays_media_folder_status(char *text, int size);

#ifdef __cplusplus
}
#endif

#endif
