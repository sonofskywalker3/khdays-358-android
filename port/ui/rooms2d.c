/* The port's list of rooms on the game's own screens (rooms2d.h): the game draws its four plates,
 * labels, button, note and hand as ever; the strings and the hand's place are the port's view's
 * (runtime/net/roomlist.c), written into buffers the hooked function lends on the game's stack (the
 * game reads only its own memory), cut to fit. docs/RUNTIME_MAP.md 16.2. */
#include "rooms2d.h"

#include "../host/screens/ui_text.h"
#include "../runtime/core/utf8.h"
#include "../runtime/net/roomlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLATES 4
#define NAME_UNITS 10    /* a room's name on a plate: the DS's nickname's, what the plates were made for */
#define FIELD_UNITS 16   /* a field's text on a plate: its last ones while it is longer */
#define NOTE_LINE 28     /* the upper note's lines, as long as the game's own */
#define ELLIPSIS 0x2026u /* in the game's font (text/font_eu_10.nftr) */

/* A growing UTF-16 string, ended by 0. */
typedef struct Wide {
    unsigned short *s;
    size_t n, room;
} Wide;

static Wide built; /* the string being made, then copied into the game's buffer */

static int put(Wide *w, unsigned short c)
{
    if (w->n + 2 > w->room) {
        const size_t want = w->room != 0 ? w->room * 2 : 32;
        unsigned short *more = (unsigned short *)realloc(w->s, want * sizeof(*more));
        if (more == NULL) {
            return 0;
        }
        w->s = more;
        w->room = want;
    }
    w->s[w->n++] = c;
    w->s[w->n] = 0;
    return 1;
}

static void clear(Wide *w)
{
    w->n = 0;
    put(w, 0);
    w->n = 0;
}

/* UTF-8 to UTF-16 (outside the plane, a '?'). */
static void from_utf8(Wide *w, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)(s != NULL ? s : ""); *p != 0;) {
        put(w, khdays_utf8_next(&p));
    }
}

static void from_ascii(Wide *w, const char *s)
{
    for (; *s != 0; ++s) {
        put(w, (unsigned char)*s);
    }
}

/* The first `units` of `w` (the last one an ellipsis when it was longer). */
static void cut_end(Wide *w, size_t units)
{
    if (w->n > units) {
        w->n = units - 1;
        put(w, ELLIPSIS);
    }
}

/* The last `units` of `w` (the first one an ellipsis when it was longer). */
static void cut_start(Wide *w, size_t units)
{
    if (w->n > units) {
        const size_t drop = w->n - units + 1;
        memmove(w->s + 1, w->s + drop, (w->n - drop + 1) * sizeof(*w->s));
        w->s[0] = ELLIPSIS;
        w->n -= drop - 1;
    }
}

/* `text` broken into lines of the note's length at its spaces. */
static void wrapped(Wide *w, const char *text)
{
    size_t line_start;
    from_utf8(w, text);
    line_start = 0;
    while (w->n - line_start > NOTE_LINE) {
        size_t at = line_start + NOTE_LINE;
        while (at > line_start && w->s[at] != ' ') {
            --at;
        }
        if (at == line_start) {
            break; /* one word longer than a line */
        }
        w->s[at] = '\n';
        line_start = at + 1;
    }
}

/* `w` into the game's buffer `out` of `units` (a last one an ellipsis when it is cut). */
static void *give(const Wide *w, void *out, int units)
{
    unsigned short *o = (unsigned short *)out;
    size_t n = w->n;
    if (o == NULL || units < 2) {
        return NULL;
    }
    if (n > (size_t)units - 1) {
        n = (size_t)units - 1;
        memcpy(o, w->s, (n - 1) * sizeof(*o));
        o[n - 1] = ELLIPSIS;
    } else if (n > 0) {
        memcpy(o, w->s, n * sizeof(*o));
    }
    o[n] = 0;
    return out;
}

/* The view while it is the game look's on the list's screens, else NULL (released either way by
 * done()). */
static const KhdaysRoomView *list_view(void)
{
    const KhdaysRoomView *v = khdays_roomlist_hold();
    if (v == NULL || !v->look_game ||
        (v->screen != KHDAYS_ROOMS_LIST && v->screen != KHDAYS_ROOMS_CREATE && v->screen != KHDAYS_ROOMS_PASSWORD) ||
        v->row_count < 1) {
        return NULL;
    }
    return v;
}

