/* How an HD movie runs (movie_hd.h), as states: Idle, Playing, Held (the game's movie ended first),
 * Finishing (the video ended first), Skipping (START where the DS allows it); the credits scene held at
 * its roll's end instead. The game's thread brings the movie's events, the window's the video's and
 * START; one lock keeps them in order (docs/PLATFORM.md, "HD movies"). */
#include "movie_hd.h"

#include "../core/clock.h"
#include "../input/input.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../../host/app/host.h"
#include "../../host/movies/movie_hd.h"
#include "../../host/movies/movie_subtitles.h"
#include "../../host/app/sound_output.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

extern int GetLanguage(void);     /* the game's: gLangPath's language */
int khdays_language_wanted(void); /* language.c */

/* SCENE: a scene's videos play, the game held; SCENE_SKIP: they are over, the game let go to take its
 * own skip at its next step; SCENE_LANDING: the skip asked, until the scene's stretch ends;
 * SCENE_PAUSE: START paused the video and let the game go to open its own pause over it. */
enum { IDLE, PLAYING, HELD, FINISHING, SKIPPING, SCENE, SCENE_SKIP, SCENE_LANDING, SCENE_PAUSE };

static SRWLOCK lock = SRWLOCK_INIT;
static volatile LONG state = IDLE;    /* read without the lock to see whether a course runs */
static char id[64];                   /* the movie's, as the game names it */
static int skippable = -1;            /* the DS lets START skip it: 1, 0, -1 not said yet */
static int skip_wanted;               /* START came before the movie said */
static int skipped;                   /* the game marked its skip */
static uint16_t game_last, host_last; /* the buttons last seen, for START's press */
static volatile LONG ended;           /* a course has just ended: the buttons held then are withheld */

/* state and ended read as they are written: atomically (the decoder's thread
 * and the game's both look; volatile alone is atomic only by MSVC's reading) */
static LONG state_now(void)
{
    return InterlockedCompareExchange(&state, 0, 0);
}

static LONG ended_now(void)
{
    return InterlockedCompareExchange(&ended, 0, 0);
}
static uint16_t stuck;     /* ... until they are let go */
/* SCENE_PAUSE: START given to open the game's pause; the buttons held then; the pause seen open;
 * the frames since START */
static int pause_start_given, pause_seen, pause_frames, pause_press;
static uint16_t pause_stuck;
#define PAUSE_WAIT_FRAMES 90 /* a pause the game does not open within this: the video goes on */
static int credits_scene;  /* the credits scene runs (khdays_movie_hd_credits) */
static int course_credits; /* the course is its movie's */

/* The ids already told about (no video, or one that cannot be played): once each. */
static char (*told)[64];
static int told_count, told_capacity;

static int tell_once(const char *movie)
{
    for (int i = 0; i < told_count; ++i) {
        if (strcmp(told[i], movie) == 0) {
            return 0;
        }
    }
    if (told_count == told_capacity) {
        const int capacity = told_capacity ? told_capacity * 2 : 16;
        char(*grown)[64] = realloc(told, (size_t)capacity * sizeof(*told));
        if (grown == NULL) {
            return 1;
        }
        told = grown;
        told_capacity = capacity;
    }
    snprintf(told[told_count++], sizeof(told[0]), "%s", movie);
    return 1;
}

/* The movie's id: the path's file name before its first dot ("/mv/837.mods" -> "837", "843.z" ->
 * "843"). */
static int movie_id(const char *path, char *out, size_t size)
{
    const char *base = path, *dot;
    size_t n;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            base = p + 1;
        }
    }
    dot = strchr(base, '.');
    n = dot != NULL ? (size_t)(dot - base) : strlen(base);
    if (n == 0 || n >= size) {
        return 0;
    }
    memcpy(out, base, n);
    out[n] = '\0';
    return 1;
}

static const char *skippable_text(void)
{
    return skippable > 0 ? "skippable" : skippable == 0 ? "not skippable" : "not said";
}

