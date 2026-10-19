/* khdays-saveslots-test [FOLDER]: the save files beyond the DS's three (runtime/menus/saveslots.h): SHA-1
 * on its known answers, what each file holds, the chip's view and the port's, a save into a port's file
 * committed and read back, the file saved last. */
#include "../runtime/menus/saveslots.h"
#include "../runtime/core/sha1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

void khdays_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    fputc('\n', stdout);
}

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* GameState_SetField's way: `bits` bits from bit `bit` of the state's +0x10 array. */
static void set_field(u8 *state, int bit, int bits, u32 value)
{
    for (int i = 0; i < bits; ++i) {
        const int b = bit + i, word = b / 32, shift = 31 - b % 32;
        u8 *w = state + 0x10 + 4 * word;
        u32 v = (u32)w[0] | (u32)w[1] << 8 | (u32)w[2] << 16 | (u32)w[3] << 24;
        const u32 one = (value >> (bits - 1 - i)) & 1u;
        v = (v & ~(1u << shift)) | one << shift;
        w[0] = (u8)v;
        w[1] = (u8)(v >> 8);
        w[2] = (u8)(v >> 16);
        w[3] = (u8)(v >> 24);
    }
}

/* A copy the game would sign: the magic, the state's SHA-1, the state (its day and its save
 * counter set). */
static void make_copy(u8 *copy, int day, u32 count)
{
    u8 *state = copy + 0x18;
    memset(copy, 0, KHDAYS_SLOT_COPY);
    for (u32 i = 0; i < KHDAYS_STATE_BYTES; ++i) {
        state[i] = (u8)(i * 7u);
    }
    memset(state + 0x10, 0, 0x200);
    set_field(state, 0, 9, (u32)day);
    set_field(state, 0xc77, 16, count >> 16);
    set_field(state, 0xc87, 16, count & 0xffffu);
    copy[0] = 0xa6;
    copy[1] = 0x92;
    copy[2] = 0xf5;
    copy[3] = 0xc8;
    khdays_sha1(state, KHDAYS_STATE_BYTES, copy + 4);
}

/* What the game reads of its slot `g`: the chip's bytes through the view. */
static void game_read(u32 at, u8 *out, u32 len)
{
    for (u32 i = 0; i < len; ++i) {
        u32 chip_at;
        const u8 *b = khdays_slots_byte(at + i, 0, &chip_at);
        out[i] = b != NULL ? *b : 0xff;
    }
}

static void game_write(u32 at, const u8 *in, u32 len)
{
    for (u32 i = 0; i < len; ++i) {
        u32 chip_at;
        u8 *b = khdays_slots_byte(at + i, 1, &chip_at);
        if (b != NULL) {
            *b = in[i];
        }
    }
}

static u32 slot_at(int g, int copy)
{
    return KHDAYS_SLOTS_START + (u32)g * KHDAYS_SLOT_BYTES + (u32)copy * KHDAYS_SLOT_COPY;
}

