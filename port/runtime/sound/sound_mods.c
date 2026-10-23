/* The sounds mods replace (sound_mods.h). */
#include "sound_mods.h"
#include "../core/window.h"
#include "../core/rom.h"
#include "../../sound/stream_reader.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

/* The decoded files kept, at most (a few minutes of stereo music is tens of
 * MB): past it, the least recently used that nothing plays are let go, and
 * decoded again if they are wanted again. */
#define KEPT_BYTES (384u * 1024 * 1024)

/* --- The files, by identifier ---------------------------------------------------------- */

enum { NO_FILE, QUEUED, READY, FAILED, LET_GO };

typedef struct Entry {
    char identifier[160];
    char path[520];
    KhdaysOriginalLoop loop;
    volatile LONG state; /* written by the decoder when it is done */
    KhdaysReplacement file;
    int users; /* players and streams playing it */
    u64 used;  /* when it was last given to one */
    struct Entry *next;
} Entry;

static Entry *entries;
static u64 uses;
static SRWLOCK queue_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE queue_filled = CONDITION_VARIABLE_INIT;
static Entry **queue; /* the decoder's, the first next (grows) */
static int queued, queue_room;

static DWORD WINAPI decoder(LPVOID unused)
{
    (void)unused;
    for (;;) {
        Entry *e;
        char why[160];
        AcquireSRWLockExclusive(&queue_lock);
        while (queued == 0) {
            SleepConditionVariableSRW(&queue_filled, &queue_lock, INFINITE, 0);
        }
        e = queue[0];
#pragma warning(suppress : 6386) /* queued > 0 here (the wait above): one fewer, never a wrap */
        memmove(queue, queue + 1, (size_t)--queued * sizeof(queue[0]));
        ReleaseSRWLockExclusive(&queue_lock);
        if (khdays_replacement_load(&e->file, e->path, &e->loop, why, sizeof(why))) {
            InterlockedExchange(&e->state, READY);
        } else {
            fprintf(stderr, "sound: %s %s; the original plays\n", e->path, why);
            InterlockedExchange(&e->state, FAILED);
        }
    }
}

/* Puts a file that was let go in the decoder's queue: first when it is wanted now, last when it is
 * decoded ahead (what plays now does not wait behind those); one already queued and wanted now goes
 * first. */
static void enqueue(Entry *e, int now)
{
    static int started;
    int at = -1;
    if (!started) {
        started = 1;
        CreateThread(NULL, 0, decoder, NULL, 0, NULL);
    }
    AcquireSRWLockExclusive(&queue_lock);
    if (e->state == LET_GO) {
        if (queued == queue_room) {
            const int room = queue_room ? queue_room * 2 : 64;
            Entry **grown = (Entry **)realloc(queue, (size_t)room * sizeof(*grown));
            if (grown == NULL) {
                ReleaseSRWLockExclusive(&queue_lock);
                return; /* asked again at its next start */
            }
            queue = grown;
            queue_room = room;
        }
        InterlockedExchange(&e->state, QUEUED);
        queue[queued] = e;
        at = queued++;
        WakeConditionVariable(&queue_filled);
    } else {
        for (int i = 0; i < queued && at < 0; ++i) {
            at = queue[i] == e ? i : -1;
        }
    }
    if (now && at > 0) {
        memmove(queue + 1, queue, (size_t)at * sizeof(queue[0]));
        queue[0] = e;
    }
    ReleaseSRWLockExclusive(&queue_lock);
}

/* Past the budget, the least recently used files nothing plays are let go. */
static void keep_within_budget(void)
{
    for (;;) {
        u64 kept = 0;
        Entry *oldest = NULL;
        for (Entry *e = entries; e != NULL; e = e->next) {
            if (e->state == READY) {
                kept += e->file.frames * (u64)e->file.channels * sizeof(int16_t);
                if (e->users == 0 && (oldest == NULL || e->used < oldest->used)) {
                    oldest = e;
                }
            }
        }
        if (kept <= KEPT_BYTES || oldest == NULL) {
            return;
        }
        khdays_replacement_free(&oldest->file);
        oldest->state = LET_GO;
    }
}

static int trace;

void khdays_sound_mods_trace(int on)
{
    trace = on;
}

/* mods/<Mod>/sounds, in the folder the mods' order reads (the VFS's mods
 * root, "mods" beside the game). */
