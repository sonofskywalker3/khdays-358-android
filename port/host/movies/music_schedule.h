/* An HD video's music as the HD collection's 358/2 Days theater plays it over the video's sound
 * (music_schedule.c): music/<video>.txt beside the video, made from the player's copy by
 * tools/hd/theater_movies.py -- which track plays at each of the video's frames, and how loud.
 * Plain C: no SDL, no system calls but the file's reading. */
#ifndef KHDAYS_HOST_MUSIC_SCHEDULE_H
#define KHDAYS_HOST_MUSIC_SCHEDULE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysMusicCue {
    int first, end; /* the frames it plays from, and before */
    char track[64]; /* music/<track>.ogg */
    long long loop; /* the sample its track goes back to at its end; -1: it plays once */
} KhdaysMusicCue;

typedef struct KhdaysMusicRamp {
    int first, end; /* the level moves between these frames... */
    int level;      /* ...to this percent, from the level before (100 at first) */
} KhdaysMusicRamp;

typedef struct KhdaysMusicSchedule {
    int rate;             /* the video's frames a second */
    int from_field;       /* the music comes from the video before */
    int to_field;         /* it goes on into the next video */
    KhdaysMusicCue *cues; /* in the file's order */
    int cue_count;
    KhdaysMusicRamp *ramps; /* in the file's order */
    int ramp_count;
} KhdaysMusicSchedule;

/* Reads `path` into `out`. Returns 1 with at least one cue, else 0 (no file, or none). */
int khdays_music_schedule_read(const char *path, KhdaysMusicSchedule *out);

void khdays_music_schedule_free(KhdaysMusicSchedule *s);

/* The cue playing at `frame` (first <= frame < end), or -1. */
int khdays_music_cue_at(const KhdaysMusicSchedule *s, double frame);

/* The music's level at `frame`, 0-1: the ramps up to it, each from the level before. */
double khdays_music_level_at(const KhdaysMusicSchedule *s, double frame);

/* Where in its track a cue is at `frame`, in samples of `rate` a second: from the track's start at
 * the cue's first frame, back to its loop past the end of `length` samples. -1 once a track that
 * plays once has ended. */
long long khdays_music_track_sample(const KhdaysMusicCue *cue, double frame, int video_rate, int rate,
                                    long long length);

#ifdef __cplusplus
}
#endif

#endif
