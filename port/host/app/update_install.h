/* The system's part of updating (updates.c): where the installer goes and running it
 * (update_install_win.c on Windows). */
#ifndef KHDAYS_HOST_UPDATE_INSTALL_H
#define KHDAYS_HOST_UPDATE_INSTALL_H

#include <stddef.h>

/* Whether this copy can update itself: an installed copy (its uninstaller beside it) on Windows itself,
 * not under Proton or Wine. */
int khdays_update_install_possible(void);

/* The download folder (khdays-update in the temporary files), made if needed, into `out` (UTF-8); 0
 * when it cannot be. */
int khdays_update_install_folder(char *out, size_t size);

/* Removes the installers an earlier update left in that folder (the one running now stays). */
void khdays_update_install_clean(void);

/* Runs installer `file` silently over this copy (keeping the last install's choices), reopening the
 * game when done; Windows asks for the administrator's permission. 1 when it started (the game must
 * then quit so its files can be replaced), else 0 with the reason in `why`. */
int khdays_update_install_run(const char *file, char *why, size_t size);

#endif
