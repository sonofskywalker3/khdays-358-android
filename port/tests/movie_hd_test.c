/* How an HD movie runs (runtime/movies/movie_hd.c), against a scripted game and video: the ids, the
 * lookup, every transition, the buttons the game is given and the clock's pause reasons (with the real
 * runtime/core/clock.c); an event scene's videos and a monologue's voice. It runs in a
 * folder of its own, which it creates and leaves behind. */
#include "../host/movies/movie_hd.h"
#include "../host/app/host.h"
#include "../host/app/sound_output.h"
#include "../runtime/core/clock.h"
#include "../runtime/input/input.h"
#include "../runtime/movies/movie_hd.h"

#include <direct.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "movie_hd_test:%d: failed: %s\n", __LINE__, #c);                                           \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* --- what the course calls, scripted --- */
static int graphics_flags = KHDAYS_HOST_MOVIES_HD;
static int open_ok = 1, opens, closes, started, muted;
static int status_now; /* what khdays_hd_status says */
static char opened_path[260];
static int told_cannot; /* log lines saying a file cannot be played */

int khdays_host_graphics_flags(void)
{
    return graphics_flags;
}

/* the window woken as a scene's video begins (the held game sends no frames) */
static int wakes;
void khdays_host_wake(void)
{
    ++wakes;
}

/* A mod's video (mods/<Mod>/movies/hd<id>.mp4, as khdays-studio saves one):
 * the one mod holds hd843's once the test gives it. */
static int mod_has_843;
int khdays_mods_find(const char *relative, char *path, int size)
{
    if (!mod_has_843 || strcmp(relative, "movies/hd843.mp4") != 0) {
        return 0;
    }
    snprintf(path, (size_t)size, "mods/m/movies/hd843.mp4");
    return 1;
}

/* the game's: what the event pause's Skip stops (the scenes' course asks it) */
static int sound_effects_stopped;
void SoundMgr_StopAllSePlayers(void)
{
    ++sound_effects_stopped;
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    snprintf(opened_path, sizeof(opened_path), "%s", path);
    if (!open_ok) {
        snprintf(why, size, "scripted failure");
        return 0;
    }
    ++opens;
    started = 0;
    status_now = 0;
    info->width = 1920;
    info->height = 1080;
    info->fps = 60.0;
    info->channels = 2;
    info->rate = 48000;
    return 1;
}
void khdays_hd_start(void)
{
    started = 1;
}
static int hd_paused; /* the decoder paused (START's pause over a scene) */
void khdays_hd_pause(int pause)
{
    hd_paused = pause;
}
void khdays_hd_close(void)
{
    ++closes;
}
int khdays_hd_status(char *why, size_t size)
{
    if (status_now == -1) {
        snprintf(why, size, "scripted failure part-way");
    }
    return status_now;
}
void khdays_sound_output_mute(int mute)
{
    muted = mute;
}
/* a monologue's voice: the files asked for, and the stops */
static char voice_path[260];
static int voices, voice_files, voice_stops;
void khdays_hd_voice_play(const char *const *paths, int count)
{
    snprintf(voice_path, sizeof(voice_path), "%s", count > 0 ? paths[0] : "");
    voice_files = count;
    ++voices;
}
void khdays_hd_voice_stop(void)
{
    ++voice_stops;
}
/* the subtitles asked for when a video starts: its movie, in the game's language */
static char subtitles_movie[16];
static int subtitles_language;
void khdays_movie_subtitles_load(const char *movie, int language)
{
    snprintf(subtitles_movie, sizeof(subtitles_movie), "%s", movie);
    subtitles_language = language;
}
int GetLanguage(void)
{
    return 5;
}
int khdays_language_wanted(void)
{
    return 0;
}
void khdays_log(const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (strstr(line, "cannot be played") != NULL) {
        ++told_cannot;
    }
}

static void touch(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        fclose(f);
    }
}

static int held(void)
{
    return khdays_clock_held(KHDAYS_HOLD_MOVIE);
}

