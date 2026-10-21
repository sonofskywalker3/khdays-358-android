/* The window, run by SDL3 on a thread of its own: the game's thread hands over frames and reads the
 * input through SDL's locks and atomics; gpu_present.c draws, devices.c reads the devices through the
 * player's profile, the settings menu opens on Esc or the pad's settings button, sound_output.c plays.
 * Closing the window ends the process (docs/PLATFORM.md, "The PC's side"). */
#include "host.h"
#if defined(__ANDROID__)
#include "../android/bottom_screen.h"
#endif
#include "devices.h"
#include "press_edges.h"
#include "../render/gpu_present.h"
#include "../movies/movie_hd.h"
#include "profile.h"
#include "../../runtime/menus/newgame.h"
#include "../../runtime/menus/savelist.h"
#include "../settings/settings_model.h"
#include "../settings/settings_sample.h"
#include "../settings/settings_ui.h"
#include "../screens/cine_screen.h"
#include "../../runtime/menus/cinelist.h"
#include "../screens/room_screen.h"
#include "../screens/select_screen.h"
#include "../screens/camp_screen.h"
#include "../screens/save_screen.h"
#include "sound_output.h"
#include "updates.h"
#include "../screens/ui_font.h"
#include "../screens/ui_text.h"
#include "../../runtime/core/clock.h"
#include "../../runtime/display/display.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/core/language.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/net/net_session.h"
#include "../../runtime/scripts/queue.h"
#include "../../runtime/scripts/view.h"
#include "../../runtime/core/runtime.h"
#include "../../runtime/core/session_log.h"
#include "../../ui/situation.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 192

/* The plain two-screen frame of khdays_host_present (when there are no frame
 * packages). */
static SDL_Mutex *frame_lock;
static uint32_t frame[2 * W * H];
static SDL_AtomicInt frame_new;     /* set by khdays_host_present, cleared by the window */
static SDL_AtomicInt frame_pending; /* a frame event is already on its way */
static Uint32 frame_event;          /* our SDL event: "a new frame is ready" */
static Uint64 last_redraw;          /* SDL_GetTicks of the last redraw */
#define MOVIE_REDRAW_MS 8           /* an HD video drawn at least this often (ms) */

/* The frame packages. There are three, so neither thread ever waits for the
 * other: the game thread fills one, the newest finished one waits in
 * `mailbox`, and the window shows the third. `mailbox` holds the waiting
 * package's index, with FRESH set until the window takes it. Only the window
 * clears FRESH, so a swap never loses a package either thread holds. */
enum { FRESH = 4 };
static KhdaysFramePackage *packages[3];
static int game_slot;         /* the game thread's */
static int host_slot = 1;     /* the host thread's */
static SDL_AtomicInt mailbox; /* = 2 at the start */
/* What the window asks the game thread to put in each package. */
static SDL_AtomicInt wants;   /* KHDAYS_WANT_* */
static SDL_AtomicInt columns; /* how wide the 3D should be, in DS pixels (256 is the DS's width) */
static SDL_AtomicInt scale;   /* the internal resolution the picture is composed at */

static SDL_Mutex *input_lock;
static KhdaysInputSnapshot input;
/* Presses and releases since the game's last read of the pad (press_edges.h), under input_lock. */
static KhdaysPressEdges edges, edges_field;

static SDL_Semaphore *ready; /* signalled when the window is up (or failed) */
static SDL_AtomicInt started;
static SDL_Semaphore *closed; /* signalled when the window and the sound are gone */
static Uint32 close_event;    /* our SDL event: khdays_host_close's request */
static Uint32 wake_event;     /* our SDL event: khdays_host_wake's, the loop looks again */

/* Stores the latest input for the game thread to read. */
static void publish(const KhdaysInputSnapshot *snapshot)
{
    SDL_LockMutex(input_lock);
    const uint32_t generation = input.generation + 1;
    khdays_press_edges_note(&edges, input.ds_buttons, snapshot->ds_buttons);
    khdays_press_edges_note(&edges_field, input.ds_buttons_field, snapshot->ds_buttons_field);
    input = *snapshot;
    input.generation = generation;
    SDL_UnlockMutex(input_lock);
}

/* Reports a problem with the settings file or a controller: in the log, and
 * on the settings menu's notice line. */
static void notice(const char *text)
{
    fprintf(stderr, "host: %s\n", text);
    khdays_settings_add_notice(text);
}

/* Tells the game thread what to put in the frame packages for the picture
 * the window shows now (it reads this at its next VBlank). */
static void tell_game(void)
{
    SDL_SetAtomicInt(&wants, khdays_present_wants());
    SDL_SetAtomicInt(&columns, khdays_present_columns());
    SDL_SetAtomicInt(&scale, khdays_present_scale());
}

