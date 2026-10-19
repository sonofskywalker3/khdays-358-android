/* What the window's thread asks of the mods (the settings menu opened or closed, an option changed, a
 * button pressed, a mod turned on, off, moved or reloaded): kept until the game's thread takes it at
 * the frame point. */
#ifndef KHDAYS_SCRIPTS_QUEUE_H
#define KHDAYS_SCRIPTS_QUEUE_H

enum KhdaysScriptsAsk {
    KHDAYS_ASK_SETTINGS_OPENED,
    KHDAYS_ASK_SETTINGS_CLOSED,
    KHDAYS_ASK_OPTION, /* `mod` `key` = `value` */
    KHDAYS_ASK_BUTTON, /* `mod` `key` pressed */
    KHDAYS_ASK_ENABLE, /* the mod folder `mod` on (`value` 1) or off */
    KHDAYS_ASK_MOVE,   /* the mod folder `mod` one place earlier (`value` -1) or later (+1) */
    KHDAYS_ASK_RELOAD, /* the mod `mod` (its id), or every mod ("*") */
    KHDAYS_ASK_RESUME  /* the paused mod `mod` (its id) */
};

typedef struct KhdaysScriptsMessage {
    int ask;   /* KhdaysScriptsAsk */
    char *mod; /* to free() with the message */
    char *key;
    double value;
} KhdaysScriptsMessage;

/* Any thread: adds a message (the strings are copied). */
void khdays_scripts_ask(int ask, const char *mod, const char *key, double value);

/* The game's thread: takes the oldest message into `out` (its strings now the caller's). Returns 0
 * when there is none. */
int khdays_scripts_take(KhdaysScriptsMessage *out);

#endif
