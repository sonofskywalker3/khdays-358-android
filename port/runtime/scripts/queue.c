/* What the window's thread asks of the mods (queue.h). */
#include "queue.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static SDL_Mutex *lock;
static SDL_InitState once;
static KhdaysScriptsMessage *messages;
static int count, capacity, first;

static void ready(void)
{
    if (SDL_ShouldInit(&once)) {
        lock = SDL_CreateMutex();
        SDL_SetInitialized(&once, lock != NULL);
    }
}

static char *copy(const char *s)
{
    char *out;
    if (s == NULL) {
        return NULL;
    }
    out = malloc(strlen(s) + 1);
    if (out != NULL) {
        strcpy(out, s);
    }
    return out;
}

void khdays_scripts_ask(int ask, const char *mod, const char *key, double value)
{
    ready();
    SDL_LockMutex(lock);
    if (first > 0 && first == count) {
        first = count = 0;
    }
    if (count == capacity) {
        const int grown = capacity == 0 ? 16 : capacity * 2;
        KhdaysScriptsMessage *m = realloc(messages, (size_t)grown * sizeof(*m));
        if (m == NULL) {
            SDL_UnlockMutex(lock);
            return;
        }
        messages = m;
        capacity = grown;
    }
    messages[count].ask = ask;
    messages[count].mod = copy(mod);
    messages[count].key = copy(key);
    messages[count].value = value;
    ++count;
    SDL_UnlockMutex(lock);
}

int khdays_scripts_take(KhdaysScriptsMessage *out)
{
    int taken = 0;
    ready();
    SDL_LockMutex(lock);
    if (first < count) {
        *out = messages[first++];
        taken = 1;
    }
    if (first == count) {
        first = count = 0;
    }
    SDL_UnlockMutex(lock);
    return taken;
}
