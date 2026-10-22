/* The HD videos' subtitles (movie_subtitles.c): the lines the HD collection's
 * 358/2 Days theater shows over its videos, made from the player's copy by
 * tools/hd/theater_movies.py, drawn over the video the window shows
 * (runtime/movies/movie_hd.h) while the movies' subtitles are on ([video]
 * subtitles, runtime/menus/newgame.h). */
#ifndef KHDAYS_HOST_MOVIE_SUBTITLES_H
#define KHDAYS_HOST_MOVIE_SUBTITLES_H

struct SDL_Renderer; /* SDL's (the game's side includes no SDL) */

#ifdef __cplusplus
extern "C" {
#endif

/* Any thread, when a video starts: its subtitles for `language` (the
 * firmware's 1-5: en fr de it es), a mod's (mods/<Mod>/movies/subtitles/
 * <lang>/hd<movie>.txt) or the player's (data/cutscenes/cinematics/
 * subtitles/<lang>/hd<movie>.txt), or none. The ones before are dropped. */
void khdays_movie_subtitles_load(const char *movie, int language);

/* The host's thread, after the video's picture is drawn at `width` x
 * `height` (the renderer's logical size): the lines due at `time` (the
 * picture's, in seconds) of a file of `fps` frames a second. */
void khdays_movie_subtitles_draw(struct SDL_Renderer *renderer, double time, double fps, int width, int height,
                                 float scale);

/* The host's thread: its texture freed (the renderer goes, or the video is over). */
void khdays_movie_subtitles_release(void);

#ifdef __cplusplus
}
#endif

#endif
