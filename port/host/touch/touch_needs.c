/* touch_needs.h */
#include "touch_needs.h"

#include "../../ui/situation.h"

int khdays_touch_needs_mode(const KhdaysTouchSituation *s)
{
    if (s->port_menu) {
        return KHDAYS_TOUCH_MODE_NONE;
    }
    if (s->hd_movie) {
        return KHDAYS_TOUCH_MODE_TALK;
    }
    switch (s->scene) {
    case KHDAYS_SIT_FIELD:
    case KHDAYS_SIT_FIELD_3D_BOTH: return s->hud ? KHDAYS_TOUCH_MODE_FIELD : KHDAYS_TOUCH_MODE_TALK;
    case KHDAYS_SIT_MOVIE:
    case KHDAYS_SIT_INTRO:
    case KHDAYS_SIT_DAY_COUNTER:
    case KHDAYS_SIT_THOUGHTS:
    case KHDAYS_SIT_THE_END: return KHDAYS_TOUCH_MODE_TALK;
    default: return KHDAYS_TOUCH_MODE_MENU; /* the title, the camp's pages and tutorials, saves, the shop, reviews */
    }
}

int khdays_touch_needs_window(int one_picture, int second_display)
{
    return one_picture || !second_display;
}
