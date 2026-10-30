/* host/app/instance.h on POSIX. Each slot is a lock on a file in the saves folder (the system releases
 * it when the process ends); on Android one copy runs, which always takes slot 1. */
#include "instance.h"
#if defined(__ANDROID__)
#include "../android/saves_folder.h"
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static int slot;        /* 0 until a slot is taken */
static char folder[64]; /* "saves" or "saves/player<N>" */

/* Tries to take slot `number`. Returns 1 if it is ours now (its lock stays open), or 0 if another
 * copy holds it. If the system cannot make the lock at all, the slot counts as ours. */
static int claim(int number)
{
    char name[64];
    int file;
    mkdir("saves", 0777);
    snprintf(name, sizeof(name), "saves/.player-%d.lock", number);
    file = open(name, O_RDWR | O_CREAT, 0666);
    if (file < 0) {
        return 1;
    }
    if (flock(file, LOCK_EX | LOCK_NB) != 0) {
        close(file);
        return 0;
    }
    return 1; /* the lock stays held while the process runs */
}

int khdays_host_player_slot(void)
{
    const char *value;
    if (slot != 0) {
        return slot;
    }
    /* named by the environment: the walks run as player 1 whatever else is running */
    value = getenv("KHDAYS_PLAYER_SLOT");
    if (value != NULL && atoi(value) > 0) {
        slot = atoi(value);
        return slot;
    }
#if defined(__ANDROID__)
    slot = 1;
#else
    for (int number = 1;; ++number) {
        if (claim(number)) {
            slot = number;
            break;
        }
    }
#endif
    if (slot > 1) {
        fprintf(stderr, "khdays-native: another copy runs: this one is player %d (saves/player%d)\n", slot, slot);
    }
    return slot;
}

int khdays_host_player_slot_taken(void)
{
    return slot;
}

const char *khdays_host_user_folder(void)
{
    if (slot == 0) {
        return "saves"; /* no slot taken (the tools and tests): use slot 1's */
    }
    if (folder[0] == '\0') {
        const int number = slot;
        if (number == 1) {
            snprintf(folder, sizeof(folder), "saves");
        } else {
            snprintf(folder, sizeof(folder), "saves/player%d", number);
            mkdir("saves", 0777);
            mkdir(folder, 0777);
        }
    }
    return folder;
}

const char *khdays_host_saves_folder(void)
{
#if defined(__ANDROID__)
    static char saves[1024];
    if (saves[0] == '\0') { /* (the game's thread only: its save's first read) */
        char decided[sizeof(saves)];
        khdays_saves_folder_decide(khdays_host_user_folder(), decided, sizeof(decided));
        memcpy(saves, decided, sizeof(saves));
    }
    return saves;
#else
    return khdays_host_user_folder();
#endif
}

int khdays_host_other_copies(void)
{
#if defined(__ANDROID__)
    return 0;
#else
    /* slot 1's lock held by someone else */
    int file = open("saves/.player-1.lock", O_RDWR);
    int others = 0;
    if (file >= 0) {
        others = slot != 1 && flock(file, LOCK_EX | LOCK_NB) != 0;
        close(file);
    }
    return others;
#endif
}

/* "Player" and four digits from this machine: a hash of its host name and of the user folder, which
 * reveals neither. */
void khdays_host_automatic_name(char *out, size_t size)
{
    char machine[256] = "";
    unsigned int digest = 2166136261u; /* FNV-1a */
    const char *user = khdays_host_user_folder();
    if (gethostname(machine, sizeof(machine) - 1) == 0) {
        for (const char *c = machine; *c != '\0'; ++c) {
            digest = (digest ^ (unsigned char)*c) * 16777619u;
        }
    }
    for (const char *c = user; *c != '\0'; ++c) {
        digest = (digest ^ (unsigned char)*c) * 16777619u;
    }
    snprintf(out, size, "Player%04u", digest % 10000u);
}

int khdays_host_player_name(const char *nickname, unsigned short *name, int room)
{
    char chosen[256];
    int length = 0;
    const int number = slot > 1 ? slot : 0;
    const char *named = getenv("KHDAYS_NAME");
    if (named != NULL && named[0] != '\0' && strlen(named) < sizeof(chosen)) {
        snprintf(chosen, sizeof(chosen), "%s", named);
    } else if (nickname != NULL && nickname[0] != '\0') {
        snprintf(chosen, sizeof(chosen), "%s", nickname);
    } else {
        khdays_host_automatic_name(chosen, sizeof(chosen));
    }
    for (const char *c = chosen; *c != '\0' && length < room; ++c) {
        if (*c >= 0x20 && *c < 0x7f) {
            name[length++] = (unsigned short)*c;
        }
    }
    if (number > 0 && room > 0) {
        /* player N's copy on this machine: the number goes last, replacing the name's last letter if
         * the name is full */
        if (length >= room) {
            length = room - 1;
        }
        name[length++] = (unsigned short)('0' + number % 10);
    }
    return length;
}