/* Back to the game: shown, heard, its clock let go. */
static void finish(const char *how)
{
    khdays_hd_close();
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_sound_output_mute(0);
    khdays_log("movie %s: the HD video %s (the game's movie %s); the game goes on", id, how, skippable_text());
    fprintf(stderr, "movies: %s: the HD video %s; the game goes on\n", id, how);
    InterlockedExchange(&ended, 1);
    InterlockedExchange(&state, IDLE);
}

/* The game's own movie plays: from a stream's opening to the playback's stop (the hooks below), HD
 * video or not -- for the one screen's situation (ui/situation_decide.c). */
static volatile LONG game_movie;

void khdays_movie_hd_reset(void)
{
    InterlockedExchange(&game_movie, 0);
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        finish("stopped by the game's reset");
    }
    ReleaseSRWLockExclusive(&lock);
}

static void scene_videos_over(const char *how);
static int scene_chapter; /* the course is a Theater chapter's (no event: it ends with its videos) */
static void scene_pause_begin(void);

static void begin_skip(void)
{
    khdays_hd_close(); /* the picture and the sound stop at once; the window keeps the last picture */
    skipped = 0;
    InterlockedExchange(&state, SKIPPING);
    khdays_log("movie %s: the HD video stopped by START; START given to the game for its movie", id);
}

static void request_skip(void)
{
    switch (state_now()) {
    case PLAYING:
        if (skippable > 0) {
            begin_skip();
        } else if (skippable < 0) {
            skip_wanted = 1; /* decided when the movie says (khdays_movie_hd_flags) */
        }
        break;
    case FINISHING:
        if (skippable > 0) {
            skipped = 0;
            InterlockedExchange(&state, SKIPPING);
        }
        break;
    case HELD:
        if (skippable != 0) {
            finish("skipped");
        }
        break;
    case SCENE:
        if (scene_chapter) {
            scene_videos_over("stopped by START");
        } else {
            scene_pause_begin();
        }
        break;
    default: break;
    }
}

int khdays_movie_game_playing(void)
{
    return InterlockedCompareExchange(&game_movie, 0, 0) != 0;
}

void khdays_movie_hd_stream(int slot, const char *path, int replaced)
{
    char movie[64], file[1024], why[320];
    KhdaysHdInfo info;
    (void)slot;
    InterlockedExchange(&game_movie, 1);
    if (path == NULL || replaced || state_now() != IDLE || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD) ||
        !movie_id(path, movie, sizeof(movie))) {
        return;
    }
    /* a mod's video first (mods/<Mod>/movies/hd<id>.mp4, as khdays-studio
     * saves one), then the player's own HD videos */
    {
        char relative[64];
        snprintf(relative, sizeof(relative), "movies/hd%s.mp4", movie);
        if (!khdays_mods_find(relative, file, (int)sizeof(file))) {
            snprintf(file, sizeof(file), "data/cutscenes/cinematics/hd%s.mp4", movie);
        }
    }
    if (GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) {
        if (tell_once(movie)) {
            khdays_log("movie %s: no HD video (%s); the game's movie plays", movie, file);
        }
        return;
    }
    /* opened outside the lock: only this thread starts a course */
    if (!khdays_hd_open(file, &info, why, sizeof(why))) {
        if (tell_once(movie)) {
            khdays_log("movie %s: %s cannot be played (%s); the game's movie plays", movie, file, why);
            fprintf(stderr, "movies: %s cannot be played (%s); the game's movie plays\n", file, why);
        }
        return;
    }
    /* the theater's subtitles for it in the game's language (a language just chosen first) */
    khdays_movie_subtitles_load(movie, khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage());
    AcquireSRWLockExclusive(&lock);
    snprintf(id, sizeof(id), "%s", movie);
    skippable = -1;
    skip_wanted = 0;
    skipped = 0;
    course_credits = credits_scene;
    game_last = host_last = 0xffff; /* what is held now is no press */
    khdays_sound_output_mute(1);
    khdays_hd_start();
    InterlockedExchange(&state, PLAYING);
    ReleaseSRWLockExclusive(&lock);
    khdays_log("movie %s: the HD video %s plays (%dx%d, %.3f frames a second, %s)", movie, file, info.width,
               info.height, info.fps, info.channels > 0 ? "with its sound" : "no sound");
    fprintf(stderr, "movies: %s shown instead from %s (%dx%d, %s)\n", movie, file, info.width, info.height,
            info.channels > 0 ? "with its sound" : "no sound");
}

