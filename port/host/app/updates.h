/* Updates. At start a thread asks GitHub for the latest release (KHDAYS_UPDATE_SOURCE asks another
 * address, for tests) and keeps the answer in saves/updates.txt; the title screen offers a newer one.
 * Download fetches its installer, checks its SHA-256 against GitHub's and runs it, closing the game;
 * a copy that cannot update itself (not installed, or under Proton/Wine) opens the release's page
 * instead. Headless and scripted runs never ask, nor KHDAYS_UPDATES=off. */
#ifndef KHDAYS_HOST_UPDATES_H
#define KHDAYS_HOST_UPDATES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The player's answers: the release's page, later, skip this release, run the downloaded installer. */
enum { KHDAYS_UPDATE_DOWNLOAD, KHDAYS_UPDATE_LATER, KHDAYS_UPDATE_SKIP, KHDAYS_UPDATE_INSTALL };

/* The installer's download. */
enum { KHDAYS_DOWNLOAD_NONE, KHDAYS_DOWNLOAD_GETTING, KHDAYS_DOWNLOAD_READY, KHDAYS_DOWNLOAD_FAILED };

/* This build's version ("0.1.0", the repository's VERSION file). */
const char *khdays_updates_version(void);

/* At the start, once, with the player's setting (`enabled`): reads what was found before, and when
 * GitHub was last asked a day ago or more, asks again in the background. */
void khdays_updates_start(int enabled);

/* A newer release to tell the player of now: its tag into `tag` (`size` bytes) and 1; or 0 when
 * there is none, the player was told of it already this run, or said not to be told of it. */
int khdays_updates_offer(char *tag, size_t size);

/* Whether Download can update this copy itself: GitHub listed the release's installer and this copy
 * can run it (update_install.h). Otherwise Download opens the release's page. */
int khdays_updates_can_install(void);

/* Starts downloading the release's installer, in the background (once: again after a failure). */
void khdays_updates_download(void);

/* The download now (KHDAYS_DOWNLOAD_*): the bytes so far and the file's size, and why it failed. */
int khdays_updates_download_state(long long *done, long long *total, char *why, size_t size);

/* Gives the download up (it then ends as KHDAYS_DOWNLOAD_NONE). */
void khdays_updates_download_cancel(void);

/* Runs the downloaded installer (update_install.h): 1 when it started, the game then to close; 0
 * with the reason in `why` (the administrator's permission refused, among others). */
int khdays_updates_install(char *why, size_t size);

/* The player's answer to the offer (KHDAYS_UPDATE_*): Download opens the release's page, Skip
 * remembers not to tell of this release again, Later tells again at the next start, Install says
 * the installer runs. Either way the offer is not made again this run. */
void khdays_updates_answer(int answer);

#ifdef __cplusplus
}
#endif

#endif
