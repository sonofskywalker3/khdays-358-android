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
    KHDAYS_TOUCH_MODE_TALK,  /* a movie: a tap is A, a small Start */
    KHDAYS_TOUCH_MODE_EVENT  /* an event: the menu's controls, which answer it */
};

typedef struct KhdaysTouchSituation {
    int port_menu; /* a movie's pause is over the game: it takes touches itself */
    int hd_movie;  /* an HD movie plays */
    int scene;     /* KHDAYS_SIT_* (ui/situation.h) */
    int event;     /* the field shows an event or a person's words: the player does not move Roxas */
    int settings;  /* Game Settings is over the game */
    int movie_pause; /* a pause (Continue, Skip) is open over a movie: it is answered with the D-pad and A */
} KhdaysTouchSituation;

/* The controls for a situation: every one of the game's screens gets some. */
int khdays_touch_needs_mode(const KhdaysTouchSituation *situation);

/* The mode to show from what mode_now says frame by frame: an event goes back to play only once it has said so for
 * KHDAYS_TOUCH_SETTLE_FRAMES, so that the gaps between an event's lines do not blink the controls; any other change,
 * an event's start with what it lets go of among them, at once. `shown` and `waited` are the caller's, zero at first. */
#define KHDAYS_TOUCH_SETTLE_FRAMES 15
int khdays_touch_needs_settled(int mode, int *shown, int *waited);

/* Whether a window can show them: not with the lower screen on a display of its own, which takes touches, unless
 * the one picture is what that window shows. */
int khdays_touch_needs_window(int one_picture, int second_display);

#ifdef __cplusplus
}
#endif

#endif