void khdays_movie_hd_stopped(void)
{
    InterlockedExchange(&game_movie, 0);
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == PLAYING && course_credits) {
        khdays_log("movie %s: the game's movie ended; its staff roll runs on behind the HD video", id);
    } else if (state_now() == PLAYING) {
        khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1); /* the game stops at its next wait */
        InterlockedExchange(&state, HELD);
        khdays_log("movie %s: the game's movie ended; the game waits for the HD video", id);
        fprintf(stderr, "movies: %s: the game's movie ended; the game waits for the HD video\n", id);
    } else if (state_now() == FINISHING) {
        finish("ended");
    } else if (state_now() == SKIPPING) {
        finish("skipped");
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_credits(int phase)
{
    AcquireSRWLockExclusive(&lock);
    if (phase == 1) {
        credits_scene = 1;
    } else if (state_now() != IDLE && course_credits) {
        if (phase == 2 && state_now() == PLAYING) {
            khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
            InterlockedExchange(&state, HELD);
            khdays_log("movie %s: the staff roll ended; the game waits for the HD video", id);
            fprintf(stderr, "movies: %s: the staff roll ended; the game waits for the HD video\n", id);
        } else if (phase == 0 || state_now() == SKIPPING) {
            finish(state_now() == SKIPPING ? "skipped" : "ended with the credits scene");
        }
    }
    if (phase == 0) {
        credits_scene = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_flags(unsigned flags)
{
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        skippable = (flags & 8) != 0;
        if (state_now() == SKIPPING && (flags & 4)) {
            skipped = 1;
        }
        if (skip_wanted) {
            skip_wanted = 0;
            request_skip();
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

uint16_t khdays_movie_hd_game_buttons(uint16_t buttons)
{
    uint16_t given;
    if (state_now() == IDLE && !ended_now() && stuck == 0) {
        return buttons;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        const uint16_t pressed = (uint16_t)(buttons & ~game_last);
        game_last = buttons;
        if (pressed & KHDAYS_KEY_START) {
            request_skip();
        }
        given = state_now() == SKIPPING && !skipped ? (uint16_t)KHDAYS_KEY_START : 0; /* a scene's: none */
        if (state_now() == SCENE_PAUSE) {
            /* the game's own pause: START pressed every other frame until it opens (the game refuses
             * one while its screen fades: a scene held at its start), then the player's buttons, but
             * the ones held as it opened (the START that asked for it) only once let go */
            if (!pause_seen) {
                pause_start_given = 1;
                pause_press = !pause_press;
                pause_stuck = buttons;
                given = pause_press ? (uint16_t)KHDAYS_KEY_START : 0;
            } else {
                pause_stuck &= buttons;
                given = (uint16_t)(buttons & ~pause_stuck);
            }
        }
    } else {
        if (ended_now()) {
            stuck = buttons; /* held as the course ended */
            InterlockedExchange(&ended, 0);
        }
        stuck &= buttons;
        given = (uint16_t)(buttons & ~stuck);
    }
    ReleaseSRWLockExclusive(&lock);
    return given;
}

int khdays_movie_hd_showing(void)
{
    return state_now() != IDLE;
}

/* --- The theater's HD scenes, its chapters, the monologues' voice --------------------------------- */

#define SCENE_SKIP_REQUEST 0x588u /* the event machine's skip request (Game_UnwindActionStack) */

void SoundMgr_StopAllSePlayers(void); /* the game's: what the event pause's Skip stops */

static char scene_key[48];
static uint32_t scene_state; /* the event machine's state (a reference) */
static char (*scene_stems)[64];
static int scene_count, scene_next;

/* KHDAYS_TEST_SCENE_START=<ms>: START pressed that long into each scene's course (the scripted
 * input cannot: the game, held, reads none), once a course. */
static int test_start_ms = -2; /* -2 not read, -1 none */
static ULONGLONG scene_began_at;
static int test_start_done;

/* A scene video's file: a mod's cutscenes/cinematics/<stem>.mp4 first, then the player's own. */
void khdays_movie_hd_scene_file(const char *stem, char *file, size_t size)
{
    char relative[128];
    snprintf(relative, sizeof(relative), "cutscenes/cinematics/%s.mp4", stem);
    if (!khdays_mods_find(relative, file, (int)size)) {
        snprintf(file, size, "data/cutscenes/cinematics/%s.mp4", stem);
    }
}

/* The next of the scene's videos: opened (Media Foundation takes its time: never under the lock), then
 * started with its subtitles (the stem without its "hd", as the movies' are named). */
static int scene_open(int index)
{
    char file[1024], why[320];
    KhdaysHdInfo info;
    khdays_movie_hd_scene_file(scene_stems[index], file, sizeof(file));
    if (!khdays_hd_open(file, &info, why, sizeof(why))) {
        khdays_log("scene %s: %s cannot be played (%s)", scene_key, file, why);
        fprintf(stderr, "scenes: %s: %s cannot be played (%s)\n", scene_key, file, why);
        return 0;
    }
    khdays_log("scene %s: the HD video %s plays (%dx%d, %s)", scene_key, file, info.width, info.height,
               info.channels > 0 ? "with its sound" : "no sound");
    fprintf(stderr, "scenes: %s: %s shown instead (%dx%d)\n", scene_key, file, info.width, info.height);
    return 1;
}

/* Under the lock: the video just opened starts. */
static void scene_start_opened(void)
{
    const char *stem = scene_stems[scene_next];
    khdays_movie_subtitles_load(strncmp(stem, "hd", 2) == 0 ? stem + 2 : stem,
                                khdays_language_wanted() != 0 ? khdays_language_wanted() : GetLanguage());
    khdays_hd_start();
    ++scene_next;
}

/* The videos are over (ended, or stopped): the game is let go to take its own skip at its next step;
 * the window keeps the last picture until the scene's stretch ends. Under the lock. */
static void scene_videos_over(const char *how)
{
    if (scene_chapter) {
        finish(how); /* a Theater chapter's: back to its list */
        return;
    }
    khdays_hd_close();
    InterlockedExchange(&state, SCENE_SKIP);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_log("scene %s: the HD videos %s; the game's own skip is asked", scene_key, how);
    fprintf(stderr, "scenes: %s: the HD videos %s; the game's skip is asked\n", scene_key, how);
}

/* START during the videos: they pause, picture and sound, and the game is let go with START (its
 * buttons, khdays_movie_hd_game_buttons) to open its own pause over them; khdays_movie_hd_scene_frame
 * watches it. Under the lock. */
static void scene_pause_begin(void)
{
    khdays_hd_pause(1);
    pause_start_given = pause_seen = pause_frames = pause_press = 0;
    pause_stuck = 0;
    InterlockedExchange(&state, SCENE_PAUSE);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_log("scene %s: START: the video paused, the game's own pause asked", scene_key);
    fprintf(stderr, "scenes: %s: START: the video paused, the game's pause asked\n", scene_key);
}

void khdays_movie_hd_scene_frame(int pause_open, int skip_requested)
{
    if (state_now() != SCENE_PAUSE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == SCENE_PAUSE && pause_start_given) {
        ++pause_frames;
        if (pause_open) {
            pause_seen = 1;
        } else if (pause_seen || pause_frames > PAUSE_WAIT_FRAMES) {
            if (skip_requested) {
                /* its Skip: the game unwinds to the scene's end, which ends the course */
                khdays_hd_close();
                InterlockedExchange(&state, SCENE_LANDING);
                khdays_log("scene %s: the game's pause skipped the scene", scene_key);
                fprintf(stderr, "scenes: %s: the game's pause skipped the scene\n", scene_key);
            } else {
                /* closed (or never opened): the game held again, the video on */
                khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
                khdays_hd_pause(0);
                InterlockedExchange(&state, SCENE);
                khdays_log("scene %s: the game's pause %s; the video goes on", scene_key,
                           pause_seen ? "closed" : "did not open");
                fprintf(stderr, "scenes: %s: the game's pause %s; the video goes on\n", scene_key,
                        pause_seen ? "closed" : "did not open");
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_hd_scene_paused(void)
{
    return state_now() == SCENE_PAUSE;
}

uint32_t khdays_movie_hd_scene_state(void)
{
    const LONG now = state_now();
    return now == SCENE || now == SCENE_SKIP || now == SCENE_LANDING || now == SCENE_PAUSE ? scene_state : 0u;
}

static int scene_course(const char *key, uint32_t event_state, char *const *stems, int count, int chapter)
{
    char file[1024];
    if (count <= 0 || state_now() != IDLE || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD)) {
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        khdays_movie_hd_scene_file(stems[i], file, sizeof(file));
        if (GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) {
            if (tell_once(key)) {
                khdays_log("scene %s: no HD video %s; the game's scene plays", key, file);
                fprintf(stderr, "scenes: %s: no HD video %s; the game's scene plays\n", key, file);
            }
            return 0;
        }
    }
    AcquireSRWLockExclusive(&lock);
    {
        char (*grown)[64] = realloc(scene_stems, (size_t)count * sizeof(*scene_stems));
        if (grown == NULL) {
            ReleaseSRWLockExclusive(&lock);
            return 0;
        }
        scene_stems = grown;
    }
    for (int i = 0; i < count; ++i) {
        snprintf(scene_stems[i], sizeof(scene_stems[i]), "%s", stems[i]);
    }
    scene_count = count;
    scene_chapter = chapter;
    scene_next = 0;
    scene_state = event_state;
    snprintf(scene_key, sizeof(scene_key), "%s", key);
    snprintf(id, sizeof(id), "%s", key);
    skippable = 1;
    game_last = host_last = 0xffff; /* what is held now is no press */
    ReleaseSRWLockExclusive(&lock);
    /* opened outside the lock: only this thread starts a course */
    if (!scene_open(0)) {
        if (tell_once(key)) {
            khdays_log("scene %s: the game's scene plays", key);
        }
        return 0;
    }
    AcquireSRWLockExclusive(&lock);
    scene_start_opened();
    khdays_sound_output_mute(1);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1); /* the game stops at its next wait */
    scene_began_at = GetTickCount64();
    test_start_done = 0;
    InterlockedExchange(&state, SCENE);
    ReleaseSRWLockExclusive(&lock);
    /* the held game sends no frames: without this the window may sleep on through the video, every
     * picture passed over (seen: 3.8 s of hd002's 4.1 s) */
    khdays_host_wake();
    return 1;
}

int khdays_movie_hd_scene(const char *key, uint32_t event_state, char *const *stems, int count)
{
    return scene_course(key, event_state, stems, count, 0);
}

int khdays_movie_hd_chapter(const char *key, char *const *stems, int count)
{
    return scene_course(key, 0u, stems, count, 1);
}

void khdays_movie_hd_scene_step(uint32_t event_state, void *host_state)
{
    if (state_now() != SCENE_SKIP) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == SCENE_SKIP && event_state == scene_state) {
        SoundMgr_StopAllSePlayers();
        *(volatile uint32_t *)((uint8_t *)host_state + SCENE_SKIP_REQUEST) = 1u;
        InterlockedExchange(&state, SCENE_LANDING);
        khdays_log("scene %s: the game's skip asked", scene_key);
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_movie_hd_voice(const char *key, char *const *stems, int count)
{
    char **files;
    int ok = 1;
    if (count <= 0 || !(khdays_host_graphics_flags() & KHDAYS_HOST_MOVIES_HD)) {
        return 0;
    }
    files = (char **)calloc((size_t)count, sizeof(*files));
    if (files == NULL) {
        return 0;
    }
    for (int i = 0; i < count && ok; ++i) {
        files[i] = (char *)malloc(1024);
        if (files[i] == NULL) {
            ok = 0;
            break;
        }
        khdays_movie_hd_scene_file(stems[i], files[i], 1024);
        if (GetFileAttributesA(files[i]) == INVALID_FILE_ATTRIBUTES) {
            if (tell_once(key)) {
                khdays_log("scene %s: no HD video %s; the game's page alone", key, files[i]);
                fprintf(stderr, "scenes: %s: no HD video %s; the game's page alone\n", key, files[i]);
            }
            ok = 0;
        }
    }
    if (ok) {
        khdays_hd_voice_play((const char *const *)files, count);
        khdays_log("scene %s: the voice of %s heard over the game's page", key, files[0]);
        fprintf(stderr, "scenes: %s: the voice of %s heard over the game's page\n", key, files[0]);
    }
    for (int i = 0; i < count; ++i) {
        free(files[i]);
    }
    free(files);
    return ok;
}

void khdays_movie_hd_voice_stop(void)
{
    khdays_hd_voice_stop();
}

void khdays_movie_hd_scene_ended(void)
{
    LONG now = state_now();
    if (now != SCENE && now != SCENE_SKIP && now != SCENE_LANDING && now != SCENE_PAUSE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    now = state_now();
    if (now == SCENE || now == SCENE_SKIP || now == SCENE_LANDING || now == SCENE_PAUSE) {
        finish(now == SCENE_LANDING ? "ended, the game's scene skipped to its end" : "ended with its scene");
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_poll(void)
{
    char why[320];
    int status;
    if (state_now() == SCENE) {
        int open_next = 0;
        AcquireSRWLockExclusive(&lock);
        if (test_start_ms == -2) {
            const char *v = getenv("KHDAYS_TEST_SCENE_START");
            test_start_ms = v != NULL ? atoi(v) : -1;
        }
        if (state_now() == SCENE && test_start_ms >= 0 && !test_start_done &&
            GetTickCount64() - scene_began_at >= (ULONGLONG)test_start_ms) {
            test_start_done = 1;
            fprintf(stderr, "scenes: %s: KHDAYS_TEST_SCENE_START: START pressed\n", scene_key);
            request_skip();
        }
        if (state_now() == SCENE) {
            status = khdays_hd_status(why, sizeof(why));
            if (status == -1) {
                khdays_log("scene %s: the HD video stopped part-way (%s)", scene_key, why);
                scene_videos_over("stopped part-way");
            } else if (status == 1 && scene_next >= scene_count) {
                scene_videos_over("ended");
            } else if (status == 1) {
                open_next = 1;
            }
        }
        ReleaseSRWLockExclusive(&lock);
        if (open_next) {
            /* the next video opened here, the presenter's own thread, outside the lock (the opening
             * takes its time): nothing asks it for pictures meanwhile, the window keeps the last */
            const int opened = scene_open(scene_next);
            AcquireSRWLockExclusive(&lock);
            if (state_now() == SCENE) {
                if (opened) {
                    scene_start_opened();
                } else {
                    scene_videos_over("ended (the next could not be played)");
                }
            }
            ReleaseSRWLockExclusive(&lock);
        }
        return;
    }
    if (state_now() != PLAYING && state_now() != HELD) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() == PLAYING || state_now() == HELD) {
        status = khdays_hd_status(why, sizeof(why));
        if (status == -1) {
            khdays_log("movie %s: the HD video stopped part-way (%s); the game's movie shows", id, why);
            fprintf(stderr, "movies: %s: the HD video stopped part-way (%s)\n", id, why);
            finish("stopped part-way");
        } else if (status == 1 && (state_now() == HELD || course_credits)) {
            finish("ended");
        } else if (status == 1) {
            InterlockedExchange(&state, FINISHING);
            khdays_log("movie %s: the HD video ended before the game's movie; its last picture stays", id);
            fprintf(stderr, "movies: %s: the HD video ended before the game's movie\n", id);
        }
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_movie_hd_host_buttons(uint16_t buttons)
{
    if (state_now() == IDLE) {
        return;
    }
    AcquireSRWLockExclusive(&lock);
    if (state_now() != IDLE) {
        const uint16_t pressed = (uint16_t)(buttons & ~host_last);
        host_last = buttons;
        if (pressed & KHDAYS_KEY_START) {
            request_skip();
        }
    }
    ReleaseSRWLockExclusive(&lock);
}