static void done(void)
{
    khdays_roomlist_release();
}

/* The row on plate `plate`, NULL when it is empty (the last row is the button's). */
static const KhdaysRoomRow *on_plate(const KhdaysRoomView *v, int plate)
{
    const int r = v->top + plate;
    return plate >= 0 && plate < PLATES && r >= 0 && r < v->row_count - 1 ? &v->rows[r] : NULL;
}

/* Whether `r` is the field being typed into. */
static int typing_into(const KhdaysRoomView *v, const KhdaysRoomRow *r)
{
    return r != NULL && v->editing && v->cursor >= 0 && v->cursor < v->row_count && r == &v->rows[v->cursor];
}

static int field_name(int which)
{
    return which == KHDAYS_ROOMFIELD_SEARCH ? TXT_ROOM_SEARCH
           : which == KHDAYS_ROOMFIELD_NAME ? TXT_ROOM_NAME
                                            : TXT_ROOM_PASSWORD_TAG;
}

static const char *word(int id, int language)
{
    const char *s = khdays_text_in(id, language);
    return s != NULL ? s : khdays_text_in(id, 1);
}

void *khdays_rooms2d_label(int plate, void *game, void *empty, void *out_buffer, int units)
{
    const KhdaysRoomView *v = list_view();
    void *out = game;
    if (v != NULL && plate >= 0 && plate < PLATES) {
        const KhdaysRoomRow *r = on_plate(v, plate);
        Wide *w = &built;
        clear(w);
        if (r == NULL) {
            out = empty;
        } else if (r->kind == KHDAYS_ROOMROW_ROOM) {
            if (r->text != NULL && r->text[0] != 0) {
                from_utf8(w, r->text);
            } else {
                for (size_t k = 0; k < sizeof(r->room.host) / sizeof(r->room.host[0]) && r->room.host[k] != 0; ++k) {
                    put(w, r->room.host[k]);
                }
            }
            cut_end(w, NAME_UNITS);
            out = give(w, out_buffer, units);
        } else if (r->kind == KHDAYS_ROOMROW_FIELD) {
            const int typing = typing_into(v, r);
            if ((r->text == NULL || r->text[0] == 0) && !typing) {
                from_utf8(w, word(field_name(r->which), v->language)); /* whole: the words fit */
            } else {
                if (r->which == KHDAYS_ROOMFIELD_PASSWORD) {
                    for (const unsigned char *p = (const unsigned char *)(r->text != NULL ? r->text : ""); *p != 0;
                         ++p) {
                        if ((*p & 0xc0) != 0x80) {
                            put(w, '*'); /* one a character */
                        }
                    }
                } else {
                    from_utf8(w, r->text);
                }
                if (typing) {
                    put(w, '_');
                }
                cut_start(w, FIELD_UNITS);
            }
            out = give(w, out_buffer, units);
        } else {
            out = empty;
        }
    }
    done();
    return out;
}

int khdays_rooms2d_colour(int plate, int game)
{
    const KhdaysRoomView *v = list_view();
    int out = game;
    if (v != NULL) {
        const KhdaysRoomRow *r = on_plate(v, plate);
        const int typing = typing_into(v, r);
        /* grey: an empty plate, a field showing its name */
        out = r == NULL || (r->kind == KHDAYS_ROOMROW_FIELD && (r->text == NULL || r->text[0] == 0) && !typing) ? 3 : 1;
    }
    done();
    return out;
}

void *khdays_rooms2d_detail(int plate, void *out_buffer, int units)
{
    const KhdaysRoomView *v = list_view();
    void *out = NULL;
    if (v != NULL) {
        const KhdaysRoomRow *r = on_plate(v, plate);
        if (r != NULL && r->kind == KHDAYS_ROOMROW_ROOM && r->room.players > 0) {
            char line[32];
            clear(&built);
            snprintf(line, sizeof(line), "%d/%d", r->room.players, r->room.most > 0 ? r->room.most : PLATES);
            from_ascii(&built, line);
            out = give(&built, out_buffer, units);
        }
    }
    done();
    return out;
}