int khdays_sound_mods_present(void)
{
    static int present = -1;
    if (present < 0) {
        WIN32_FIND_DATAA found;
        HANDLE h = FindFirstFileA("mods\\*", &found);
        present = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char sounds[600];
                DWORD attributes;
                if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == '.') {
                    continue;
                }
                snprintf(sounds, sizeof(sounds), "mods\\%s\\sounds", found.cFileName);
                attributes = GetFileAttributesA(sounds);
                present = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
            } while (!present && FindNextFileA(h, &found));
            FindClose(h);
        }
        if (present) {
            fprintf(stderr, "sound: mods with sounds found: their files replace the originals\n");
        }
    }
    return present;
}

/* The entry for an identifier, looked for in the mods once (its state
 * NO_FILE if no mod has a file for it). */
static Entry *entry_find(const char *identifier)
{
    static const char *const extensions[] = {".wav", ".flac", ".ogg"};
    Entry *e;
    for (e = entries; e != NULL; e = e->next) {
        if (strcmp(e->identifier, identifier) == 0) {
            break;
        }
    }
    if (e == NULL) {
        e = calloc(1, sizeof(*e));
        if (e == NULL) {
            return NULL;
        }
        snprintf(e->identifier, sizeof(e->identifier), "%s", identifier);
        e->state = NO_FILE;
        for (size_t i = 0; i < sizeof(extensions) / sizeof(extensions[0]); ++i) {
            char relative[200];
            snprintf(relative, sizeof(relative), "sounds/%s%s", identifier, extensions[i]);
            if (khdays_mods_find(relative, e->path, sizeof(e->path))) {
                e->state = LET_GO; /* to decode */
                break;
            }
        }
        e->next = entries;
        entries = e;
    }
    return e;
}

/* The entry wanted now: NULL if there is no file for it (or it cannot be
 * decoded); decoded again if it was let go, with the original's loop for a
 * file that gives none. */
static Entry *entry_want(Entry *e, const KhdaysOriginalLoop *loop)
{
    if (e == NULL || e->state == NO_FILE || e->state == FAILED) {
        return NULL;
    }
    if (e->state == LET_GO && loop != NULL) {
        e->loop = *loop;
    }
    if (e->state == LET_GO || e->state == QUEUED) {
        enqueue(e, 1);
    }
    e->used = ++uses;
    return e;
}

static Entry *entry_for(const char *identifier, const KhdaysOriginalLoop *loop)
{
    return entry_want(entry_find(identifier), loop);
}

/* Whether a found file is a folder of its own (not "." or ".."). */
static int is_folder(const WIN32_FIND_DATAA *found)
{
    return (found->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found->cFileName[0] != '.';
}

/* A mod's sequence archive's entries (mods/<mod>/sounds/seqarc/<archive>/<entry>.*), queued to decode. */
static void decode_archive_ahead(const char *mod, const char *archive)
{
    char pattern[600];
    WIN32_FIND_DATAA file;
    HANDLE found;
    snprintf(pattern, sizeof(pattern), "mods\\%s\\sounds\\seqarc\\%s\\*", mod, archive);
    found = FindFirstFileA(pattern, &file);
    if (found == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char identifier[160];
        const char *dot = strrchr(file.cFileName, '.');
        Entry *e;
        if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || dot == NULL) {
            continue;
        }
        /* the mods' order picks the file (entry_find), whichever mod it was seen in */
        snprintf(identifier, sizeof(identifier), "seqarc/%s/%.*s", archive, (int)(dot - file.cFileName),
                 file.cFileName);
        e = entry_find(identifier);
        if (e != NULL && e->state == LET_GO) {
            enqueue(e, 0);
        }
    } while (FindNextFileA(found, &file));
    FindClose(found);
}

/* The sequence archives' entries the mods have (sounds/seqarc/<archive>/<entry>): the game's sound
 * effects and voices, short, decoded ahead so that even their first play is the mod's (they take no
 * loop of the original's). Once, under the bridge's lock. */
