/* The player's input written down and played back (replay.h). Its clock is the game's reads of the
 * input: each Pad_Sample's and each other read of the buttons (a movie's, which reads KEYINPUT itself
 * while no game frame runs), counted from the start -- the same count whatever the host's pace.
 * The file is text: a first line "khdays input 1", then one line per change -- "i <read> <the
 * snapshot's fields>" at a Pad_Sample, "k <read> <the buttons>" at another read -- and one per clock
 * reading -- "t <hour> <minute> <second>", in the order they happened. */
#include "replay.h"
#include "../core/session_log.h"
#include "../../host/app/files.h"
#include "../../host/app/instance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST_LINE "khdays input 1"
#define SAVE_ROOM (4u << 20)  /* more than the chip's save */
#define PORT_SLOTS 99         /* saves/slots/slot01.sav .. slot99.sav (saveslots.c) */

typedef struct Change {
    u32 frame;   /* the read it comes at */
    int buttons; /* 1: a "k" line, the buttons alone */
    KhdaysInputSnapshot snapshot;
} Change;

typedef struct Reading {
    int hour, minute, second;
} Reading;

/* writing */
static FILE *record;
static int record_decided;
static KhdaysInputSnapshot written;
static int written_any;

/* playing back */
static int playing;
static Change *changes;
static int change_count, change_next;
static KhdaysInputSnapshot held;
static Reading *readings;
static int reading_count, reading_next;
static int told_end;

static u32 reads; /* the input's reads so far: the recording's clock */

/* --- The snapshot as a line ------------------------------------------------------------- */

static void write_snapshot(FILE *f, u32 frame, const KhdaysInputSnapshot *s)
{
    fprintf(f, "i %u %x %x %x %d %d %u %d %d %u %u %u %u %u %d %d %u %u %x\n", frame, s->ds_buttons,
            s->ds_buttons_field, s->actions, s->camera_x, s->camera_y, s->camera_speed, s->move_x, s->move_y,
            s->run_threshold, s->walk_speed, s->touch_x, s->touch_y, s->touching, s->mouse_x, s->mouse_y,
            s->mouse_speed, s->ui_open, s->enhancements);
}

static int read_snapshot(const char *line, Change *c)
{
    unsigned buttons, field, actions, speed, run, walk, tx, ty, touching, mouse_speed, ui_open, enh;
    int cx, cy, mx, my, px, py;
    memset(c, 0, sizeof(*c));
    if (sscanf(line, "i %u %x %x %x %d %d %u %d %d %u %u %u %u %u %d %d %u %u %x", &c->frame, &buttons, &field,
               &actions, &cx, &cy, &speed, &mx, &my, &run, &walk, &tx, &ty, &touching, &px, &py, &mouse_speed,
               &ui_open, &enh) != 19) {
        return 0;
    }
    c->snapshot.ds_buttons = (uint16_t)buttons;
    c->snapshot.ds_buttons_field = (uint16_t)field;
    c->snapshot.actions = actions;
    c->snapshot.camera_x = (int16_t)cx;
    c->snapshot.camera_y = (int16_t)cy;
    c->snapshot.camera_speed = (uint16_t)speed;
    c->snapshot.move_x = (int16_t)mx;
    c->snapshot.move_y = (int16_t)my;
    c->snapshot.run_threshold = (uint8_t)run;
    c->snapshot.walk_speed = (uint8_t)walk;
    c->snapshot.touch_x = (uint16_t)tx;
    c->snapshot.touch_y = (uint16_t)ty;
    c->snapshot.touching = (uint8_t)touching;
    c->snapshot.mouse_x = px;
    c->snapshot.mouse_y = py;
    c->snapshot.mouse_speed = (uint16_t)mouse_speed;
    c->snapshot.ui_open = (uint8_t)ui_open;
    c->snapshot.enhancements = (uint16_t)enh;
    return 1;
}

