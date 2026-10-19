/* movie_hd.h where the system has no video decoder the port uses (all but Windows, for now): no HD
 * video opens, so each movie plays as the game's own. */
#include "movie_hd.h"

#include <stdio.h>

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    (void)path;
    (void)info;
    if (why != NULL && size > 0) {
        snprintf(why, size, "HD videos are not played on this system");
    }
    return 0;
}

void khdays_hd_start(void)
{
}

void khdays_hd_pause(int pause)
{
    (void)pause;
}

void khdays_hd_close(void)
{
}

int khdays_hd_status(char *why, size_t size)
{
    (void)why;
    (void)size;
    return 2;
}

void khdays_hd_set_gain(float gain)
{
    (void)gain;
}

int khdays_hd_picture_begin(KhdaysHdPicture *picture)
{
    (void)picture;
    return 0;
}

void khdays_hd_picture_done(void)
{
}

int khdays_hd_picture_rgba(uint8_t **rgba, size_t *capacity, int *w, int *h)
{
    (void)rgba;
    (void)capacity;
    (void)w;
    (void)h;
    return 0;
}

void khdays_hd_voice_play(const char *const *paths, int count)
{
    (void)paths;
    (void)count;
}

void khdays_hd_voice_stop(void)
{
}

void khdays_hd_voice_pause(int pause)
{
    (void)pause;
}

void khdays_hd_voice_set_gain(float gain)
{
    (void)gain;
}