/* The window as [video] display says (profile.h KhdaysDisplay): a window, one without borders
 * over its screen (SDL's full screen at the desktop's mode, NULL), or the screen the game's alone
 * at the desktop's mode. */
static SDL_Window *host_window;
static int last_full = KHDAYS_DISPLAY_BORDERLESS; /* the full screen Alt+Enter goes back to */

static void display_now(int display)
{
    static int shown = -1;
#if defined(__ANDROID__)
    display = KHDAYS_DISPLAY_BORDERLESS; /* an app has the whole display, without the system's bars */
#endif
    if (host_window == NULL || display == shown) {
        return;
    }
    shown = display;
    if (display != KHDAYS_DISPLAY_WINDOW) {
        last_full = display;
    }
    if (display == KHDAYS_DISPLAY_WINDOW) {
        SDL_SetWindowFullscreen(host_window, false);
        SDL_SetWindowBordered(host_window, true);
    } else {
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(host_window));
        SDL_SetWindowFullscreenMode(host_window, display == KHDAYS_DISPLAY_FULLSCREEN ? desktop : NULL);
        SDL_SetWindowFullscreen(host_window, true);
    }
    SDL_SyncWindow(host_window);
    khdays_log("window: %s", display == KHDAYS_DISPLAY_WINDOW       ? "a window"
                             : display == KHDAYS_DISPLAY_BORDERLESS ? "a borderless window over the screen"
                                                                    : "full screen");
}

/* Applies a change in the menu's Video section at once: the picture behind
 * the menu is drawn again from the last frame with the new settings. (The game
 * stays paused; if the last frame did not carry the 3D, the change shows from
 * the next frame the game draws.) */
static void video_now(void)
{
    static KhdaysVideo applied;
    const KhdaysVideo *v = &khdays_settings_profile()->video;
    if (memcmp(v, &applied, sizeof(applied)) == 0) {
        return;
    }
    applied = *v;
    display_now(v->display);
    khdays_present_set_video(v);
    tell_game();
    if (packages[0] != NULL) {
        khdays_present_package(packages[host_slot]);
    }
}

/* The text of the ping display ([network] show_ping), to SDL_free; NULL when
 * it is off or outside a session. It lists each member's round trip, joined
 * by " / " ("-" until measured, "<1" under half a millisecond). While the
 * menu is open its setting counts, so a change shows at once. */
static char *ping_line(void)
{
    const KhdaysProfile *p = khdays_settings_is_open() ? khdays_settings_profile() : khdays_devices_profile();
    unsigned int *aids;
    int *ms, count, shown = 0;
    size_t size;
    char *numbers, *line;
    if (!p->network.show_ping || !khdays_net_in_session()) {
        return NULL;
    }
    count = khdays_net_round_trips(NULL, NULL, 0);
    aids = (unsigned int *)SDL_malloc(sizeof(*aids) * (size_t)(count > 0 ? count : 1));
    ms = (int *)SDL_malloc(sizeof(*ms) * (size_t)(count > 0 ? count : 1));
    size = 16 * (size_t)(count > 0 ? count : 1) + 2; /* at most "-2147483648 / " per member */
    numbers = (char *)SDL_malloc(size);
    if (aids == NULL || ms == NULL || numbers == NULL) {
        SDL_free(aids);
        SDL_free(ms);
        SDL_free(numbers);
        return NULL;
    }
    numbers[0] = '\0';
    /* members who joined since the count wait for the next picture */
    count = SDL_min(count, khdays_net_round_trips(aids, ms, count));
    for (int i = 0; i < count; ++i) {
        char one[16];
        if (ms[i] < 0) {
            SDL_strlcpy(one, "-", sizeof(one));
        } else if (ms[i] == 0) {
            SDL_strlcpy(one, "<1", sizeof(one)); /* under half a millisecond: copies on the same PC */
        } else {
            SDL_snprintf(one, sizeof(one), "%d", ms[i]);
        }
        if (shown++ > 0) {
            SDL_strlcat(numbers, " / ", size);
        }
        SDL_strlcat(numbers, one, size);
    }
    SDL_asprintf(&line, khdays_text(TXT_PING_LINE), shown > 0 ? numbers : "-");
    SDL_free(aids);
    SDL_free(ms);
    SDL_free(numbers);
    return line;
}

/* The mods' notices shown now (runtime/scripts/view.h), each for five seconds; each also kept for the
 * settings menu's notice line. */
typedef struct ModNotice {
    char *line;
    Uint64 until;
} ModNotice;
static ModNotice *mod_notices;
static int mod_notice_count, mod_notice_capacity;

