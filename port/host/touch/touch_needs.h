/* Which of the on-screen controls each of the game's screens needs (touch_needs.c): plain tables for the tests,
 * knowing nothing of how the controls are drawn or what fingers do, so that no screen leaves a player with nothing
 * but a touch screen without a way on. */
#ifndef KHDAYS_TOUCH_NEEDS_H
#define KHDAYS_TOUCH_NEEDS_H

#ifdef __cplusplus
extern "C" {
#endif

/* What the controls are for now. */
enum {
    KHDAYS_TOUCH_MODE_NONE,  /* nothing shown or held */
    KHDAYS_TOUCH_MODE_FIELD, /* play: stick, camera, buttons, the command deck */
    KHDAYS_TOUCH_MODE_MENU,  /* D-pad and buttons */
    KHDAYS_TOUCH_MODE_TALK   /* an event, a movie: a tap is A, a small Start */
};

typedef struct KhdaysTouchSituation {
    int port_menu; /* the settings or a movie's pause are over the game: they take touches themselves */
    int hd_movie;  /* an HD movie plays */
    int scene;     /* KHDAYS_SIT_* (ui/situation.h) */
    int hud;       /* the field's HUD shows (without it, an event) */
} KhdaysTouchSituation;

/* The controls for a situation: every one of the game's screens gets some. */
int khdays_touch_needs_mode(const KhdaysTouchSituation *situation);

/* Whether a window can show them: not with the lower screen on a display of its own, which takes touches, unless
 * the one picture is what that window shows. */
int khdays_touch_needs_window(int one_picture, int second_display);

#ifdef __cplusplus
}
#endif

#endif
