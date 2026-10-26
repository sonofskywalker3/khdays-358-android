/* The player's own folder for the mods and the HD videos (Android): chosen on the setup screen
 * (setup/SetupActivity.kt, MediaFolder.kt), kept in the app's private storage (media_folder.txt), read
 * as the game starts (runtime/core/folders.h). */
#ifndef KHDAYS_HOST_ANDROID_MEDIA_FOLDER_H
#define KHDAYS_HOST_ANDROID_MEDIA_FOLDER_H

#ifdef __cplusplus
extern "C" {
#endif

/* The folder chosen (an absolute path): "" for none (the app's own folder). */
const char *khdays_media_folder_chosen(void);

/* Opens the setup screen over the game (the ROM, the HD files, this folder). 0 when it could not be. */
int khdays_media_folder_open_setup(void);

#ifdef __cplusplus
}
#endif

#endif
