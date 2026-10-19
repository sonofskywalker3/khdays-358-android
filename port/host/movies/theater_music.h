/* The theater's music over an HD video (theater_music.c): its schedule's tracks (music_schedule.h)
 * decoded by stb_vorbis into an audio stream of their own, at the video's clock. The HD video drives
 * it (movie_hd_win.c): opened with the video, told its clock from the video's thread, and closed
 * with it -- or kept playing into the next video when the schedules say the music goes on. */
#ifndef KHDAYS_HOST_THEATER_MUSIC_H
#define KHDAYS_HOST_THEATER_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

/* A video opened from `video_path`: its schedule, music/<stem>.txt beside it, or none. The music
 * kept from the video before plays on when this one takes it from there with the same track. */
void khdays_theater_music_open(const char *video_path);

/* The video's thread, often: its clock (`seconds` of the file), whether it has started and is
 * paused. Starts, stops, moves and sets the volume of the music it says. */
void khdays_theater_music_tick(double seconds, int started, int paused);

/* The video closed. With `next_follows` (another video opens at once, as a scene's videos do), music
 * that goes on into the next video keeps playing; otherwise, or when it does not go on, it fades out. */
void khdays_theater_music_close(int next_follows);

/* The music's volume (0-1): the master volume times the music's. Any thread. */
void khdays_theater_music_set_gain(float gain);

#ifdef __cplusplus
}
#endif

#endif