int main(int argc, char **argv)
{
    static u8 chip[0x10000], copy[KHDAYS_SLOT_COPY], back[KHDAYS_SLOT_COPY];
    char folder[MAX_PATH], path[MAX_PATH + 64];
    u8 digest[20];
    const u8 *state;

    /* SHA-1's known answers (FIPS 180-4 examples) */
    static const u8 abc[20] = {0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                               0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
    static const u8 two_blocks[20] = {0x84, 0x98, 0x3e, 0x44, 0x1c, 0x3b, 0xd2, 0x6e, 0xba, 0xae,
                                      0x4a, 0xa1, 0xf9, 0x51, 0x29, 0xe5, 0xe5, 0x46, 0x70, 0xf1};
    const char *message = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    khdays_sha1((const u8 *)"abc", 3, digest);
    CHECK(memcmp(digest, abc, 20) == 0);
    khdays_sha1((const u8 *)message, (u32)strlen(message), digest);
    CHECK(memcmp(digest, two_blocks, 20) == 0);

    if (argc > 1) {
        snprintf(folder, sizeof(folder), "%s", argv[1]);
    } else {
        char temp[MAX_PATH];
        GetTempPathA(sizeof(temp), temp);
        snprintf(folder, sizeof(folder), "%skhdays-saveslots-test-%lu", temp, (unsigned long)GetCurrentProcessId());
    }
    CreateDirectoryA(folder, NULL);

    /* the chip: file 1 a save (day 10, count 5), file 2 deleted as the game deletes (its copies'
     * first 0x20 bytes zero, the rest left), file 3 broken (both copies' digests wrong) */
    memset(chip, 0xff, sizeof(chip));
    make_copy(chip + slot_at(0, 0), 10, 5);
    make_copy(chip + slot_at(0, 1), 10, 5);
    make_copy(chip + slot_at(1, 0), 20, 9);
    memset(chip + slot_at(1, 0), 0, 0x20);
    memset(chip + slot_at(1, 1), 0, 0x20);
    make_copy(chip + slot_at(2, 0), 30, 7);
    chip[slot_at(2, 0) + 0x100] ^= 1;
    make_copy(chip + slot_at(2, 1), 30, 7);
    chip[slot_at(2, 1) + 0x100] ^= 1;
    khdays_slots_attach(chip, sizeof(chip), folder);

    CHECK(khdays_slots_state(0, &state) == KHDAYS_SLOT_SAVED && state == chip + slot_at(0, 0) + 0x18);
    CHECK(khdays_slots_field(state, 0, 9) == 10);
    CHECK(khdays_slots_state(1, NULL) == KHDAYS_SLOT_EMPTY);
    CHECK(khdays_slots_state(2, NULL) == KHDAYS_SLOT_BROKEN);
    CHECK(khdays_slots_state(3, NULL) == KHDAYS_SLOT_EMPTY);
    CHECK(khdays_slots_state(KHDAYS_SLOT_COUNT - 1, NULL) == KHDAYS_SLOT_EMPTY);
    CHECK(khdays_slots_latest() == 0);

    /* copy 0 broken, copy 1 good: copy 1 is taken, as the game's read takes it */
    chip[slot_at(0, 0) + 0x40] ^= 1;
    CHECK(khdays_slots_state(0, &state) == KHDAYS_SLOT_SAVED && state == chip + slot_at(0, 1) + 0x18);
    chip[slot_at(0, 0) + 0x40] ^= 1;

    /* the chip's own view: the game's slots are the chip's */
    CHECK(!khdays_slots_port_view());
    game_read(slot_at(2, 0), back, KHDAYS_SLOT_COPY);
    CHECK(memcmp(back, chip + slot_at(2, 0), KHDAYS_SLOT_COPY) == 0);

    /* the port's view: game slot 1 the file saved last, slots 2 and 3 blank, writes there dropped */
    khdays_slots_set_port_view(1);
    CHECK(khdays_slots_port_view() && khdays_slots_shown() == 0);
    game_read(slot_at(0, 0), back, KHDAYS_SLOT_COPY);
    CHECK(memcmp(back, chip + slot_at(0, 0), KHDAYS_SLOT_COPY) == 0);
    game_read(slot_at(1, 0), back, KHDAYS_SLOT_COPY);
    CHECK(back[0] == 0 && back[0x1f] == 0 && back[0x20] == 0xff);
    memset(copy, 0x55, sizeof(copy));
    game_write(slot_at(2, 0), copy, KHDAYS_SLOT_COPY);
    CHECK(chip[slot_at(2, 0) + 0x18 + 0x200] != 0x55);

    /* a save into file 50: the game writes its slot 1, the commit writes the port's file */
    khdays_slots_show(49);
    CHECK(khdays_slots_shown() == 49);
    game_read(slot_at(0, 0), back, 32);
    CHECK(back[0] == 0); /* never written: blank */
    make_copy(copy, 50, 12);
    game_write(slot_at(0, 0), copy, KHDAYS_SLOT_COPY);
    game_write(slot_at(0, 1), copy, KHDAYS_SLOT_COPY);
    CHECK(khdays_slots_state(49, &state) == KHDAYS_SLOT_SAVED && khdays_slots_field(state, 0, 9) == 50);
    CHECK(chip[slot_at(0, 0) + 0x18 + 0x10] == 0); /* file 1 untouched */
    CHECK(khdays_slots_state(0, NULL) == KHDAYS_SLOT_SAVED);
    CHECK(khdays_slots_latest() == 49);
    CHECK(khdays_slots_commit());
    snprintf(path, sizeof(path), "%s/slots/slot50.sav", folder);
    {
        FILE *f = fopen(path, "rb");
        long size = -1;
        CHECK(f != NULL);
        if (f != NULL) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fclose(f);
        }
        CHECK(size == (long)KHDAYS_SLOT_BYTES);
    }

    /* again from the files, as at the next start: file 50 read back, the view the chip's own */
    khdays_slots_attach(chip, sizeof(chip), folder);
    CHECK(!khdays_slots_port_view());
    CHECK(khdays_slots_state(49, &state) == KHDAYS_SLOT_SAVED && khdays_slots_field(state, 0, 9) == 50);
    CHECK(khdays_slots_latest() == 49);
    khdays_slots_set_port_view(1);
    CHECK(khdays_slots_shown() == 49);

    /* a deleted file under game slot 1 (WriteSlotHeaders: each copy's first 0x20 bytes zero):
     * the next time the view comes on, game slot 1 is the file saved last that is a save */
    memset(copy, 0, 0x20);
    game_write(slot_at(0, 0), copy, 0x20);
    game_write(slot_at(0, 1), copy, 0x20);
    CHECK(khdays_slots_state(49, NULL) == KHDAYS_SLOT_EMPTY);
    khdays_slots_set_port_view(0);
    khdays_slots_set_port_view(1);
    CHECK(khdays_slots_shown() == 0);
    CHECK(khdays_slots_commit());

    /* the page view (the game's own lists in pages): game slot g is file top + g -- a save into
     * game slot 3 is file top + 3's, the chip's own slots untouched */
    khdays_slots_show_page(47);
    CHECK(khdays_slots_page() == 47 && !khdays_slots_port_view() && khdays_slots_shown() == 47);
    game_read(slot_at(0, 0), back, 32);
    CHECK(back[0] == 0); /* file 48, never written: blank */
    make_copy(copy, 51, 13);
    game_write(slot_at(2, 0), copy, KHDAYS_SLOT_COPY);
    game_write(slot_at(2, 1), copy, KHDAYS_SLOT_COPY);
    CHECK(khdays_slots_state(49, &state) == KHDAYS_SLOT_SAVED && khdays_slots_field(state, 0, 9) == 51);
    CHECK(khdays_slots_state(2, NULL) == KHDAYS_SLOT_BROKEN); /* the chip's file 3 as it was */
    CHECK(khdays_slots_commit());
    /* the first page is the chip's three; the page kept within the files */
    khdays_slots_show_page(0);
    game_read(slot_at(2, 0), back, KHDAYS_SLOT_COPY);
    CHECK(memcmp(back, chip + slot_at(2, 0), KHDAYS_SLOT_COPY) == 0);
    khdays_slots_show_page(KHDAYS_SLOT_COUNT + 5);
    CHECK(khdays_slots_page() == KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS);
    khdays_slots_show_page(-5);
    CHECK(khdays_slots_page() == 0);
    /* the port's view and the chip's own end the page */
    khdays_slots_set_port_view(1);
    CHECK(khdays_slots_page() == -1 && khdays_slots_port_view());
    khdays_slots_show_page(10);
    khdays_slots_set_port_view(0);
    CHECK(khdays_slots_page() == -1 && !khdays_slots_port_view() && khdays_slots_shown() == 0);

    /* the view kept and put back around a write of the port's own (the autosave): the page with file
     * 99 in game slot 3, then the chip's own view again */
    {
        KhdaysSlotsView kept;
        khdays_slots_keep_view(&kept);
        khdays_slots_show_page(KHDAYS_SLOT_COUNT - KHDAYS_GAME_SLOTS);
        CHECK(khdays_slots_file_of(2) == KHDAYS_SLOT_COUNT - 1);
        khdays_slots_put_view(&kept);
        CHECK(khdays_slots_page() == -1 && !khdays_slots_port_view() && khdays_slots_file_of(0) == 0);
        khdays_slots_set_port_view(1);
        CHECK(khdays_slots_file_of(1) == -1); /* blank in the port's view */
        khdays_slots_set_port_view(0);
    }
    CHECK(khdays_slots_highest_counter() == 13); /* file 50's */

    /* a commit that fails (file 50 read-only): the game told so, and file 50 as it is on the disk,
     * nothing of it left for the next commit */
    SetFileAttributesA(path, FILE_ATTRIBUTE_READONLY);
    khdays_slots_show_page(47);
    make_copy(copy, 52, 14);
    game_write(slot_at(2, 0), copy, KHDAYS_SLOT_COPY);
    CHECK(khdays_slots_changed());
    CHECK(!khdays_slots_commit());
    CHECK(!khdays_slots_changed());
    CHECK(khdays_slots_state(49, &state) == KHDAYS_SLOT_SAVED && khdays_slots_field(state, 0, 9) == 51);
    CHECK(khdays_slots_commit());
    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    khdays_slots_set_port_view(0);

    DeleteFileA(path);
    snprintf(path, sizeof(path), "%s/slots", folder);
    RemoveDirectoryA(path);
    if (argc <= 1) {
        RemoveDirectoryA(folder);
    }
    if (failures == 0) {
        printf("khdays-saveslots-test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
