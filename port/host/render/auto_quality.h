/* Auto ([video] internal_resolution or lower_resolution = auto): the screens' scales measured where the player is,
 * a pair kept for each screen mode (one screen, two screens), the sharpest that holds a target of pictures a second
 * (auto_quality.c). The search and the watch over play are here, apart from the window: gpu_present.c applies each
 * candidate, counts the pictures and keeps the results. */
#ifndef KHDAYS_AUTO_QUALITY_H
#define KHDAYS_AUTO_QUALITY_H

#define KHDAYS_AUTO_TARGET 80      /* pictures a second the test asks of a pair: headroom for crowds and the heat */
#define KHDAYS_AUTO_FLOOR 60       /* in play, under this for KHDAYS_AUTO_LOW_SECONDS: a step down owed */
#define KHDAYS_AUTO_LOW_SECONDS 5
#define KHDAYS_AUTO_SCALE_MIN 2    /* the least either screen goes to */

/* What a search may change: a screen on auto is searched between KHDAYS_AUTO_SCALE_MIN and its `*_max` (the most
 * its display fits); one the player chose stays at its `*_fixed`. */
typedef struct KhdaysAutoSetup {
    int upper_auto, lower_auto;
    int upper_fixed, lower_fixed;
    int upper_max, lower_max;
    int target;
} KhdaysAutoSetup;

/* The search: the upper screen first, from its most down until a pair holds the target (the lower at its least);
 * then the lower up from its least while the pair still holds. `upper`, `lower`: the pair to measure now. */
typedef struct KhdaysAutoSearch {
    KhdaysAutoSetup setup;
    int phase; /* 0 the upper, 1 the lower, 2 done */
    int upper, lower;
    int best_upper, best_lower, best_fps, have_best;
    int last_fps; /* the last pair's */
} KhdaysAutoSearch;

void khdays_auto_search_start(KhdaysAutoSearch *s, const KhdaysAutoSetup *setup);
/* The pair now measured at `fps`: 1 and the next pair in upper/lower, or 0 when done (best_*). */
int khdays_auto_search_measured(KhdaysAutoSearch *s, int fps);
/* Stopped early (out of time): the best that held, or else the last pair tried (the least so far). */
void khdays_auto_search_finish(KhdaysAutoSearch *s);

/* The watch over play: a second's pictures at a time, while the pair is in force and the player plays. */
typedef struct KhdaysAutoWatch {
    int low_seconds; /* under KHDAYS_AUTO_FLOOR in a row */
    int owed;        /* a step down owed (taken between missions) */
} KhdaysAutoWatch;

/* `floor`: KHDAYS_AUTO_FLOOR, or less on a display that shows fewer (gpu_present.c auto_floor). */
void khdays_auto_watch_second(KhdaysAutoWatch *w, int pictures, int floor);
/* A step down for a pair: the lower one (when on auto and above its least), else the upper one (the same). 1 when a
 * step was taken. */
int khdays_auto_step_down(int upper_auto, int lower_auto, int *upper, int *lower);

#endif