/* Whether two snapshots hold the same input (the generation aside). */
static int same(const KhdaysInputSnapshot *a, const KhdaysInputSnapshot *b)
{
    KhdaysInputSnapshot x = *a, y = *b;
    x.generation = y.generation = 0;
    return memcmp(&x, &y, sizeof(x)) == 0;
}

/* --- Playing back ------------------------------------------------------------------------- */

void khdays_replay_init(void)
{
    const char *path = getenv("KHDAYS_REPLAY");
    FILE *f;
    char line[256];
    int changes_room = 0, readings_room = 0;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    f = fopen(path, "r");
    if (f == NULL || fgets(line, sizeof(line), f) == NULL || strncmp(line, FIRST_LINE, strlen(FIRST_LINE)) != 0) {
        fprintf(stderr, "KHDAYS_REPLAY: %s is not a recording of the player's input\n", path);
        exit(2);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        if (line[0] == 'i' || line[0] == 'k') {
            unsigned buttons;
            int ok;
            if (change_count == changes_room) {
                Change *grown;
                changes_room = changes_room ? changes_room * 2 : 1024;
                grown = (Change *)realloc(changes, (size_t)changes_room * sizeof(*changes));
                if (grown == NULL) {
                    fprintf(stderr, "KHDAYS_REPLAY: no memory for %s\n", path);
                    exit(2);
                }
                changes = grown;
            }
            if (changes != NULL && line[0] == 'k') {
                memset(&changes[change_count], 0, sizeof(changes[change_count]));
                changes[change_count].buttons = 1;
                ok = sscanf(line, "k %u %x", &changes[change_count].frame, &buttons) == 2;
                changes[change_count].snapshot.ds_buttons = (uint16_t)buttons;
            } else {
                ok = changes != NULL && read_snapshot(line, &changes[change_count]);
            }
            if (!ok) {
                fprintf(stderr, "KHDAYS_REPLAY: cannot read \"%s\"\n", line);
                exit(2);
            }
            ++change_count;
        } else if (line[0] == 't') {
            if (reading_count == readings_room) {
                Reading *grown;
                readings_room = readings_room ? readings_room * 2 : 64;
                grown = (Reading *)realloc(readings, (size_t)readings_room * sizeof(*readings));
                if (grown == NULL) {
                    fprintf(stderr, "KHDAYS_REPLAY: no memory for %s\n", path);
                    exit(2);
                }
                readings = grown;
            }
            if (readings == NULL || sscanf(line, "t %d %d %d", &readings[reading_count].hour,
                                           &readings[reading_count].minute, &readings[reading_count].second) != 3) {
                fprintf(stderr, "KHDAYS_REPLAY: cannot read \"%s\"\n", line);
                exit(2);
            }
            ++reading_count;
        }
    }
    fclose(f);
    playing = 1;
    fprintf(stderr, "replay: %s, %d changes up to read %u, %d clock readings\n", path, change_count,
            change_count ? changes[change_count - 1].frame : 0u, reading_count);
}

int khdays_replay_playing(void)
{
    return playing;
}

/* --- Writing ------------------------------------------------------------------------------- */

/* Copies `from` to `to` (creating its folder): 0 when `from` is not there. */
static int copy_file(const char *from, const char *to, void *buffer)
{
    const long size = khdays_host_read_file(from, buffer, SAVE_ROOM);
    return size > 0 && khdays_host_write_file_atomic(to, buffer, (size_t)size);
}

/* The saves as they are before the game writes any: what a playback starts from. */
static void copy_saves(const char *base)
{
    const char *folder = khdays_host_user_folder();
    char from[320], to[320];
    int copied = 0;
    void *buffer = malloc(SAVE_ROOM);
    if (buffer == NULL) {
        return;
    }
    snprintf(from, sizeof(from), "%s/khdays.sav", folder);
    snprintf(to, sizeof(to), "%s.saves/khdays.sav", base);
    copied += copy_file(from, to, buffer);
    for (int n = 1; n <= PORT_SLOTS; ++n) {
        snprintf(from, sizeof(from), "%s/slots/slot%02d.sav", folder, n);
        snprintf(to, sizeof(to), "%s.saves/slots/slot%02d.sav", base, n);
        copied += copy_file(from, to, buffer);
    }
    free(buffer);
    khdays_log("input: %d save files copied to %s.saves", copied, base);
}

