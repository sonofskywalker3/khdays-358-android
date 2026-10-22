/* The HD videos' subtitles (movie_subtitles.h): a video's file of lines (picture, rate, atlas, each
 * line's frames, place and texels), written by tools/hd/theater_movies.py; the format and how frames
 * are counted: docs/HD_SCREENS.md, "The HD videos' subtitles". */
#include "movie_subtitles.h"

#include <SDL3/SDL.h>

#include "../render/png_io.h"
#include "../../runtime/core/rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define SUBTITLE_WIDTH_SHARE 0.96f /* a line made larger stays within this much of the picture's width */

typedef struct Line {
    int first, end, x, y, u, v, w, h;
} Line;

typedef struct Subtitles {
    int picture_w, picture_h, rate;
    Line *lines;
    int count;
    uint8_t *pixels; /* the atlas's, until the host's thread makes its texture */
    int atlas_w, atlas_h;
} Subtitles;

static SRWLOCK lock = SRWLOCK_INIT;
static Subtitles loaded; /* the video's, given to the host's thread */
static int fresh;        /* loaded holds what the texture does not show yet */
static Subtitles shown;  /* the host's thread's */
static SDL_Texture *texture;

static void drop(Subtitles *s)
{
    free(s->lines);
    if (s->pixels != NULL) {
        khdays_png_free(s->pixels);
    }
    memset(s, 0, sizeof(*s));
}

/* Reads a video's file; 1 when it has lines. */
static int read_file(const char *path, Subtitles *s)
{
    char line[512], atlas[256] = "", folder[1024];
    FILE *f = fopen(path, "r");
    int capacity = 0;
    const char *error = NULL;
    const char *slash;
    if (f == NULL) {
        return 0;
    }
    memset(s, 0, sizeof(*s));
    s->picture_w = 1920;
    s->picture_h = 1080;
    s->rate = 30;
    while (fgets(line, sizeof(line), f) != NULL) {
        Line l;
        if (sscanf(line, "picture %d %d", &s->picture_w, &s->picture_h) == 2 ||
            sscanf(line, "rate %d", &s->rate) == 1 || sscanf(line, "atlas %255s", atlas) == 1) {
            continue;
        }
        if (sscanf(line, "line %d %d %d %d %d %d %d %d", &l.first, &l.end, &l.x, &l.y, &l.u, &l.v, &l.w, &l.h) == 8) {
            if (s->count == capacity) {
                const int grown = capacity ? capacity * 2 : 64;
                Line *more = realloc(s->lines, (size_t)grown * sizeof(Line));
                if (more == NULL) {
                    break;
                }
                s->lines = more;
                capacity = grown;
            }
            s->lines[s->count++] = l;
        }
    }
    fclose(f);
    slash = strrchr(path, '/');
    if (strrchr(path, '\\') > slash) {
        slash = strrchr(path, '\\');
    }
    snprintf(folder, sizeof(folder), "%.*s%s", slash != NULL ? (int)(slash - path) : 0, path, slash != NULL ? "/" : "");
    if (s->count > 0 && atlas[0] != '\0' && s->picture_w > 0 && s->picture_h > 0 && s->rate > 0) {
        char file[1300];
        snprintf(file, sizeof(file), "%s%s", folder, atlas);
        s->pixels = khdays_png_read(file, &s->atlas_w, &s->atlas_h, &error);
        if (s->pixels == NULL) {
            fprintf(stderr, "movies: subtitles %s: %s: %s\n", path, file, error != NULL ? error : "not read");
        }
    }
    if (s->pixels == NULL) {
        drop(s);
        return 0;
    }
    return 1;
}

void khdays_movie_subtitles_load(const char *movie, int language)
{
    static const char *const codes[] = {"", "en", "fr", "de", "it", "es"};
    char relative[128], path[1024];
    Subtitles s;
    int found;
    memset(&s, 0, sizeof(s));
    if (language >= 1 && language <= 5) {
        snprintf(relative, sizeof(relative), "movies/subtitles/%s/hd%s.txt", codes[language], movie);
        if (!khdays_mods_find(relative, path, (int)sizeof(path))) {
            snprintf(path, sizeof(path), "data/cutscenes/cinematics/subtitles/%s/hd%s.txt", codes[language], movie);
        }
        found = read_file(path, &s);
        if (found) {
            fprintf(stderr, "movies: %s: %d subtitle lines from %s\n", movie, s.count, path);
        }
    }
    AcquireSRWLockExclusive(&lock);
    drop(&loaded);
    loaded = s;
    fresh = 1;
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_subtitles_release(void)
{
    if (texture != NULL) {
        SDL_DestroyTexture(texture);
        texture = NULL;
    }
    drop(&shown);
    AcquireSRWLockExclusive(&lock);
    fresh = loaded.count > 0; /* a renderer made again makes it again */
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_subtitles_draw(SDL_Renderer *renderer, double time, double fps, int width, int height, float scale)
{
    int frame;
    float kx, ky;
    AcquireSRWLockExclusive(&lock);
    if (fresh) {
        fresh = 0;
        if (texture != NULL) {
            SDL_DestroyTexture(texture);
            texture = NULL;
        }
        drop(&shown);
        shown = loaded;
        if (loaded.lines != NULL) {
            /* a copy: loaded keeps its own for a renderer made again */
            shown.lines = malloc((size_t)loaded.count * sizeof(Line));
            if (shown.lines != NULL) {
                memcpy(shown.lines, loaded.lines, (size_t)loaded.count * sizeof(Line));
            } else {
                shown.count = 0;
            }
        }
        shown.pixels = NULL;
        if (loaded.pixels != NULL && shown.count > 0) {
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, loaded.atlas_w,
                                        loaded.atlas_h);
            if (texture != NULL) {
                SDL_UpdateTexture(texture, NULL, loaded.pixels, loaded.atlas_w * 4);
                SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (texture == NULL || shown.count == 0) {
        return;
    }
    if (fps > 0.0 && (SDL_fabs(fps - shown.rate) < 0.01 || SDL_fabs(fps - shown.rate * 1000.0 / 1001.0) < 0.01)) {
        frame = (int)SDL_floor(time * fps + 0.5);
    } else {
        frame = (int)SDL_floor(time * shown.rate + 0.5);
    }
    kx = (float)width / (float)shown.picture_w;
    ky = (float)height / (float)shown.picture_h;
    for (int i = 0; i < shown.count; ++i) {
        const Line *l = &shown.lines[i];
        if (frame >= l->first && frame < l->end) {
            const SDL_FRect from = {(float)l->u, (float)l->v, (float)l->w, (float)l->h};
            /* `scale` times larger about the line's middle and its foot, kept within the picture */
            const float k = SDL_min(scale, (float)shown.picture_w * SUBTITLE_WIDTH_SHARE / (float)l->w);
            const float w = (float)l->w * SDL_max(k, 1.0f), h = (float)l->h * SDL_max(k, 1.0f);
            const float middle = (float)l->x + (float)l->w / 2.0f, foot = (float)(l->y + l->h);
            const SDL_FRect at = {(middle - w / 2.0f) * kx, (foot - h) * ky, w * kx, h * ky};
            SDL_RenderTexture(renderer, texture, &from, &at);
        }
    }
}