static void decode_entries_ahead(void)
{
    static int done;
    WIN32_FIND_DATAA mod;
    HANDLE mods;
    if (done) {
        return;
    }
    done = 1;
    mods = FindFirstFileA("mods\\*", &mod);
    if (mods == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        char pattern[600];
        WIN32_FIND_DATAA archive;
        HANDLE archives;
        if (!is_folder(&mod)) {
            continue;
        }
        snprintf(pattern, sizeof(pattern), "mods\\%s\\sounds\\seqarc\\*", mod.cFileName);
        archives = FindFirstFileA(pattern, &archive);
        if (archives == INVALID_HANDLE_VALUE) {
            continue;
        }
        do {
            if (is_folder(&archive)) {
                decode_archive_ahead(mod.cFileName, archive.cFileName);
            }
        } while (FindNextFileA(archives, &archive));
        FindClose(archives);
    } while (FindNextFileA(mods, &mod));
    FindClose(mods);
}

/* --- The waves --------------------------------------------------------------------- */

static void set_entry(Entry **slot, Entry *e);

/* Whether any mod has a sounds/wave folder (looked at once). */
static int wave_folders(void)
{
    static int found = -1;
    if (found < 0) {
        WIN32_FIND_DATAA mod;
        HANDLE h = FindFirstFileA("mods\\*", &mod);
        found = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char wave[600];
                DWORD attributes;
                snprintf(wave, sizeof(wave), "mods\\%s\\sounds\\wave", mod.cFileName);
                attributes = GetFileAttributesA(wave);
                found = mod.cFileName[0] != '.' && attributes != INVALID_FILE_ATTRIBUTES &&
                        (attributes & FILE_ATTRIBUTE_DIRECTORY);
            } while (!found && FindNextFileA(h, &mod));
            FindClose(h);
        }
    }
    return found;
}

static u32 rd32(u32 address)
{
    const u8 *p = (const u8 *)KHDAYS_W(address);
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* The wave archives in memory that have replaced waves: a SWAR (+8 its
 * size, +0x38 how many waves, +0x3c where each is: from the archive below
 * 0x02000000, else in memory -- a wave archive loaded wave by wave, as
 * LoadSingleWaves fills it in), and each wave's file. */
#define WAVE_ARCHIVES 32

static struct {
    u32 address; /* 0: none */
    int number;
    u32 size;
    int waves;
    Entry **entry; /* per wave, NULL: none */
    u64 used;
} wave_archive[WAVE_ARCHIVES];

/* Whether a mod has a folder for wave archive `number` (sounds/wave/<its
 * name or number>), looked at once for each. */
static int has_folder(const KhdaysSoundArchive *archive, int number)
{
    static signed char *known; /* by archive number: 0 not looked at, 1 has, -1 has not (grows) */
    static int known_count;
    char identifier[160], *slash;
    WIN32_FIND_DATAA mod;
    HANDLE h;
    if (number < 0) {
        return 0;
    }
    if (number >= known_count) {
        int count = known_count ? known_count : 256;
        signed char *grown;
        while (count <= number)
            count *= 2;
        grown = (signed char *)realloc(known, (size_t)count);
        if (grown == NULL) {
            return 0;
        }
        memset(grown + known_count, 0, (size_t)(count - known_count));
        known = grown;
        known_count = count;
    }
    if (known[number] != 0) {
        return known[number] > 0;
    }
    known[number] = -1;
    if (!khdays_sound_archive_identifier(archive, KHDAYS_SOUND_WAVE, number, 0, identifier, sizeof(identifier)) ||
        (slash = strrchr(identifier, '/')) == NULL) {
        return 0;
    }
    *slash = '\0'; /* wave/<archive> */
    h = FindFirstFileA("mods\\*", &mod);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char folder[600];
            DWORD attributes;
            snprintf(folder, sizeof(folder), "mods\\%s\\sounds\\%s", mod.cFileName, identifier);
            attributes = GetFileAttributesA(folder);
            if (mod.cFileName[0] != '.' && attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                known[number] = 1;
            }
        } while (known[number] < 0 && FindNextFileA(h, &mod));
        FindClose(h);
    }
    return known[number] > 0;
}

/* What each channel bound to a wave plays. */
static struct {
    Entry *entry;
    u32 rate; /* the original's */
} wave_channel[16];

/* Where wave `w` of the SWAR at `address` is; 0 while it is not loaded. */
static u32 wave_of(u32 address, int w)
{
    const u32 at = rd32(address + 0x3c + 4u * (u32)w);
    return at == 0 ? 0 : at < 0x02000000u ? address + at : at;
}

