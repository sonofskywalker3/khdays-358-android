/* What the settings menu's Mods page shows of the mods (view.h). */
#include "view.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static SDL_Mutex *lock;
static SDL_InitState once;
static KhdaysModsView published;

typedef struct Notice {
    int kind;
    char *mod, *text;
} Notice;
static Notice *notices;
static int notice_count, notice_capacity;

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

static void copy_texts(char *out[KHDAYS_OPTION_LANGUAGES], char *const in[KHDAYS_OPTION_LANGUAGES])
{
    for (int i = 0; i < KHDAYS_OPTION_LANGUAGES; ++i) {
        out[i] = copy(in[i]);
    }
}

static void copy_option(KhdaysOptionView *out, const KhdaysOptionView *in)
{
    *out = *in;
    out->key = copy(in->key);
    copy_texts(out->label, in->label);
    copy_texts(out->description, in->description);
    out->values = in->value_count > 0 ? calloc((size_t)in->value_count, sizeof(*out->values)) : NULL;
    for (int i = 0; out->values != NULL && i < in->value_count; ++i) {
        out->values[i] = copy(in->values[i]);
    }
    if (out->values == NULL) {
        out->value_count = 0;
    }
}

static void copy_view(KhdaysModsView *out, const KhdaysModsView *in)
{
    out->serial = in->serial;
    out->count = in->count;
    out->mods = in->count > 0 ? calloc((size_t)in->count, sizeof(*out->mods)) : NULL;
    if (out->mods == NULL) {
        out->count = 0;
        return;
    }
    for (int i = 0; i < in->count; ++i) {
        const KhdaysModView *a = &in->mods[i];
        KhdaysModView *b = &out->mods[i];
        *b = *a;
        b->folder = copy(a->folder);
        b->id = copy(a->id);
        b->name = copy(a->name);
        b->version = copy(a->version);
        b->author = copy(a->author);
        b->description = copy(a->description);
        b->reason = copy(a->reason);
        b->options = a->option_count > 0 ? calloc((size_t)a->option_count, sizeof(*b->options)) : NULL;
        b->option_count = b->options != NULL ? a->option_count : 0;
        for (int o = 0; o < b->option_count; ++o) {
            copy_option(&b->options[o], &a->options[o]);
        }
    }
}

void khdays_scripts_view_free(KhdaysModsView *v)
{
#pragma warning(push)
#pragma warning(disable : 6001) /* every view comes from calloc and copy_view: each field NULL or its own */
    for (int i = 0; i < v->count; ++i) {
        KhdaysModView *m = &v->mods[i];
        free(m->folder);
        free(m->id);
        free(m->name);
        free(m->version);
        free(m->author);
        free(m->description);
        free(m->reason);
        for (int o = 0; o < m->option_count; ++o) {
            KhdaysOptionView *ov = &m->options[o];
            free(ov->key);
            for (int t = 0; t < KHDAYS_OPTION_LANGUAGES; ++t) {
                free(ov->label[t]);
                free(ov->description[t]);
            }
            for (int s = 0; s < ov->value_count; ++s) {
                free(ov->values[s]);
            }
            free(ov->values);
        }
        free(m->options);
    }
#pragma warning(pop)
    free(v->mods);
    v->mods = NULL;
    v->count = 0;
}

void khdays_scripts_view_set(const KhdaysModsView *now)
{
    ready();
    SDL_LockMutex(lock);
    {
        const unsigned serial = published.serial + 1;
        khdays_scripts_view_free(&published);
        copy_view(&published, now);
        published.serial = serial;
    }
    SDL_UnlockMutex(lock);
}

void khdays_scripts_notice(int kind, const char *mod, const char *text)
{
    ready();
    SDL_LockMutex(lock);
    if (notice_count == notice_capacity) {
        const int grown = notice_capacity == 0 ? 8 : notice_capacity * 2;
        Notice *more = realloc(notices, (size_t)grown * sizeof(*more));
        if (more == NULL) {
            SDL_UnlockMutex(lock);
            return;
        }
        notices = more;
        notice_capacity = grown;
    }
    notices[notice_count].kind = kind;
    notices[notice_count].mod = copy(mod != NULL ? mod : "");
    notices[notice_count].text = copy(text != NULL ? text : "");
    ++notice_count;
    SDL_UnlockMutex(lock);
}

int khdays_scripts_take_notice(int *kind, char **mod, char **text)
{
    int taken = 0;
    ready();
    SDL_LockMutex(lock);
    if (notice_count > 0) {
        *kind = notices[0].kind;
        *mod = notices[0].mod;
        *text = notices[0].text;
        memmove(notices, notices + 1, (size_t)(notice_count - 1) * sizeof(*notices));
        --notice_count;
        taken = 1;
    }
    SDL_UnlockMutex(lock);
    return taken;
}

int khdays_scripts_view(KhdaysModsView *view)
{
    int newer;
    ready();
    SDL_LockMutex(lock);
    newer = published.serial != view->serial;
    if (newer) {
        khdays_scripts_view_free(view);
        copy_view(view, &published);
    }
    SDL_UnlockMutex(lock);
    return newer;
}

const char *khdays_scripts_text(char *const texts[KHDAYS_OPTION_LANGUAGES], int language)
{
    if (language > 0 && language < KHDAYS_OPTION_LANGUAGES && texts[language] != NULL) {
        return texts[language];
    }
    if (texts[0] != NULL) {
        return texts[0];
    }
    if (texts[1] != NULL) {
        return texts[1];
    }
    for (int i = 2; i < KHDAYS_OPTION_LANGUAGES; ++i) {
        if (texts[i] != NULL) {
            return texts[i];
        }
    }
    return NULL;
}