int main(void)
{
    char folder[MAX_PATH], previous[MAX_PATH];
    const uint16_t START = KHDAYS_KEY_START, A = KHDAYS_KEY_A;
    GetCurrentDirectoryA(MAX_PATH, previous);
    GetTempPathA(MAX_PATH, folder);
    strncat(folder, "khdays-movie-hd-test", MAX_PATH - strlen(folder) - 1);
    _mkdir(folder);
    SetCurrentDirectoryA(folder);
    _mkdir("data");
    _mkdir("data/cutscenes");
    _mkdir("data/cutscenes/cinematics");
    touch("data/cutscenes/cinematics/hd802.mp4");
    touch("data/cutscenes/cinematics/hd843.mp4");
    khdays_clock_init();

    /* the ids and the lookup */
    khdays_movie_hd_stream(1, "/mv/839_es.mods", 0); /* no file: the game's movie */
    CHECK(!khdays_movie_hd_showing() && opens == 0);
    khdays_movie_hd_stream(0, "/mv/802.mods", 1); /* a mod's movie takes it */
    CHECK(!khdays_movie_hd_showing() && opens == 0);
    graphics_flags = 0; /* movies = pc or ds, or no window */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    CHECK(!khdays_movie_hd_showing() && opens == 0);
    graphics_flags = KHDAYS_HOST_MOVIES_HD;

    /* watched to its end: held at the game's movie's end, let go at the video's */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd802.mp4") == 0);
    CHECK(khdays_movie_hd_showing() && started && muted);
    CHECK(strcmp(subtitles_movie, "802") == 0 && subtitles_language == 5); /* its subtitles, in the game's language */
    CHECK(khdays_movie_hd_game_buttons(A) == 0);                           /* the player's buttons withheld */
    khdays_movie_hd_stream(2, "/mv/802.mods", 0);                          /* a second stream: the same course */
    CHECK(opens == 1);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    CHECK(khdays_movie_hd_showing() && held());
    khdays_movie_hd_poll(); /* still playing */
    CHECK(held());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held() && !muted && closes == 1);
    CHECK(khdays_movie_hd_game_buttons(A) == 0); /* held when it ended: withheld until let go */
    CHECK(khdays_movie_hd_game_buttons(0) == 0);
    CHECK(khdays_movie_hd_game_buttons(A) == A);
    khdays_movie_hd_game_buttons(0);

    /* the video ends first: its last picture until the game's movie ends */
    khdays_movie_hd_stream(1, "843.z", 0);
    CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd843.mp4") == 0);
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(khdays_movie_hd_showing() && !held());
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing() && !held() && !muted);
    khdays_movie_hd_game_buttons(0);

    /* a mod's video before the player's own */
    mod_has_843 = 1;
    _mkdir("mods");
    _mkdir("mods/m");
    _mkdir("mods/m/movies");
    touch("mods/m/movies/hd843.mp4");
    khdays_movie_hd_stream(1, "843.z", 0);
    CHECK(strcmp(opened_path, "mods/m/movies/hd843.mp4") == 0);
    status_now = 1;
    khdays_movie_hd_poll();
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);
    mod_has_843 = 0;

    /* START where the DS lets skip: the video stops, START given until the game skips */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    {
        const int closed_before = closes;
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* pressed: the skip, START given */
        CHECK(closes == closed_before + 1 && khdays_movie_hd_showing());
    }
    CHECK(khdays_movie_hd_game_buttons(START) == START);
    khdays_movie_hd_flags(8 | 4); /* the game took the skip */
    CHECK(khdays_movie_hd_game_buttons(START) == 0);
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing() && !muted && !held());
    CHECK(khdays_movie_hd_game_buttons(START) == 0); /* still held from the course */
    CHECK(khdays_movie_hd_game_buttons(0) == 0);
    CHECK(khdays_movie_hd_game_buttons(START) == START); /* a new press is the game's */
    khdays_movie_hd_game_buttons(0);

    /* START before the movie said: decided when it says */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_game_buttons(START);
    khdays_movie_hd_flags(8);
    CHECK(khdays_movie_hd_game_buttons(START) == START);
    khdays_movie_hd_flags(8 | 4);
    khdays_movie_hd_stopped();
    CHECK(!khdays_movie_hd_showing());
    khdays_movie_hd_game_buttons(0);

    /* where the DS does not let skip: START does nothing, playing or held */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(0);
    CHECK(khdays_movie_hd_game_buttons(START) == 0 && khdays_movie_hd_showing());
    khdays_movie_hd_stopped();
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    CHECK(khdays_movie_hd_showing() && held());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);

    /* held, the player's START from the host: let go at once */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    CHECK(held());
    khdays_movie_hd_host_buttons(0);
    khdays_movie_hd_host_buttons(START);
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);

    /* the settings menu during a hold: closing it leaves the game held */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
    CHECK(held());
    {
        const u64 a = khdays_clock_cycles();
        Sleep(20);
        CHECK(khdays_clock_cycles() == a); /* the time stands still */
    }
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!held());
    {
        const u64 a = khdays_clock_cycles();
        Sleep(20);
        CHECK(khdays_clock_cycles() > a);
    }
    khdays_movie_hd_game_buttons(0);

    /* the credits scene: not held at its movie's end; held where
     * its roll ends if the video still plays (the story: START does nothing) */
    khdays_movie_hd_credits(1);
    khdays_movie_hd_stream(1, "/mv/843.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(0);
    khdays_movie_hd_stopped();
    CHECK(khdays_movie_hd_showing() && !held()); /* the roll runs on behind the video */
    CHECK(khdays_movie_hd_game_buttons(START) == 0 && khdays_movie_hd_showing());
    khdays_movie_hd_credits(2);
    CHECK(khdays_movie_hd_showing() && held());
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_credits(0);
    khdays_movie_hd_game_buttons(0);
    /* ... the video ends first: the game shown wherever the roll is */
    khdays_movie_hd_credits(1);
    khdays_movie_hd_stream(1, "/mv/843.mods", 0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    status_now = 1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !held() && !muted);
    khdays_movie_hd_credits(0);
    khdays_movie_hd_game_buttons(0);
    /* ... the Theater's (its argument lets skip): START during the roll, the scene leaves */
    khdays_movie_hd_credits(1);
    khdays_movie_hd_stream(1, "/mv/843.mods", 0);
    khdays_movie_hd_game_buttons(0);
    khdays_movie_hd_flags(8);
    khdays_movie_hd_stopped();
    CHECK(khdays_movie_hd_game_buttons(START) == START && khdays_movie_hd_showing());
    khdays_movie_hd_credits(0);
    CHECK(!khdays_movie_hd_showing() && !held());
    khdays_movie_hd_game_buttons(0);

    /* failing part-way: the game's movie shown from where it is */
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    status_now = -1;
    khdays_movie_hd_poll();
    CHECK(!khdays_movie_hd_showing() && !muted && !held());
    khdays_movie_hd_game_buttons(0);

    /* an event scene of two videos: the game held while they play, then let go to take
     * its own skip at its event's next step, shown until the scene ends */
    {
        static uint8_t event[0x600]; /* the event machine's state: its skip request at +0x588 */
        char *const stems[] = {"hd010", "hd011"};
        const int opened_before = opens, stopped_before = sound_effects_stopped;
        touch("data/cutscenes/cinematics/hd010.mp4");
        touch("data/cutscenes/cinematics/hd011.mp4");
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, 2) == 1);
        CHECK(khdays_movie_hd_showing() && held() && muted && opens == opened_before + 1 && wakes == 1);
        CHECK(strcmp(opened_path, "data/cutscenes/cinematics/hd010.mp4") == 0 && strcmp(subtitles_movie, "010") == 0);
        CHECK(khdays_movie_hd_game_buttons(A) == 0);
        status_now = 1;
        khdays_movie_hd_poll(); /* the first ended: the second */
        CHECK(opens == opened_before + 2 && strcmp(subtitles_movie, "011") == 0 && held());
        status_now = 1;
        khdays_movie_hd_poll(); /* the last ended: let go */
        CHECK(khdays_movie_hd_showing() && !held() && muted);
        CHECK(khdays_movie_hd_game_buttons(START) == 0); /* a scene's own skip: no button given */
        khdays_movie_hd_scene_step(0x9999u, event);      /* another event's step */
        CHECK(*(uint32_t *)(event + 0x588) == 0);
        khdays_movie_hd_scene_step(0x1234u, event);
        CHECK(*(uint32_t *)(event + 0x588) == 1 && sound_effects_stopped == stopped_before + 1);
        khdays_movie_hd_scene_ended();
        CHECK(!khdays_movie_hd_showing() && !muted && !held());
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
    }

    /* a scene whose video is missing: the game's own scene plays, nothing held */
    {
        char *const stems[] = {"hd010", "hd999"};
        const int opened_before = opens;
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, 2) == 0);
        CHECK(!khdays_movie_hd_showing() && !held() && !muted && opens == opened_before);
        graphics_flags = 0; /* movies = pc or ds: none of the scenes */
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, 1) == 0 && !khdays_movie_hd_showing());
        graphics_flags = KHDAYS_HOST_MOVIES_HD;
    }

    /* START during a scene's video: the video paused, the game let go with START once to open its own
     * pause; Continue holds it again and the video goes on, Skip lets it unwind to the scene's end */
    {
        static uint8_t event[0x600];
        char *const stems[] = {"hd010"};
        const int closed_before = closes;
        touch("data/cutscenes/cinematics/hd010.mp4");
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, 1) == 1);
        CHECK(khdays_movie_hd_scene_state() == 0x1234u);
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START);
        CHECK(khdays_movie_hd_scene_paused() && khdays_movie_hd_showing() && hd_paused && !held() && muted);
        khdays_movie_hd_scene_frame(0, 0); /* before the game read its START: waits */
        CHECK(khdays_movie_hd_scene_paused());
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* START pressed for the pause ... */
        khdays_movie_hd_scene_frame(0, 0);                   /* (refused: the screen fades) */
        CHECK(khdays_movie_hd_game_buttons(START) == 0);     /* ... every other frame ... */
        khdays_movie_hd_scene_frame(0, 0);
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* ... until it opens */
        khdays_movie_hd_scene_frame(1, 0);                   /* the pause open */
        CHECK(khdays_movie_hd_game_buttons(START) == 0);     /* held since: not a new press */
        CHECK(khdays_movie_hd_game_buttons(0) == 0);
        CHECK(khdays_movie_hd_game_buttons(A) == A); /* the pause's choices are the player's */
        CHECK(khdays_movie_hd_scene_paused());
        khdays_movie_hd_scene_frame(0, 0); /* closed by Continue */
        CHECK(!khdays_movie_hd_scene_paused() && khdays_movie_hd_showing() && held() && !hd_paused);
        CHECK(khdays_movie_hd_game_buttons(A) == 0); /* held for the video again: none */
        /* again, and Skip */
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START);
        khdays_movie_hd_game_buttons(START);
        khdays_movie_hd_scene_frame(1, 0);
        khdays_movie_hd_scene_frame(0, 1);
        CHECK(!khdays_movie_hd_scene_paused() && khdays_movie_hd_showing() && !held() && closes == closed_before + 1);
        khdays_movie_hd_scene_step(0x1234u, event); /* nothing more to ask: the game's own skip runs */
        CHECK(*(uint32_t *)(event + 0x588) == 0);
        khdays_movie_hd_scene_ended();
        CHECK(!khdays_movie_hd_showing() && !muted && khdays_movie_hd_scene_state() == 0u);
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
        /* a pause the game does not open: the video goes on */
        CHECK(khdays_movie_hd_scene("EV_S#12", 0x1234u, stems, 1) == 1);
        khdays_movie_hd_host_buttons(0);
        khdays_movie_hd_host_buttons(START);
        for (int i = 0; i <= 90; ++i) {
            khdays_movie_hd_game_buttons(0);
            khdays_movie_hd_scene_frame(0, 0);
        }
        CHECK(!khdays_movie_hd_scene_paused() && held() && !hd_paused);
        khdays_movie_hd_scene_ended();
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_game_buttons(0);
    }

    /* a day's monologue (scene 10): its clip's voice over the game's page, the game going on */
    {
        char *const stems[] = {"hd000"}, *const missing[] = {"hd001"};
        touch("data/cutscenes/cinematics/hd000.mp4");
        CHECK(khdays_movie_hd_voice("mnl#0", stems, 1) == 1);
        CHECK(voices == 1 && voice_files == 1 && strcmp(voice_path, "data/cutscenes/cinematics/hd000.mp4") == 0);
        CHECK(!khdays_movie_hd_showing() && !held() && !muted);
        CHECK(khdays_movie_hd_game_buttons(START) == START); /* the game's own page takes its buttons */
        khdays_movie_hd_game_buttons(0);
        khdays_movie_hd_voice_stop();
        CHECK(voice_stops == 1);
        CHECK(khdays_movie_hd_voice("mnl#9", missing, 1) == 0 && voices == 1); /* no file: the page alone */
        graphics_flags = 0;
        CHECK(khdays_movie_hd_voice("mnl#0", stems, 1) == 0 && voices == 1); /* movies = pc or ds */
        graphics_flags = KHDAYS_HOST_MOVIES_HD;
    }
    DeleteFileA("data/cutscenes/cinematics/hd000.mp4");
    DeleteFileA("data/cutscenes/cinematics/hd010.mp4");
    DeleteFileA("data/cutscenes/cinematics/hd011.mp4");

    /* failing to open: the game's movie, said once */
    open_ok = 0;
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    khdays_movie_hd_stream(1, "/mv/802.mods", 0);
    CHECK(!khdays_movie_hd_showing() && told_cannot == 1);

    DeleteFileA("data/cutscenes/cinematics/hd802.mp4");
    DeleteFileA("data/cutscenes/cinematics/hd843.mp4");
    RemoveDirectoryA("data/cutscenes/cinematics");
    RemoveDirectoryA("data/cutscenes");
    RemoveDirectoryA("data");
    SetCurrentDirectoryA(previous);
    RemoveDirectoryA(folder);
    if (failures == 0) {
        printf("movie_hd_test: all checks passed\n");
    }
    return failures != 0;
}