static void take_mod_notices(void)
{
    const Uint64 now = SDL_GetTicks();
    int kind, kept = 0;
    char *mod, *text;
    for (int i = 0; i < mod_notice_count; ++i) {
        if (mod_notices[i].until > now) {
            mod_notices[kept++] = mod_notices[i];
        } else {
            SDL_free(mod_notices[i].line);
        }
    }
    mod_notice_count = kept;
    while (khdays_scripts_take_notice(&kind, &mod, &text)) {
        static const int formats[] = {[KHDAYS_NOTICE_STOPPED] = TXT_MOD_STOPPED,
                                      [KHDAYS_NOTICE_PAUSED] = TXT_MOD_PAUSED,
                                      [KHDAYS_NOTICE_REFUSED] = TXT_MOD_REFUSED};
        char said[400], *line = NULL;
        if (kind == KHDAYS_NOTICE_SAID) {
            SDL_snprintf(said, sizeof(said), "%s", text);
        } else {
            SDL_snprintf(said, sizeof(said), khdays_text(formats[kind]), text);
        }
        if (SDL_asprintf(&line, "%s: %s", mod, said) >= 0) {
            khdays_settings_add_notice(line);
            if (mod_notice_count == mod_notice_capacity) {
                const int grown = mod_notice_capacity == 0 ? 4 : mod_notice_capacity * 2;
                ModNotice *more = SDL_realloc(mod_notices, (size_t)grown * sizeof(*more));
                if (more != NULL) {
                    mod_notices = more;
                    mod_notice_capacity = grown;
                }
            }
            if (mod_notice_count < mod_notice_capacity) {
                mod_notices[mod_notice_count].line = line;
                mod_notices[mod_notice_count].until = now + 5000;
                ++mod_notice_count;
            } else {
                SDL_free(line);
            }
        }
        free(mod);
        free(text);
    }
}

#if defined(__ANDROID__)
/* The settings menu on the second display (asked for on 2026-10-21): while it is open, and on the
 * title while that shows as an HD picture on the upper display alone -- there drawn as it opens, the
 * pad's settings button (L3) moving the pad between the title and the menu. Not touched: its text is
 * zoomed to be read on the small display, which leaves its rows too narrow for fingers. */
static int bottom_menu;

#define BOTTOM_MENU_IDLE_MS 500 /* the closed menu's picture, redrawn this often (it does not change) */
#define BOTTOM_MENU_ZOOM 1.4f   /* its text's size on the second display, times the window's rule */

/* Draws the menu on the second display when it belongs there; returns whether it did. */
static int bottom_settings_draw(SDL_Renderer *renderer)
{
    static SDL_Texture *target;
    static int target_w, target_h;
    static Uint64 idle_drawn;
    SDL_Texture *previous;
    SDL_Surface *read, *rgba;
    int w = 0, h = 0;
    const int open = khdays_settings_is_open();
    const int on = khdays_bottom_size(&w, &h) && (open || khdays_present_title_alone());
    if (on != bottom_menu) {
        bottom_menu = on;
        idle_drawn = 0;
        khdays_present_hold_bottom(on);
        khdays_bottom_set_game_touch(!on);
    }
    if (!on) {
        return 0;
    }
    if (!open && idle_drawn != 0 && SDL_GetTicks() - idle_drawn < BOTTOM_MENU_IDLE_MS) {
        return 1; /* the closed menu's picture is already there */
    }
    if (target == NULL || target_w != w || target_h != h) {
        if (target != NULL) {
            SDL_DestroyTexture(target);
        }
        target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
        target_w = w;
        target_h = h;
        if (target == NULL) {
            return 1;
        }
    }
    previous = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    khdays_settings_set_zoom(BOTTOM_MENU_ZOOM);
    if (open) {
        khdays_settings_draw(renderer, NULL);
    } else {
        khdays_settings_draw_closed(renderer, khdays_devices_profile());
        idle_drawn = SDL_GetTicks();
    }
    khdays_settings_set_zoom(1.0f);
    read = SDL_RenderReadPixels(renderer, NULL);
    SDL_SetRenderTarget(renderer, previous);
    if (read == NULL) {
        return 1;
    }
    rgba = read->format == SDL_PIXELFORMAT_ABGR8888 ? read : SDL_ConvertSurface(read, SDL_PIXELFORMAT_ABGR8888);
    if (rgba != NULL) {
        khdays_bottom_present_whole((const Uint8 *)rgba->pixels, rgba->w, rgba->h, rgba->pitch);
        if (rgba != read) {
            SDL_DestroySurface(rgba);
        }
    }
    SDL_DestroySurface(read);
    return 1;
}
#endif

/* Draws the last frame, with the FPS and ping displays and the settings menu
 * over it. Draws nothing while the graphics card is lost (the loop recreates
 * it). */
