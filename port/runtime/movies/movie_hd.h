/* The course of an HD movie: with an HD video (data/cutscenes/cinematics/hd<id>.mp4) and Video `hd`,
 * the window shows it with its sound while the game plays its own movie unseen and silent, then holds
 * the game until the video ends; START stops it where the DS lets the movie be skipped. */
#ifndef KHDAYS_MOVIE_HD_H
#define KHDAYS_MOVIE_HD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's thread. A stream of a movie opens (Ov024_MobiClip_OpenStreamSlot,
 * the path as the game built it); `replaced` when a mod's movie takes it. */
void khdays_movie_hd_stream(int slot, const char *path, int replaced);

/* The game's movie stops (Ov024_MobiClip_StopPlayback). */
void khdays_movie_hd_stopped(void);

/* The game's reset (start.c): an HD video showing is closed, the game shown
 * and heard again, its clock let go. */
void khdays_movie_hd_reset(void);

/* The credits scene, ov011: 1 it begins (Ov011_CreateScene), 2 its staff roll ends
 * (Ov011_StartTitleExit), 0 it ends. The HD video carries the roll too, so the game runs its roll
 * behind the video and is held where the roll ends if the video still plays. */
void khdays_movie_hd_credits(int phase);

/* The movie's loop, each turn (Ov024_MobiClip_UpdatePlayback's player flags,
 * Ov012_RunOpeningScene's with bit 3 set): bit 3 the DS lets START skip it,
 * bit 2 it has been skipped. */
void khdays_movie_hd_flags(unsigned flags);

/* The DS buttons the game is given, from those pressed (the player's and a
 * script's): none of them while a course runs -- only START while the port
 * skips the game's movie -- and, after it, none of those still held from it
 * until they are let go. A START pressed during a course asks for the skip. */
uint16_t khdays_movie_hd_game_buttons(uint16_t buttons);

/* Either thread: whether a course runs (the window shows the HD video). */
int khdays_movie_hd_showing(void);
/* Either thread: whether the game's own movie plays (a stream opened, its playback not stopped). */
int khdays_movie_game_playing(void);

/* The host's thread, each refresh while a course runs: the video's end or
 * failure. */
void khdays_movie_hd_poll(void);

/* The host's thread: the player's DS buttons now (while the game is held its
 * thread reads none). */
void khdays_movie_hd_host_buttons(uint16_t buttons);

/* The theater's HD scenes. The game's thread, as an event's scene begins its skippable
 * stretch (`state` the event machine's, a reference): with Video `hd`, the videos `stems` in a row
 * (data/cutscenes/cinematics/<stem>.mp4, a mod's cutscenes/cinematics/ first) shown in its place, the
 * game held meanwhile. Returns 1 when the course starts, 0 when the scene plays as the game's (no
 * Video `hd`, a video missing or unplayable: said in the log once). */
int khdays_movie_hd_scene(const char *key, uint32_t state, char *const *stems, int count);

/* The game's thread, at each step of a script machine (`state` its reference): once the scene's videos
 * have ended, the game's own skip is asked there (the sound effects stopped, the skip request set),
 * as the event pause's Skip does. */
void khdays_movie_hd_scene_step(uint32_t state, void *host_state);

/* The game's thread: the scene's stretch has ended (its skip target cleared, or its event closed):
 * the course ends and the game is shown again. */
void khdays_movie_hd_scene_ended(void);

/* The game's thread, each frame: START during a scene's videos paused them and let the game open its
 * own pause (Continue, Skip); `pause_open` whether it is open (PauseMenu_GetMode), `skip_requested`
 * whether the event's skip request is set. Once it closes, Skip leaves the game to unwind to the
 * scene's end; Continue (or a pause that never opened) holds the game again and the video goes on. */
void khdays_movie_hd_scene_frame(int pause_open, int skip_requested);

/* Whether the game's pause is over a scene's paused video (the window draws the pause over it). */
int khdays_movie_hd_scene_paused(void);

/* The event machine's state (a reference) of the scene whose videos are shown, 0 when none. */
uint32_t khdays_movie_hd_scene_state(void);

/* A Theater chapter's videos in a row (host/screens/cine_screen.c), the game's list held under them,
 * as a scene's are; START or their end back to the list. Returns 1 when they start, 0 when not (no
 * Video `hd`, a video missing). */
int khdays_movie_hd_chapter(const char *key, char *const *stems, int count);

/* A scene video's file: a mod's cutscenes/cinematics/<stem>.mp4 first, then the player's own. */
void khdays_movie_hd_scene_file(const char *stem, char *file, size_t size);

/* The game's thread, as a day's monologue begins (scene 10): with Video `hd`, the sound of the videos
 * `stems` in a row (the theater's voice-only clips; their pictures are black) heard over the game's
 * own page, the game going on as on the DS. Returns 1 when the voice starts, 0 when there is none (no
 * Video `hd`, a video missing: said in the log once). */
int khdays_movie_hd_voice(const char *key, char *const *stems, int count);

/* The game's thread: the monologue has ended; its voice stops if it still speaks. */
void khdays_movie_hd_voice_stop(void);

#ifdef __cplusplus
}
#endif

#endif
