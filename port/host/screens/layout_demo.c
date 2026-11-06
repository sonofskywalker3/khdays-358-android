/* layout_demo.h. A thread of its own waits out the script's delays and puts its steps on the game's own event queue,
 * where a real mouse's events go: nothing reaches any other window or the desktop. */
#include "layout_demo.h"

#include "layout_editor_screen.h"
#include "../render/onescreen.h"
#include "../../ui/layout_editor.h"
#include "../../ui/situation.h"

#include <stdio.h>
#include <string.h>

#define FIELD_POLL_MS 50
#define STEP_WORD 16
#define SCRIPT_BYTES 8192

static Uint32 demo_event;
static SDL_AtomicInt shot_wanted;
static char script[SCRIPT_BYTES];
static int shot_number;
static const char *shot_dir;
static SDL_WindowID demo_window;

static void push_mouse(Uint32 type, float x, float y)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.windowID = demo_window;
        e.motion.x = x;
        e.motion.y = y;
        e.motion.state = SDL_BUTTON_LMASK;
    } else {
        e.button.windowID = demo_window;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
    }
    SDL_PushEvent(&e);
}

static void push_own(int what)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = demo_event;
    e.user.code = what;
    SDL_PushEvent(&e);
}

static int SDLCALL run(void *unused)
{
    char *step = script;
    (void)unused;
    while (step != NULL && *step != '\0') {
        char *next = strchr(step, ';');
        char word[STEP_WORD] = {0};
        int delay = 0;
        float x = 0.0f, y = 0.0f;
        if (next != NULL) {
            *next++ = '\0';
        }
        if (sscanf(step, "%d %15s %f %f", &delay, word, &x, &y) >= 2) {
            SDL_Delay((Uint32)delay);
            if (strcmp(word, "field") == 0) {
                while (!(khdays_onescreen_scene() == KHDAYS_SIT_FIELD && (khdays_onescreen_states() & (1u << KHDAYS_ST_HUD)))) {
                    SDL_Delay(FIELD_POLL_MS);
                }
            } else if (strcmp(word, "open") == 0 || strcmp(word, "ask") == 0) {
                push_own(1);
            } else if (strcmp(word, "done") == 0) {
                push_own(3);
            } else if (strcmp(word, "esc") == 0) {
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_EVENT_KEY_DOWN;
                e.key.windowID = demo_window;
                e.key.scancode = SDL_SCANCODE_ESCAPE;
                e.key.key = SDLK_ESCAPE;
                e.key.down = true;
                SDL_PushEvent(&e);
                e.type = SDL_EVENT_KEY_UP;
                e.key.down = false;
                SDL_PushEvent(&e);
            } else if (strcmp(word, "shot") == 0) {
                push_own(2);
            } else if (strcmp(word, "down") == 0) {
                push_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
            } else if (strcmp(word, "move") == 0) {
                push_mouse(SDL_EVENT_MOUSE_MOTION, x, y);
            } else if (strcmp(word, "up") == 0) {
                push_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
            } else if (strcmp(word, "quit") == 0) {
                SDL_Event e;
                SDL_zero(e);
                e.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&e);
            }
        }
        step = next;
    }
    return 0;
}

void khdays_layout_demo_start(SDL_Window *window)
{
    const char *e = SDL_getenv("KHDAYS_LAYOUT_DEMO");
    SDL_Thread *thread;
    shot_dir = SDL_getenv("KHDAYS_LAYOUT_DEMO_DIR");
    if (e == NULL) {
        return;
    }
    SDL_strlcpy(script, e, sizeof(script));
    {
        int w = 0, h = 0;
        const char *size = SDL_getenv("KHDAYS_LAYOUT_DEMO_WINDOW");
        if (size != NULL && sscanf(size, "%d %d", &w, &h) == 2) {
            SDL_SetWindowSize(window, w, h);
        }
    }
    demo_event = SDL_RegisterEvents(1);
    demo_window = SDL_GetWindowID(window);
    thread = SDL_CreateThread(run, "layout demo", NULL);
    SDL_DetachThread(thread);
}

int khdays_layout_demo_event(const SDL_Event *event, SDL_Renderer *renderer)
{
    if (demo_event == 0 || event->type != demo_event) {
        return 0;
    }
    if (event->user.code == 1) {
        khdays_editor_request();
    } else if (event->user.code == 3) {
        float x, y;
        khdays_layout_editor_done_at(renderer, &x, &y);
        push_mouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
        push_mouse(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
    } else {
        SDL_SetAtomicInt(&shot_wanted, 1);
    }
    return 1;
}

void khdays_layout_demo_frame(SDL_Renderer *renderer)
{
    if (demo_event != 0 && SDL_CompareAndSwapAtomicInt(&shot_wanted, 1, 0)) {
        /* the whole window, not the picture's letterboxed part */
        SDL_RendererLogicalPresentation mode;
        int lw = 0, lh = 0;
        SDL_Surface *s;
        SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
        SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        s = SDL_RenderReadPixels(renderer, NULL);
        SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
        if (s != NULL && shot_dir != NULL) {
            char path[512];
            SDL_snprintf(path, sizeof(path), "%s/shot_%03d.bmp", shot_dir, shot_number++);
            SDL_SaveBMP(s, path);
        }
        SDL_DestroySurface(s);
    }
}