/* The original's loop of the wave at `wave`. */
static void wave_loop(u32 wave, KhdaysOriginalLoop *loop)
{
    /* SNDWaveParam: +0 format, +1 loop, +2 rate, +4 timer, +6 the loop's start and +8 its length,
     * in words */
    const u8 *param = (const u8 *)KHDAYS_W(wave);
    const u32 per_word = param[0] == 0 ? 4 : param[0] == 1 ? 2 : 8;
    const u32 header = param[0] == 2 ? 8 : 0;
    const u32 loop_words = (u32)(param[6] | param[7] << 8);
    const u32 words = loop_words + rd32(wave + 8);
    loop->rate = (u32)(param[2] | param[3] << 8);
    loop->looped = param[1] != 0;
    loop->start = loop_words * per_word >= header ? loop_words * per_word - header : 0;
    loop->end = words * per_word - header;
}

void khdays_sound_mods_wave_archive(const KhdaysSoundArchive *archive, int number, u32 address)
{
    int slot = 0, waves;
    const u8 *swar = (const u8 *)KHDAYS_W(address);
    char name[160];
    Entry **entry;
    int any = 0;
    if (!khdays_sound_mods_present() || !wave_folders() || archive == NULL || !khdays_game_ram(address) ||
        memcmp(swar, "SWAR", 4) != 0) {
        return;
    }
    for (int i = 0; i < WAVE_ARCHIVES; ++i) {
        if (wave_archive[i].address == address) {
            if (wave_archive[i].number == number && wave_archive[i].size == rd32(address + 8)) {
                wave_archive[i].used = ++uses;
                return; /* known */
            }
            slot = i;
            break;
        }
        if (wave_archive[i].used < wave_archive[slot].used) {
            slot = i; /* the least recently loaded, if it is new */
        }
    }
    waves = (int)rd32(address + 0x38);
    if (waves <= 0 || waves > 4096 || !has_folder(archive, number)) {
        return;
    }
    entry = calloc((size_t)waves, sizeof(*entry));
    if (entry == NULL) {
        return;
    }
    for (int w = 0; w < waves; ++w) {
        if (khdays_sound_archive_identifier(archive, KHDAYS_SOUND_WAVE, number, w, name, sizeof(name))) {
            Entry *e = entry_find(name);
            if (e != NULL && e->state != NO_FILE) {
                entry[w] = e;
                any = 1;
            }
        }
    }
    if (!any) {
        free(entry);
        return;
    }
    free(wave_archive[slot].entry); /* its entries stay, as every entry does */
    wave_archive[slot].address = address;
    wave_archive[slot].number = number;
    wave_archive[slot].size = rd32(address + 8);
    wave_archive[slot].waves = waves;
    wave_archive[slot].entry = entry;
    wave_archive[slot].used = ++uses;
    /* its replaced waves decoded ahead (the voices of a character loaded with it), so that even
     * their first play is the mod's: those already in memory, with their loop */
    decode_entries_ahead();
    for (int w = 0; w < waves; ++w) {
        const u32 wave = entry[w] != NULL && entry[w]->state == LET_GO ? wave_of(address, w) : 0;
        if (wave != 0) {
            wave_loop(wave, &entry[w]->loop);
            enqueue(entry[w], 0);
        }
    }
}

int khdays_sound_mods_bind_wave(int channel, uint32_t sad, uint32_t len, int format)
{
    (void)len;
    if (channel < 0 || channel > 15) {
        return -1;
    }
    if (wave_channel[channel].entry != NULL) {
        set_entry(&wave_channel[channel].entry, NULL);
    }
    for (int i = 0; i < WAVE_ARCHIVES; ++i) {
        const u32 address = wave_archive[i].address;
        if (address == 0) {
            continue;
        }
        if (memcmp((const u8 *)KHDAYS_W(address), "SWAR", 4) != 0 || rd32(address + 8) != wave_archive[i].size) {
            wave_archive[i].address = 0; /* its memory went to something else */
            continue;
        }
        for (int w = 0; w < wave_archive[i].waves; ++w) {
            KhdaysOriginalLoop loop;
            const u32 wave = wave_of(address, w);
            if (wave == 0 || wave + 12 != sad) {
                continue;
            }
            {
                wave_loop(wave, &loop);
                Entry *e = entry_want(wave_archive[i].entry[w], &loop);
                if (e == NULL || e->state != READY || (int)*(const u8 *)KHDAYS_W(wave) != format) {
                    return -1; /* the original, until its file is decoded */
                }
                set_entry(&wave_channel[channel].entry, e);
                wave_channel[channel].rate = loop.rate;
                if (trace) {
                    fprintf(stderr, "sound: %s plays from %s on channel %d\n", e->identifier, e->path, channel);
                }
                return KHDAYS_WAVE_HANDLE | channel;
            }
        }
    }
    return -1;
}

