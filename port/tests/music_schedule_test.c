/* The theater's music schedules (host/movies/music_schedule.c): a file with comments and two cues read,
 * the cue at a frame, the level through set points and ramps, a track's sample at a frame (looping,
 * playing once), and files with no cue or none at all. */
#include "../host/movies/music_schedule.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "music_schedule_test:%d: failed: %s\n", __LINE__, #c);                                     \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

static int near(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

/* `text` written to a file of the test's own, read back as a schedule. */
static int read_text(const char *text, KhdaysMusicSchedule *s)
{
    const char *path = "music_schedule_test.txt";
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return -1;
    }
    fputs(text, f);
    fclose(f);
    {
        const int got = khdays_music_schedule_read(path, s);
        remove(path);
        return got;
    }
}

int main(void)
{
    KhdaysMusicSchedule s;
    /* hd828's: two tracks, each set to 50 % and faded out at its end */
    CHECK(read_text("# hd828.mp4's music\n"
                    "rate 30\n"
                    "from stop\n"
                    "to field\n"
                    "cue 1195 3828 bgm_240 2111936\n"
                    "cue 4400 6044 bgm_241 -1\n"
                    "volume 1195 1195 50\n"
                    "volume 3748 3828 0\n"
                    "volume 4400 4400 50\n"
                    "volume 5958 6038 0\n",
                    &s) == 1);
    CHECK(s.rate == 30 && !s.from_field && s.to_field);
    CHECK(s.cue_count == 2 && s.ramp_count == 4);
    CHECK(strcmp(s.cues[0].track, "bgm_240") == 0 && s.cues[0].loop == 2111936);
    CHECK(strcmp(s.cues[1].track, "bgm_241") == 0 && s.cues[1].loop == -1);

    /* the cue at a frame: from its first frame, before its end, none between them */
    CHECK(khdays_music_cue_at(&s, 0) == -1);
    CHECK(khdays_music_cue_at(&s, 1194.9) == -1);
    CHECK(khdays_music_cue_at(&s, 1195) == 0);
    CHECK(khdays_music_cue_at(&s, 3827.9) == 0);
    CHECK(khdays_music_cue_at(&s, 3828) == -1);
    CHECK(khdays_music_cue_at(&s, 4400) == 1);
    CHECK(khdays_music_cue_at(&s, 6044) == -1);

    /* the level: 100 before any point, set at once, ramped linearly from the level before */
    CHECK(near(khdays_music_level_at(&s, 0), 1.0));
    CHECK(near(khdays_music_level_at(&s, 1195), 0.5));
    CHECK(near(khdays_music_level_at(&s, 3748), 0.5));
    CHECK(near(khdays_music_level_at(&s, 3788), 0.25));
    CHECK(near(khdays_music_level_at(&s, 3828), 0.0));
    CHECK(near(khdays_music_level_at(&s, 4000), 0.0));
    CHECK(near(khdays_music_level_at(&s, 4400), 0.5));
    CHECK(near(khdays_music_level_at(&s, 6100), 0.0));

    /* a track's sample: from its start at the cue's first frame (30 frames = 48,000 samples), back
     * to its loop past its end; a track that plays once has none past its end */
    CHECK(khdays_music_track_sample(&s.cues[0], 1195, 30, 48000, 3000000) == 0);
    CHECK(khdays_music_track_sample(&s.cues[0], 1225, 30, 48000, 3000000) == 48000);
    CHECK(khdays_music_track_sample(&s.cues[0], 1100, 30, 48000, 3000000) == 0);
    CHECK(khdays_music_track_sample(&s.cues[0], 1195 + 30 * 70, 30, 48000, 3000000) ==
          2111936 + (3360000 - 2111936) % (3000000 - 2111936));
    CHECK(khdays_music_track_sample(&s.cues[1], 4400 + 30 * 10, 30, 48000, 400000) == -1);
    CHECK(khdays_music_track_sample(&s.cues[1], 4400 + 30 * 5, 30, 48000, 400000) == 240000);
    khdays_music_schedule_free(&s);
    CHECK(s.cues == NULL && s.cue_count == 0);

    /* where the music comes from; a ramp up from nothing (hd818's) */
    CHECK(read_text("rate 30\nfrom field\nto stop\ncue 1038 2976 bgm_219 577280\n"
                    "volume 1038 1038 0\nvolume 1039 1119 50\nvolume 2896 2976 0\n",
                    &s) == 1);
    CHECK(s.from_field && !s.to_field);
    CHECK(near(khdays_music_level_at(&s, 1038), 0.0));
    CHECK(near(khdays_music_level_at(&s, 1079), 0.25));
    CHECK(near(khdays_music_level_at(&s, 1119), 0.5));
    khdays_music_schedule_free(&s);

    /* no cue, a malformed one, no file */
    CHECK(read_text("rate 30\nfrom stop\nto stop\nvolume 0 0 50\n", &s) == 0);
    CHECK(read_text("rate 30\ncue 0 bgm_222\n", &s) == 0);
    CHECK(khdays_music_schedule_read("no/such/schedule.txt", &s) == 0);
    CHECK(s.cues == NULL && s.cue_count == 0);

    if (failures == 0) {
        printf("music_schedule_test: all checks passed\n");
    }
    return failures != 0;
}