static void redraw(SDL_Window *window, SDL_Renderer *renderer)
{
    int pictures, frames, fps, corner_rows;
    char *ping;
    last_redraw = SDL_GetTicks();
    if (khdays_present_lost()) {
        return;
    }
    khdays_present_draw();
    khdays_save_screen_draw(renderer);   /* the port's save list over the game's (runtime/menus/savelist.h) */
    khdays_cine_screen_draw(renderer);   /* MODO CINE's list as the HD theater's (runtime/menus/cinelist.h) */
    khdays_room_screen_draw(renderer);   /* the port's list of rooms over the game's (runtime/net/roomlist.h) */
    khdays_select_screen_draw(renderer); /* Mission Mode's character select on one screen (charselect.h) */
    khdays_camp_screen_draw(renderer);   /* Mission Mode's camp menu on one screen (camp.h) */
    fps = khdays_present_fps(&pictures, &frames);
    if (fps) {
        khdays_settings_fps_draw(renderer, pictures, frames);
    }
    ping = ping_line();
    corner_rows = (fps != 0) + (ping != NULL);
    if (ping != NULL) {
        khdays_settings_ping_draw(renderer, ping, fps);
        SDL_free(ping);
    }
    take_mod_notices();
    for (int i = 0; i < mod_notice_count; ++i) {
        khdays_settings_corner_draw(renderer, mod_notices[i].line, corner_rows + i);
    }
#if defined(__ANDROID__)
    if (!bottom_settings_draw(renderer))
#endif
        khdays_settings_draw(renderer, window);
    SDL_RenderPresent(renderer);
}

/* Whether the open settings menu paused the game. In a multiplayer session
 * it does not, since the other players' games go on. */
static int settings_stopped;

/* Keeps the subtitles the new game's question chose (runtime/menus/newgame.h): in the profile, the
 * picture's settings and the settings file. */
