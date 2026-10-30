/* The save files in a folder of the player's own on Android (saves_folder.c), so that an app that syncs
 * folders (Syncthing, a cloud drive's) can reach them: the chip's file (khdays.sav) and the port's further
 * files (slots/slotNN.sav). Only those move; the settings, the logs and the mods' order stay in the app's
 * saves folder. The folder is chosen on the setup screen or from the settings menu (MediaFolder.kt,
 * saves_folder.txt in the app's private storage) and counts from the next start: the game's first read
 * of its save decides, copying the files over when they are not there yet.
 *
 * Nothing is ever deleted. A move copies each file, reads the copy back and compares it, and only when
 * all are the same is the new folder taken (saves_moved.txt in the private storage names it); until then
 * the saves stay where they were, and a move that stopped part way is made again at the next start
 * (saves_moving.txt). A chosen folder that already holds saves (synced from another device) is taken as
 * it is, never written over. Back to the app's folder, the files come from the folder they were moved
 * to, the app's own ones of before kept aside in saves/kept/<date-time>/. While the chosen folder cannot
 * be written (a memory card out, the access to all files taken back) the app's folder is played in
 * meanwhile, and its files are left as they are when the chosen folder comes back. */
#ifndef KHDAYS_HOST_ANDROID_SAVES_FOLDER_H
#define KHDAYS_HOST_ANDROID_SAVES_FOLDER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The folder the save files are read from and written to, decided as above (`own`: the app's saves
 * folder), into `folder`. */
void khdays_saves_folder_decide(const char *own, char *folder, size_t size);

#ifdef __cplusplus
}
#endif

#endif