int khdays_sound_mods_wave_sample(int handle, uint64_t index, int32_t *sample)
{
    const int channel = handle & 15;
    const Entry *e = wave_channel[channel].entry;
    const KhdaysReplacement *r;
    double at;
    float v;
    long s;
    if (!(handle & KHDAYS_WAVE_HANDLE) || e == NULL || e->state != READY) {
        *sample = 0;
        return 1; /* bound: the channel keeps to it until its next start */
    }
    r = &e->file;
    at = r->rate == wave_channel[channel].rate || wave_channel[channel].rate == 0
             ? (double)index
             : (double)index * (double)r->rate / (double)wave_channel[channel].rate;
    v = r->channels == 2 ? 0.5f * (khdays_replacement_sample(r, at, 0) + khdays_replacement_sample(r, at, 1))
                         : khdays_replacement_sample(r, at, 0);
    s = lrintf(v * 32768.0f);
    *sample = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
    return 1;
}

/* --- What plays --------------------------------------------------------------------- */

/* A sequence on an ARM7 player: armed at its start; prepared once the ARM7
 * has done the command list that prepares it (the player shows it from
 * then on, as NNSi_SndPlayerMain waits for it); playing once the player is
 * started; over once it is no longer active. */
enum { IDLE, ARMED, PREPARED, PLAYING };

static struct {
    Entry *entry;
    int state;
    u32 tag;    /* the command list that prepares it */
    u64 played; /* output samples since it started, its pauses not counted */
} sequence[16];

static struct {
    Entry *entry;
    u32 start, rate;
} stream[4];

static void set_entry(Entry **slot, Entry *e)
{
    if (*slot != NULL) {
        --(*slot)->users;
    }
    *slot = e;
    if (e != NULL) {
        ++e->users;
    }
    keep_within_budget();
}

void khdays_sound_mods_sequence(const KhdaysSoundArchive *archive, int arm7_player, int kind, int number, int entry,
                                u32 command_tag)
{
    char identifier[160];
    if (!khdays_sound_mods_present() || archive == NULL || arm7_player < 0 || arm7_player > 15 ||
        !khdays_sound_archive_identifier(archive, kind, number, entry, identifier, sizeof(identifier))) {
        return;
    }
    decode_entries_ahead();
    set_entry(&sequence[arm7_player].entry, entry_for(identifier, NULL));
    sequence[arm7_player].state = sequence[arm7_player].entry != NULL ? ARMED : IDLE;
    sequence[arm7_player].tag = command_tag;
    sequence[arm7_player].played = 0;
}

void khdays_sound_mods_stream(const KhdaysSoundArchive *archive, int stream_player, int number, uint32_t start,
                              uint32_t rate, const KhdaysOriginalLoop *loop)
{
    char identifier[160];
    if (!khdays_sound_mods_present() || archive == NULL || stream_player < 0 || stream_player > 3 ||
        !khdays_sound_archive_identifier(archive, KHDAYS_SOUND_STREAM, number, 0, identifier, sizeof(identifier))) {
        return;
    }
    set_entry(&stream[stream_player].entry, entry_for(identifier, loop));
    stream[stream_player].start = start;
    stream[stream_player].rate = rate;
}

int khdays_sound_mods_replaces_player(int p)
{
    return sequence[p].state == PLAYING && sequence[p].entry != NULL && sequence[p].entry->state == READY;
}

int khdays_sound_mods_replaces_stream(int s)
{
    return stream[s].entry != NULL && stream[s].entry->state == READY;
}

/* 7-bit volume and panning: 0..126 are N/128, 127 is 128/128 (as the mixers). */
static float factor(uint32_t value)
{
    return (value == 127 ? 128.0f : (float)value) / 128.0f;
}