static void keep_subtitles(void)
{
    KhdaysProfile p;
    char path[512];
    int on;
    if (!khdays_newgame_take_answer(&on) || khdays_devices_profile()->video.subtitles == on) {
        return;
    }
    p = *khdays_devices_profile();
    p.video.subtitles = on;
    khdays_devices_set_profile(&p);
    khdays_present_set_video(&p.video);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

/* Alt+Enter, as on most PC games: from a window to the full screen chosen last (the borderless
 * window if none), from either full screen to a window; kept in the profile and the settings file
 * as the menu keeps it. */
static void toggle_display(void)
{
    KhdaysProfile p = *khdays_devices_profile();
    char path[512];
    p.video.display = p.video.display == KHDAYS_DISPLAY_WINDOW ? last_full : KHDAYS_DISPLAY_WINDOW;
    khdays_devices_set_profile(&p);
    display_now(p.video.display);
    khdays_present_set_video(&p.video);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

static int is_alt_enter(const SDL_Event *e)
{
    return e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && (e->key.mod & SDL_KMOD_ALT) &&
           (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER);
}

/* Writes the settings file from the menu's profile as it is now. */
static void save_settings(void)
{
    char path[512];
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(khdays_settings_profile(), path, notice);
}

/* Applies each change in the open menu as it is made: each part of the profile goes where it would go
 * when the menu closes. (The game gets no input while the menu is open, so
 * new controls take effect from the first press after it closes; the menu
 * itself keeps the buttons it opened with.) */
static KhdaysSound heard; /* the sound settings applied last */

static void apply_changes(void)
{
    const unsigned changes = khdays_settings_take_changes();
    const KhdaysProfile *p = khdays_settings_profile();
    if (changes & KHDAYS_CHANGE_CONTROLS) {
        khdays_devices_set_profile(p);
    }
    if (changes & KHDAYS_CHANGE_VIDEO) {
        video_now(); /* the picture behind the menu follows */
    }
    if (changes & KHDAYS_CHANGE_SOUND) {
        khdays_sound_output_set(&p->sound);
        if (settings_stopped) {
            /* the game is paused, so play a sample of the sound whose volume changed */
            khdays_settings_sample(&heard, &p->sound);
        }
        heard = p->sound;
    }
    if (changes & KHDAYS_CHANGE_NETWORK) {
        save_settings();          /* the nickname is read back from the file */
        khdays_boot_owner_name(); /* for the next multiplayer session the game starts */
    }
    if (changes & KHDAYS_CHANGE_LANGUAGE) {
        /* the menu's text changes at once, the game's when the current scene
         * ends (runtime/core/language.c); KHDAYS_LANGUAGE still takes precedence */
        khdays_language_request(khdays_language_choose(p->language));
    }
    if (changes & KHDAYS_CHANGE_LOG) {
        khdays_log_want(p->session_log);
    }
}

/* While the settings menu shows, the game pauses: its time stops, it gets no input and its sound
 * is silent. In a multiplayer session it only stops getting input. */
static void stop_game_for_menu(void)
{
    khdays_devices_set_game_input(0);
    settings_stopped = !khdays_net_in_session();
    if (settings_stopped) {
        khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
        khdays_sound_output_pause(1);
        khdays_hd_pause(1);       /* an HD video playing for a movie pauses too */
        khdays_hd_voice_pause(1); /* and a monologue's voice */
    }
}

/* Opens the menu for the notice of a newer release alone (host/app/updates.h). */
static void open_update_notice(const char *tag)
{
    khdays_log("updates: telling the player of %s", tag);
    stop_game_for_menu();
    khdays_settings_offer_update(khdays_devices_profile(), khdays_updates_version(), tag);
}

/* Opens the settings menu (the game pauses, stop_game_for_menu). */
static void open_settings(void)
{
    char text[160];
    stop_game_for_menu();
    while (khdays_devices_take_notice(text, sizeof(text))) {
        khdays_settings_add_notice(text);
    }
    khdays_settings_set_situation(!settings_stopped, khdays_settings_sample_has_movies());
    khdays_settings_set_guest(khdays_net_own_aid() > 0);
    khdays_settings_open(khdays_devices_profile());
    heard = khdays_settings_profile()->sound;
    khdays_scripts_ask(KHDAYS_ASK_SETTINGS_OPENED, NULL, NULL, 0); /* the mods hear it */
}

/* Closes the settings menu: saves and applies what changed, and lets the game
 * run again. */
static void close_settings(void)
{
    apply_changes();
    khdays_scripts_ask(KHDAYS_ASK_SETTINGS_CLOSED, NULL, NULL, 0);
    khdays_settings_sample_stop();
    if (khdays_settings_changed()) {
        khdays_devices_set_profile(khdays_settings_profile()); /* as closing may have fixed it */
        save_settings();
        khdays_present_set_video(&khdays_settings_profile()->video);
        khdays_sound_output_set(&khdays_settings_profile()->sound);
        khdays_log_want(khdays_settings_profile()->session_log);
        khdays_savelist_set_settings(khdays_settings_profile()->save.all_files,
                                     khdays_settings_profile()->save.autosave);
        khdays_boot_owner_name(); /* the nickname, for the next multiplayer session */
        tell_game();
    }
    /* Whatever is still held (the button that closed the menu) only counts for
     * the game once it has been released, and the settings button that closed
     * the menu does not reopen it. */
    khdays_devices_set_game_input(1);
    (void)khdays_devices_take_settings_request();
    if (settings_stopped) {
        khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
        khdays_sound_output_pause(0);
        khdays_hd_pause(0);
        khdays_hd_voice_pause(0);
        settings_stopped = 0;
    }
}

/* The mouse pointer is hidden over the game while its window has the focus (the touch screen is not
 * used) and shown over the settings menu or when another window has the focus. With the mouse camera
 * on, the window also holds the mouse while the game has the input (devices.c). */
static void mouse_follow(SDL_Window *window)
{
    const int menu = khdays_settings_is_open();
    const int focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const int hold = !menu && khdays_devices_profile()->enh.mouse_camera && focused;
    const int shown = menu || !focused;
    if (hold != (int)SDL_GetWindowRelativeMouseMode(window)) {
        SDL_SetWindowRelativeMouseMode(window, hold != 0);
    }
    if (shown != (int)SDL_CursorVisible()) {
        if (shown) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
}

/* Whether this event opens the settings menu (which is closed now). */
static int opens_settings(const SDL_Event *event, SDL_Window *window)
{
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 1;
    }
    (void)window;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT &&
        khdays_present_settings_button() && khdays_present_settings_button_at(event->button.x, event->button.y)) {
        return 1; /* the picture's own settings key (the title's) */
    }
    return khdays_devices_take_settings_request();
}

/* Creates the window: the DS's two screens at 2x, or with one picture
 * the largest whole multiple of 342 x 192 (16:9) that fits in
 * three quarters of the desktop. */
static SDL_Window *open_window(const KhdaysProfile *profile)
{
    int w = 2 * W, h = 4 * H;
    SDL_Rect desk;
    if (profile->video.one_screen && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &desk)) {
        const int kx = desk.w * 3 / 4 / 342, ky = desk.h * 3 / 4 / H;
        const int k = kx < ky ? kx : ky;
        w = 342 * (k > 1 ? k : 1);
        h = H * (k > 1 ? k : 1);
    }
    return SDL_CreateWindow("Kingdom Hearts 358/2 Days (native)", w, h, SDL_WINDOW_RESIZABLE);
}

/* The window's thread: opens everything, then handles SDL's events until the
 * window closes. */
