/* touch_needs.h */
#include "touch_needs.h"

#include "../../ui/situation.h"

int khdays_touch_needs_mode(const KhdaysTouchSituation *s)
{
    if (s->settings) {
        return KHDAYS_TOUCH_MODE_MENU; /* its text and arrows are small under a finger */
    }
    if (s->port_menu) {
        return KHDAYS_TOUCH_MODE_NONE;
    }
    if (s->hd_movie) {
        return s->movie_pause ? KHDAYS_TOUCH_MODE_MENU : KHDAYS_TOUCH_MODE_TALK;
    }
    switch (s->scene) {
    case KHDAYS_SIT_FIELD:
    case KHDAYS_SIT_FIELD_3D_BOTH: return s->event ? KHDAYS_TOUCH_MODE_EVENT : KHDAYS_TOUCH_MODE_FIELD;
    case KHDAYS_SIT_MOVIE:
    case KHDAYS_SIT_INTRO:
    case KHDAYS_SIT_DAY_COUNTER:
    case KHDAYS_SIT_THOUGHTS:
    case KHDAYS_SIT_THE_END: return s->movie_pause ? KHDAYS_TOUCH_MODE_MENU : KHDAYS_TOUCH_MODE_TALK;
    default: return KHDAYS_TOUCH_MODE_MENU; /* the title, the camp's pages and tutorials, saves, the shop, reviews */
    }
}

int khdays_touch_needs_settled(int mode, int *shown, int *waited)
{
    if (mode == KHDAYS_TOUCH_MODE_FIELD && *shown == KHDAYS_TOUCH_MODE_EVENT && ++*waited < KHDAYS_TOUCH_SETTLE_FRAMES) {
        return *shown;
    }
    *waited = 0;
    *shown = mode;
    return mode;
}

int khdays_touch_needs_window(int one_picture, int second_display)
{
    return one_picture || !second_display;
}