/* At the first game frame: the input is written down when the session log is on from the start (and
 * the run is the player's, not a script's or a playback's). */
static void decide_record(void)
{
    const char *log = khdays_log_path();
    char base[256], path[272];
    size_t n;
    record_decided = 1;
    if (playing || (getenv("KHDAYS_INPUT") != NULL && getenv("KHDAYS_RECORD_INPUT") == NULL) || log == NULL) {
        return;
    }
    n = strlen(log);
    if (n < 4 || n - 4 >= sizeof(base) || strcmp(log + n - 4, ".log") != 0) {
        return;
    }
    memcpy(base, log, n - 4);
    base[n - 4] = '\0';
    copy_saves(base);
    snprintf(path, sizeof(path), "%s.input", base);
    record = fopen(path, "w");
    if (record == NULL) {
        khdays_log("input: cannot write %s", path);
        return;
    }
    fprintf(record, FIRST_LINE "\n");
    khdays_log("input: written down in %s", path);
}

/* Playing back: the changes up to this read. */
static void advance(void)
{
    while (change_next < change_count && changes[change_next].frame <= reads) {
        const Change *c = &changes[change_next++];
        if (c->buttons) {
            held.ds_buttons = held.ds_buttons_field = c->snapshot.ds_buttons;
        } else {
            held = c->snapshot;
        }
    }
    if (change_next == change_count && !told_end) {
        told_end = 1;
        fprintf(stderr, "replay: the recording ends at read %u\n", reads);
        khdays_log("replay: the recording ends at read %u", reads);
    }
}

void khdays_replay_snapshot(u32 frame, KhdaysInputSnapshot *snapshot, u16 script_keys)
{
    KhdaysInputSnapshot taken;
    ++reads;
    if (playing) {
        advance();
        held.generation = frame;
        *snapshot = held;
        return;
    }
    if (!record_decided) {
        decide_record();
    }
    taken = *snapshot;
    taken.ds_buttons |= script_keys;
    taken.ds_buttons_field |= script_keys;
    if (record != NULL && (!written_any || !same(&taken, &written))) {
        write_snapshot(record, reads, &taken);
        written = taken;
        written_any = 1;
        fflush(record); /* a crash keeps what came before it */
    }
}

void khdays_replay_read(u16 buttons)
{
    ++reads;
    if (playing) {
        advance();
        return;
    }
    if (!record_decided) {
        decide_record();
    }
    if (record != NULL && buttons != written.ds_buttons) {
        fprintf(record, "k %u %x\n", reads, buttons);
        written.ds_buttons = written.ds_buttons_field = buttons;
        written_any = 1;
        fflush(record);
    }
}

u16 khdays_replay_buttons(void)
{
    return held.ds_buttons;
}

int khdays_replay_touch(int *x, int *y)
{
    if (!held.touching) {
        return 0;
    }
    *x = held.touch_x;
    *y = held.touch_y;
    return 1;
}

void khdays_replay_time(int *hour, int *minute, int *second)
{
    if (playing && reading_next < reading_count) {
        *hour = readings[reading_next].hour;
        *minute = readings[reading_next].minute;
        *second = readings[reading_next].second;
        ++reading_next;
        return;
    }
    if (!playing && !record_decided) {
        decide_record(); /* the game can read the clock before its first frame */
    }
    khdays_host_local_time(hour, minute, second);
    if (record != NULL) {
        fprintf(record, "t %d %d %d\n", *hour, *minute, *second);
    }
}
