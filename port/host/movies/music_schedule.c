/* An HD video's music schedule (music_schedule.h). */
#include "music_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Grows `*items` (of `size` bytes each) to hold one more than `count`; 0 when memory runs out. */
static int room_for_one(void **items, int count, int *capacity, size_t size)
{
    if (count < *capacity) {
        return 1;
    }
    {
        const int grown = *capacity ? *capacity * 2 : 8;
        void *more = realloc(*items, (size_t)grown * size);
        if (more == NULL) {
            return 0;
        }
        *items = more;
        *capacity = grown;
    }
    return 1;
}

int khdays_music_schedule_read(const char *path, KhdaysMusicSchedule *out)
{
    char line[512];
    int cue_capacity = 0, ramp_capacity = 0;
    FILE *f = fopen(path, "r");
    memset(out, 0, sizeof(*out));
    out->rate = 30;
    if (f == NULL) {
        return 0;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        KhdaysMusicCue cue;
        KhdaysMusicRamp ramp;
        if (sscanf(line, "rate %d", &out->rate) == 1) {
            continue;
        }
        if (strncmp(line, "from ", 5) == 0) {
            out->from_field = strncmp(line + 5, "field", 5) == 0;
        } else if (strncmp(line, "to ", 3) == 0) {
            out->to_field = strncmp(line + 3, "field", 5) == 0;
        } else if (sscanf(line, "cue %d %d %63s %lld", &cue.first, &cue.end, cue.track, &cue.loop) == 4) {
            if (!room_for_one((void **)&out->cues, out->cue_count, &cue_capacity, sizeof(cue))) {
                break;
            }
            out->cues[out->cue_count++] = cue;
        } else if (sscanf(line, "volume %d %d %d", &ramp.first, &ramp.end, &ramp.level) == 3) {
            if (!room_for_one((void **)&out->ramps, out->ramp_count, &ramp_capacity, sizeof(ramp))) {
                break;
            }
            out->ramps[out->ramp_count++] = ramp;
        }
    }
    fclose(f);
    if (out->cue_count == 0 || out->rate <= 0) {
        khdays_music_schedule_free(out);
        return 0;
    }
    return 1;
}

void khdays_music_schedule_free(KhdaysMusicSchedule *s)
{
    free(s->cues);
    free(s->ramps);
    memset(s, 0, sizeof(*s));
}

int khdays_music_cue_at(const KhdaysMusicSchedule *s, double frame)
{
    for (int i = 0; i < s->cue_count; ++i) {
        if (frame >= s->cues[i].first && frame < s->cues[i].end) {
            return i;
        }
    }
    return -1;
}

double khdays_music_level_at(const KhdaysMusicSchedule *s, double frame)
{
    double level = 100.0;
    for (int i = 0; i < s->ramp_count; ++i) {
        const KhdaysMusicRamp *r = &s->ramps[i];
        if (frame < r->first) {
            break;
        }
        if (frame >= r->end) {
            level = r->level;
        } else {
            level += (r->level - level) * (frame - r->first) / (double)(r->end - r->first);
            break;
        }
    }
    return level < 0.0 ? 0.0 : level / 100.0;
}

long long khdays_music_track_sample(const KhdaysMusicCue *cue, double frame, int video_rate, int rate, long long length)
{
    long long sample = (long long)((frame - cue->first) / video_rate * rate);
    if (sample < 0) {
        sample = 0;
    }
    if (sample < length) {
        return sample;
    }
    if (cue->loop < 0 || cue->loop >= length) {
        return -1;
    }
    return cue->loop + (sample - cue->loop) % (length - cue->loop);
}
