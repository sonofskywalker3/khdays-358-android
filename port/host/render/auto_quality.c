/* Auto's search and its watch over play (auto_quality.h). */
#include "auto_quality.h"

static int clamp_scale(int n, int most)
{
    if (most < KHDAYS_AUTO_SCALE_MIN) {
        most = KHDAYS_AUTO_SCALE_MIN;
    }
    return n < KHDAYS_AUTO_SCALE_MIN ? KHDAYS_AUTO_SCALE_MIN : n > most ? most : n;
}

void khdays_auto_search_start(KhdaysAutoSearch *s, const KhdaysAutoSetup *setup)
{
    s->setup = *setup;
    s->setup.upper_max = clamp_scale(setup->upper_max, setup->upper_max);
    s->setup.lower_max = clamp_scale(setup->lower_max, setup->lower_max);
    s->have_best = 0;
    s->best_upper = s->best_lower = s->best_fps = s->last_fps = 0;
    s->upper = s->setup.upper_auto ? s->setup.upper_max : s->setup.upper_fixed;
    s->lower = s->setup.lower_auto ? KHDAYS_AUTO_SCALE_MIN : s->setup.lower_fixed;
    /* nothing on auto: nothing to search (the pair as chosen is measured once) */
    s->phase = s->setup.upper_auto ? 0 : 1;
}

static void keep(KhdaysAutoSearch *s, int fps)
{
    s->have_best = 1;
    s->best_upper = s->upper;
    s->best_lower = s->lower;
    s->best_fps = fps;
}

int khdays_auto_search_measured(KhdaysAutoSearch *s, int fps)
{
    const int held = fps >= s->setup.target;
    s->last_fps = fps;
    if (s->phase == 0) {
        if (held) {
            keep(s, fps);
            s->phase = 1;
        } else if (s->upper > KHDAYS_AUTO_SCALE_MIN) {
            --s->upper; /* the next sharpest */
            return 1;
        } else {
            s->phase = 2; /* none held: the least */
            khdays_auto_search_finish(s);
            return 0;
        }
    } else if (s->phase == 1) {
        if (!held) {
            s->phase = 2;
            khdays_auto_search_finish(s);
            return 0;
        }
        keep(s, fps);
    }
    /* the lower up a step while the pair holds */
    if (s->phase == 1 && s->setup.lower_auto && s->lower < s->setup.lower_max) {
        ++s->lower;
        return 1;
    }
    s->phase = 2;
    khdays_auto_search_finish(s);
    return 0;
}

void khdays_auto_search_finish(KhdaysAutoSearch *s)
{
    s->phase = 2;
    if (!s->have_best) {
        s->best_upper = s->upper;
        s->best_lower = s->lower;
        s->best_fps = s->last_fps;
        s->have_best = 1;
    }
}

void khdays_auto_watch_second(KhdaysAutoWatch *w, int pictures, int floor)
{
    if (pictures >= floor) {
        w->low_seconds = 0;
        return;
    }
    if (++w->low_seconds >= KHDAYS_AUTO_LOW_SECONDS) {
        w->owed = 1;
        w->low_seconds = 0;
    }
}

int khdays_auto_step_down(int upper_auto, int lower_auto, int *upper, int *lower)
{
    if (lower_auto && *lower > KHDAYS_AUTO_SCALE_MIN) {
        --*lower;
        return 1;
    }
    if (upper_auto && *upper > KHDAYS_AUTO_SCALE_MIN) {
        --*upper;
        return 1;
    }
    return 0;
}