static int host_thread(void *unused)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    KhdaysProfile profile;
    (void)unused;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        SDL_SignalSemaphore(ready);
        return 1;
    }
    khdays_sound_output_open();
    {
        /* The player's settings (or the defaults, the first time). Read first,
         * so that the window speaks the player's language. */
        char path[512];
        khdays_settings_path(path, sizeof(path));
        khdays_profile_load(&profile, path, notice);
        khdays_ui_set_language(khdays_language_choose(profile.language));
        khdays_sound_output_set(&profile.sound);
        khdays_log_want(profile.session_log);
        khdays_savelist_set_settings(profile.save.all_files, profile.save.autosave);
    }
    window = open_window(&profile);
    host_window = window;
    display_now(profile.video.display);
    khdays_present_set_video(&profile.video);
    renderer = window ? khdays_present_open(window, notice) : NULL;
    frame_event = SDL_RegisterEvents(3);
    close_event = frame_event + 1;
    wake_event = frame_event + 2;
    if (renderer == NULL || frame_event == 0) {
        fprintf(stderr, "host: cannot open the window: %s\n", SDL_GetError());
        SDL_SignalSemaphore(ready);
        return 1;
    }
    khdays_devices_init(&profile, publish);
    khdays_settings_set_graphics_card(khdays_present_gpu());
    tell_game();
    khdays_updates_start(profile.network.check_updates); /* in the background; told of on the title */
    SDL_SetAtomicInt(&started, 1);
    SDL_SignalSemaphore(ready);

    for (;;) {
        SDL_Event event;
        /* While the settings menu shows, it keeps animating even if the paused
         * game sends no frames. */
        int animating;
        /* A newer release is told of on the front-end (the boot logos, the title: scene 1), never
         * in the middle of the game, and once a run. */
        if (!khdays_settings_is_open() && khdays_runtime_scene() == 1) {
            char tag[64];
            if (khdays_updates_offer(tag, sizeof(tag))) {
                open_update_notice(tag);
                redraw(window, renderer);
            }
        }
        animating = khdays_settings_is_open();
        khdays_devices_frame(); /* a wheel's notch let go */
        mouse_follow(window);
        /* The 3D at the monitor's refresh rate: draw a picture
         * whenever VSync lets one through, between events. */
        const int smooth = khdays_present_smooth();
        /* An HD video playing for a movie is drawn at the display's
         * refresh rate whether or not the game sends frames (the game waits at
         * the movie's end). Here we watch for its end and pass on START, which
         * the waiting game thread cannot read. */
        const int movie = khdays_movie_hd_showing();
        /* So is an animated backdrop (the title's, ui/composition.h): its pieces move while the
         * game's still menu sends no frames. */
        const int backdrop = khdays_present_backdrop_moving();
        /* So is a situation still settling on a still picture (a pause opened where the 3D was
         * on both screens): it is decided again as the game's VBlanks pass. */
        const int settling = khdays_present_settling();
        keep_subtitles();
        if (!khdays_settings_is_open()) {
            khdays_cine_screen_buttons(khdays_cinelist_seen()); /* MODO CINE's chapters, the port's list */
        }
        if (movie && !khdays_settings_is_open()) {
            khdays_movie_hd_poll();
            khdays_movie_hd_host_buttons(khdays_host_buttons());
            /* drawn on time even while events keep coming (a gamepad's stream of them): the game,
             * held for a scene's video, sends no frames that would draw it */
            if (SDL_GetTicks() - last_redraw >= MOVIE_REDRAW_MS) {
                redraw(window, renderer);
            }
        }
        if (khdays_present_lost()) {
            /* the port noticed it itself (SDL's renderer does not on D3D12):
             * handle it like SDL's event */
            SDL_zero(event);
            event.type = SDL_EVENT_RENDER_DEVICE_LOST;
        } else if (!(smooth || movie || backdrop ? SDL_WaitEventTimeout(&event, 1)
                     : settling                  ? SDL_WaitEventTimeout(&event, 4) /* (a VBlank is ~17 ms) */
                     : animating                 ? SDL_WaitEventTimeout(&event, 50)
                                                 : SDL_WaitEvent(&event))) {
            if (animating) {
                khdays_settings_tick();
                if (khdays_settings_is_open()) {
                    apply_changes(); /* a held direction keeps repeating its change */
                }
            }
            if ((smooth && khdays_present_refresh()) || (backdrop && khdays_present_refresh_backdrop()) ||
                (settling && khdays_present_refresh_settling()) || animating || movie) {
                redraw(window, renderer);
            }
            continue;
        }
        mouse_follow(window);
        if (event.type == SDL_EVENT_QUIT) {
            if (khdays_settings_is_open() && khdays_settings_profile_changed()) {
                save_settings(); /* closed with the menu open: keep what was changed */
            }
            _Exit(0);
        }
        if (event.type == close_event) {
            if (khdays_settings_is_open()) {
                khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0); /* a reset while the menu was open */
                if (khdays_settings_profile_changed()) {
                    save_settings(); /* keep what was changed, as closing the menu would */
                }
            }
            khdays_sound_output_close();
            khdays_hd_voice_stop(); /* a monologue's voice still heard */
            khdays_ui_font_forget();
            khdays_settings_renderer_gone();
            khdays_save_screen_renderer_gone();
            khdays_cine_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_present_close();
            SDL_DestroyWindow(window);
            SDL_Quit();
            SDL_SignalSemaphore(closed);
            return 0;
        }
        if (event.type == frame_event) {
            /* clear this first, so a frame published from now on sends its own event */
            SDL_SetAtomicInt(&frame_pending, 0);
            if (SDL_GetAtomicInt(&mailbox) & FRESH) {
                host_slot = SDL_SetAtomicInt(&mailbox, host_slot) & 3;
                khdays_present_package(packages[host_slot]);
            } else if (SDL_SetAtomicInt(&frame_new, 0)) {
                SDL_LockMutex(frame_lock);
                khdays_present_screens(frame);
                SDL_UnlockMutex(frame_lock);
            } else {
                continue; /* already taken with an earlier event */
            }
            tell_game(); /* (a scale the graphics card refused changes what the game should fill) */
            redraw(window, renderer);
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            khdays_present_resized(); /* settings set to "window" follow the new size */
            tell_game();
        }
        if (event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_DEVICE_LOST) {
            int renewed;
            khdays_ui_font_forget(); /* free its textures before their renderer goes */
            khdays_settings_renderer_gone();
            khdays_save_screen_renderer_gone();
            khdays_cine_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_camp_screen_renderer_gone();
            renderer = khdays_present_event(&event, &renewed);
            khdays_settings_set_graphics_card(khdays_present_gpu());
            tell_game();
            redraw(window, renderer);
            continue;
        }

        /* Alt+Enter: the window or the full screen; neither the game's Enter nor the menu's (the
         * menu has its own Display mode, and its profile would undo a change made under it) */
        if (is_alt_enter(&event)) {
            if (!khdays_settings_is_open()) {
                toggle_display();
                tell_game();
            }
            continue;
        }

        /* Input. The devices always see it (they track what is held). While the
         * settings menu is open, the menu gets the input and the game none. */
        if (khdays_settings_is_open()) {
            /* One picture for the events waiting, not one each: a gamepad's sticks and
             * triggers send their moves many times a frame (and SDL each twice, as a joystick's
             * and a gamepad's), and a picture each -- every one waiting for the display's
             * refresh -- left the menu behind the player's presses. At least a picture a frame
             * while they keep coming. */
            static Uint64 drawn_at;
            khdays_devices_handle(&event, renderer);
            if (khdays_settings_handle(&event, window)) {
                const int update = khdays_settings_take_update_answer();
                close_settings();
                if (update >= 0) {
                    khdays_updates_answer(update); /* the notice's: the page opened, or not told again */
                }
                redraw(window, renderer);
                continue;
            }
            apply_changes();
            if (!SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST) || SDL_GetTicks() - drawn_at >= 16) {
                redraw(window, renderer);
                drawn_at = SDL_GetTicks();
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12) {
            khdays_log_mark();     /* F12 marks the session log: "the player saw something here" */
            khdays_display_mark(); /* and saves what the DS shows then (runtime/display/display.c) */
        }
        if (khdays_room_screen_event(&event)) {
            /* a field of the list of rooms takes what is typed (Escape there is its own); the
             * devices still track what is held */
            khdays_devices_handle(&event, renderer);
            continue;
        }
        if (opens_settings(&event, window)) {
            open_settings();
            redraw(window, renderer);
            continue;
        }
        khdays_devices_handle(&event, renderer); /* keys, mouse, gamepads, focus */
        for (int n = khdays_devices_take_hud_mode_request(); n > 0; --n) {
            khdays_situation_hud_mode_next(); /* the one-screen HUD mode */
            khdays_log("one screen: the HUD mode now %d (0 complete, 1 exploration, 2 none)",
                       khdays_situation_hud_mode());
        }
        if (khdays_devices_take_settings_request()) {
            open_settings();
            redraw(window, renderer);
        }
    }
}