void khdays_sound_mods_mix(const KhdaysSoundHw *hw, const KhdaysSnd7Driver *driver, uint32_t rate,
                           uint32_t finished_tag, const float player_gain[16], const float stream_gain[4], float out[2])
{
    static const float divider[4] = {1.0f, 2.0f, 4.0f, 16.0f};
    const uint32_t soundcnt = khdays_sound_hw_read(hw, 0x100, 2);
    const float master = (soundcnt & 0x8000) ? factor(soundcnt & 0x7f) : 0.0f;
    /* the sequences */
    for (int p = 0; p < 16; ++p) {
        const uint8_t *record;
        if (sequence[p].state == IDLE || driver->player_record == NULL) {
            continue;
        }
        record = driver->player_record(p);
        if (sequence[p].state == ARMED && (s32)(finished_tag - sequence[p].tag) >= 0) {
            /* SND_IsFinishedCommandTag's test */
            sequence[p].state = (record[0] & 1) ? PREPARED : IDLE;
        }
        if (sequence[p].state == PREPARED && (record[0] & 3) == 3) {
            sequence[p].state = PLAYING;
            if (trace) {
                fprintf(stderr, "sound: %s plays from %s on sequence player %d (%s, at %d tenths of a dB)\n",
                        sequence[p].entry->identifier, sequence[p].entry->path, p,
                        sequence[p].entry->state == READY ? "decoded" : "still decoding",
                        (int16_t)(record[6] | record[7] << 8));
            }
        } else if (sequence[p].state >= PREPARED && !(record[0] & 1)) {
            if (trace) {
                fprintf(stderr, "sound: %s ends on sequence player %d after %llu output samples\n",
                        sequence[p].entry->identifier, p, (unsigned long long)sequence[p].played);
            }
            sequence[p].state = IDLE;
            set_entry(&sequence[p].entry, NULL);
            continue;
        }
        if (sequence[p].state == IDLE) {
            set_entry(&sequence[p].entry, NULL);
            continue;
        }
        if (sequence[p].state != PLAYING) {
            continue;
        }
        if (!(record[0] & 4)) {
            ++sequence[p].played;
        }
        if (sequence[p].entry->state == READY) {
            const KhdaysReplacement *r = &sequence[p].entry->file;
            const int16_t decibels = (int16_t)(record[6] | record[7] << 8);
            const float gain = decibels <= -723 ? 0.0f : powf(10.0f, (float)decibels / 200.0f);
            const double at = (double)sequence[p].played * (double)r->rate / (double)rate;
            const float g = gain * player_gain[p] * master;
            out[0] += khdays_replacement_sample(r, at, 0) * g;
            out[1] += khdays_replacement_sample(r, at, 1) * g;
        }
    }
    /* the streams: each channel's own file channel, through its volume and pan */
    for (int n = 0; n < 16; ++n) {
        const KhdaysSoundChannel *c = &hw->channels[n];
        int s;
        if (c->source < 0 || (c->source & KHDAYS_WAVE_HANDLE) || !c->playing || c->played == 0) {
            continue;
        }
        s = khdays_streams_handle_player(c->source);
        if (khdays_sound_mods_replaces_stream(s)) {
            const KhdaysReplacement *r = &stream[s].entry->file;
            const uint32_t cnt = khdays_sound_hw_control(hw, n);
            const uint32_t reload = khdays_sound_hw_read(hw, (uint32_t)n * 16 + 8, 2);
            const double fraction =
                reload < 0x10000u ? (double)(c->counter - reload) / (double)(0x10000u - reload) : 0.0;
            /* at the original's rate the file's own samples, held as the
             * channel holds them; at another, between them */
            const double sample =
                (double)stream[s].start + (double)(c->played - 1) + (r->rate == stream[s].rate ? 0.0 : fraction);
            const double at = stream[s].rate != 0 ? sample * (double)r->rate / (double)stream[s].rate : 0.0;
            const float v = khdays_replacement_sample(r, at, khdays_streams_handle_channel(c->source)) *
                            factor(cnt & 0x7f) / divider[(cnt >> 8) & 3] * stream_gain[s] * master;
            const float pan = factor((cnt >> 16) & 0x7f);
            out[0] += v * (1.0f - pan);
            out[1] += v * pan;
        }
    }
}