int khdays_rooms2d_hand(int game)
{
    const KhdaysRoomView *v = list_view();
    int out = game;
    if (v != NULL) {
        out = v->cursor >= v->row_count - 1 ? 0 : v->cursor - v->top + 1;
        if (out < 0 || out > PLATES) {
            out = 0;
        }
    }
    done();
    return out;
}

void *khdays_rooms2d_text(int id, void *game, void *out_buffer, int units)
{
    const KhdaysRoomView *v = list_view();
    void *out = game;
    if (v != NULL && id == 0x3d) {
        /* the note: why the last join failed, else what the step asks */
        const int said = v->screen == KHDAYS_ROOMS_LIST && v->mods_needed[0] != 0 ? TXT_ROOM_NEEDS_MODS
                         : v->screen == KHDAYS_ROOMS_LIST && v->wrong_password    ? TXT_ROOM_WRONG_PASSWORD
                         : v->screen == KHDAYS_ROOMS_CREATE                       ? TXT_ROOM_PASSWORD_NOTE
                         : v->screen == KHDAYS_ROOMS_PASSWORD                     ? TXT_ROOM_PASSWORD_ASK
                                                                                  : -1;
        if (said == TXT_ROOM_NEEDS_MODS) {
            char line[1200];
            const int n = snprintf(line, sizeof(line), word(said, v->language), v->mods_needed);
            if (v->mods_more > 0 && n > 0 && n < (int)sizeof(line)) {
                snprintf(line + n, sizeof(line) - (size_t)n, " (+%d)", v->mods_more);
            }
            clear(&built);
            wrapped(&built, line);
            out = give(&built, out_buffer, units);
        } else if (said >= 0) {
            clear(&built);
            wrapped(&built, word(said, v->language));
            out = give(&built, out_buffer, units);
        }
    } else if (v != NULL && id == 0x3e && v->screen == KHDAYS_ROOMS_PASSWORD) {
        clear(&built);
        from_utf8(&built, word(TXT_ROOM_JOIN, v->language));
        out = give(&built, out_buffer, units);
    }
    done();
    return out;
}

/* The view while it is the game look's members' screen, else NULL (released by done()). */
static const KhdaysRoomView *members_view(void)
{
    const KhdaysRoomView *v = khdays_roomlist_hold();
    return v != NULL && v->look_game && v->screen == KHDAYS_ROOMS_MEMBERS ? v : NULL;
}

void *khdays_rooms2d_title(void *out_buffer, int units)
{
    const KhdaysRoomView *v = members_view();
    void *out = NULL;
    if (v != NULL && v->title != NULL && v->title[0] != 0) {
        clear(&built);
        from_utf8(&built, v->title);
        out = give(&built, out_buffer, units);
    }
    done();
    return out;
}

void *khdays_rooms2d_member_detail(int row, void *out_buffer, int units)
{
    const KhdaysRoomView *v = members_view();
    void *out = NULL;
    if (v != NULL && row >= 0 && row < PLATES) {
        /* the game fills its plates with the members there, in their places' order */
        int k = 0;
        for (int i = 0; i < v->row_count; ++i) {
            const KhdaysRoomRow *r = &v->rows[i];
            if (r->kind != KHDAYS_ROOMROW_MEMBER || !r->room.players) {
                continue;
            }
            if (k++ != row) {
                continue;
            }
            clear(&built);
            if (r->level > 0) {
                /* the player's level first, then the rest */
                char line[32];
                snprintf(line, sizeof(line), word(TXT_ROOM_LEVEL, v->language), r->level);
                from_utf8(&built, line);
                if (r->which == v->self || r->room.ping_ms >= 0) {
                    from_ascii(&built, "  ");
                }
            }
            if (r->which == v->self) {
                from_utf8(&built, word(TXT_ROOM_YOU, v->language));
            } else if (r->room.ping_ms >= 0) {
                char line[32];
                snprintf(line, sizeof(line), "%d ms", r->room.ping_ms);
                from_ascii(&built, line);
            }
            if (built.n > 0) {
                out = give(&built, out_buffer, units);
            }
            break;
        }
    }
    done();
    return out;
}