void khdays_host_tell_no_data(const char *where, const char *broken)
{
    const int language = khdays_language_of_system();
    const char *format = khdays_text_in(broken != NULL ? TXT_GAME_DATA_BROKEN : TXT_NEEDS_GAME_DATA, language);
    char text[1600];
    if (format == NULL) {
        format = khdays_text_in(broken != NULL ? TXT_GAME_DATA_BROKEN : TXT_NEEDS_GAME_DATA, KHDAYS_LANGUAGE_ENGLISH);
    }
    if (broken != NULL) {
        SDL_snprintf(text, sizeof(text), format, where, broken);
    } else {
        SDL_snprintf(text, sizeof(text), format, where);
    }
    fprintf(stderr, "host: %s\n", text);
    fflush(stderr);
    if (getenv("KHDAYS_HEADLESS") == NULL) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kingdom Hearts 358/2 Days (native)", text, NULL);
    }
}

int khdays_host_start(void)
{
    if (frame_lock == NULL) {
        frame_lock = SDL_CreateMutex();
        input_lock = SDL_CreateMutex();
    }
    if (frame_lock == NULL || input_lock == NULL) {
        return 0;
    }
    if (getenv("KHDAYS_HEADLESS") != NULL) {
        /* No window and no devices: a KHDAYS_INPUT script plays instead. It
         * uses the enhancements (and their speeds) from the settings file
         * named by KHDAYS_SETTINGS, if any; the regression walks name none,
         * so they run with the defaults. */
        const char *path = SDL_getenv("KHDAYS_SETTINGS");
        if (path != NULL) {
            KhdaysProfile profile;
            KhdaysDeviceState nothing;
            khdays_profile_load(&profile, path, notice);
            khdays_bindings_reset(&nothing);
            SDL_LockMutex(input_lock);
            khdays_bindings_resolve(&profile, &nothing, &input);
            SDL_UnlockMutex(input_lock);
        }
        return 1;
    }
    if (packages[0] == NULL) {
        for (int n = 0; n < 3; ++n) {
            packages[n] = (KhdaysFramePackage *)SDL_calloc(1, sizeof(KhdaysFramePackage));
            if (packages[n] == NULL) {
                return 0;
            }
        }
        SDL_SetAtomicInt(&mailbox, 2);
    }
    ready = SDL_CreateSemaphore(0);
    closed = SDL_CreateSemaphore(0);
    if (ready == NULL || closed == NULL || SDL_CreateThread(host_thread, "host", NULL) == NULL) {
        return 0;
    }
    SDL_WaitSemaphore(ready);
    return SDL_GetAtomicInt(&started) != 0;
}

void khdays_host_close(void)
{
    SDL_Event event;
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_SetAtomicInt(&started, 0); /* accept no more frames */
    SDL_zero(event);
    event.type = close_event;
    if (SDL_PushEvent(&event)) {
        SDL_WaitSemaphoreTimeout(closed, 5000);
    }
}

void khdays_host_wake(void)
{
    SDL_Event event;
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_zero(event);
    event.type = wake_event;
    SDL_PushEvent(&event);
}

void khdays_host_present(const uint32_t *upper, const uint32_t *lower)
{
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_LockMutex(frame_lock);
    memcpy(frame, upper, W * H * 4);
    memcpy(frame + W * H, lower, W * H * 4);
    SDL_UnlockMutex(frame_lock);
    SDL_SetAtomicInt(&frame_new, 1);
    if (SDL_SetAtomicInt(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

int khdays_host_frame_wants(void)
{
    return SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&wants) : 0;
}

float khdays_host_frame_widen(void)
{
    const int n = SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&columns) : 0;
    return n > 256 ? (float)n / 256.0f : 1.0f;
}

int khdays_host_frame_scale(void)
{
    const int n = SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&scale) : 1;
    return n < 1 ? 1 : n;
}

KhdaysFramePackage *khdays_host_frame_begin(void)
{
    return SDL_GetAtomicInt(&started) && packages[0] != NULL ? packages[game_slot] : NULL;
}

void khdays_host_frame_publish(KhdaysFramePackage *package)
{
    if (package != packages[game_slot]) {
        return;
    }
    game_slot = SDL_SetAtomicInt(&mailbox, game_slot | FRESH) & 3;
    if (SDL_GetAtomicInt(&started) && SDL_SetAtomicInt(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

/* Copies the latest snapshot. Headless (no window), everything is 0. */
static void khdays_host_input(KhdaysInputSnapshot *out)
{
    if (input_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(input_lock);
    *out = input;
    SDL_UnlockMutex(input_lock);
}

void khdays_host_input_take(KhdaysInputSnapshot *out)
{
    if (input_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(input_lock);
    *out = input;
    out->ds_buttons = khdays_press_edges_take(&edges, input.ds_buttons);
    out->ds_buttons_field = khdays_press_edges_take(&edges_field, input.ds_buttons_field);
    SDL_UnlockMutex(input_lock);
}

uint16_t khdays_host_buttons(void)
{
    KhdaysInputSnapshot now;
    khdays_host_input(&now);
    return now.ds_buttons;
}

int khdays_host_touch(int *x, int *y)
{
    KhdaysInputSnapshot now;
#if defined(__ANDROID__)
    if (khdays_bottom_touch(x, y)) { /* the lower screen on the second display */
        return 1;
    }
#endif
    khdays_host_input(&now);
    if (!now.touching) {
        return 0;
    }
    *x = now.touch_x;
    *y = now.touch_y;
    return 1;
}
