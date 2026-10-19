/* The sound archive (archive.h), read as NitroSystem's sndarc.c reads it: the header's offsets to the
 * symbols (SYMB), the records (INFO), the FAT and the files; SYMB and INFO each 8 tables of a count and
 * offsets (docs/SOUND.md, "The categories"). */
#include "archive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TABLE_SEQUENCE = 0,
    TABLE_SEQUENCE_ARCHIVE = 1,
    TABLE_BANK = 2,
    TABLE_WAVE_ARCHIVE = 3,
    TABLE_PLAYER = 4,
    TABLE_STREAM_PLAYER = 6,
    TABLE_STREAM = 7
};

#define NAME_CHARS 64

/* A table of names, kept in one block. */
typedef struct Names {
    int count;
    char (*name)[NAME_CHARS];
} Names;

struct KhdaysSoundArchive {
    Names sequence, sequence_archive, wave_archive, player, stream_player, stream;
    Names *entry; /* each sequence archive's entries */
    KhdaysArchiveRead read;
    void *context;
    uint32_t info, fat; /* the blocks' offsets */
};

typedef struct Reader {
    KhdaysArchiveRead read;
    void *context;
    int ok;
} Reader;

static uint32_t read32(Reader *r, uint32_t offset)
{
    uint8_t b[4];
    if (r->read(r->context, offset, b, 4) != 4) {
        r->ok = 0;
        return 0;
    }
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static int alloc_names(Reader *r, Names *names, uint32_t count)
{
    names->count = 0;
    names->name = count != 0 ? calloc(count, NAME_CHARS) : NULL;
    if (count != 0 && names->name == NULL) {
        r->ok = 0;
        return 0;
    }
    names->count = (int)count;
    return 1;
}

static void read_name(Reader *r, uint32_t symb, uint32_t name, char *out)
{
    if (name != 0) {
        r->read(r->context, symb + name, out, NAME_CHARS - 1); /* up to its NUL */
        out[NAME_CHARS - 1] = '\0';
    }
}

/* A table of names at `at` (from the SYMB block's start), `stride` words an entry. */
static void read_names(Reader *r, uint32_t symb, uint32_t at, uint32_t stride, Names *names)
{
    uint32_t count;
    if (at == 0) {
        names->count = 0;
        return;
    }
    count = read32(r, symb + at);
    if (!r->ok || count > 65536 || !alloc_names(r, names, count)) {
        r->ok = 0;
        return;
    }
    for (uint32_t i = 0; i < count && r->ok; ++i) {
        read_name(r, symb, read32(r, symb + at + 4 + 4 * stride * i), names->name[i]);
    }
}

static void free_names(Names *names)
{
    free(names->name);
    names->name = NULL;
    names->count = 0;
}

void khdays_sound_archive_close(KhdaysSoundArchive *archive)
{
    if (archive == NULL) {
        return;
    }
    /* none if the archive's symbols ended before them (open's error path) */
    for (int i = 0; archive->entry != NULL && i < archive->sequence_archive.count; ++i) {
        free_names(&archive->entry[i]);
    }
    free(archive->entry);
    free_names(&archive->sequence);
    free_names(&archive->sequence_archive);
    free_names(&archive->wave_archive);
    free_names(&archive->player);
    free_names(&archive->stream_player);
    free_names(&archive->stream);
    free(archive);
}

KhdaysSoundArchive *khdays_sound_archive_open(KhdaysArchiveRead read, void *context)
{
    Reader r = {read, context, 1};
    KhdaysSoundArchive *archive;
    char magic[4];
    uint32_t symb, table;
    if (read(context, 0, magic, 4) != 4 || memcmp(magic, "SDAT", 4) != 0) {
        fprintf(stderr, "sound: the sound archive is not an SDAT file\n");
        return NULL;
    }
    symb = read32(&r, 0x10);
    if (!r.ok || read(context, symb, magic, 4) != 4 || memcmp(magic, "SYMB", 4) != 0) {
        fprintf(stderr, "sound: the sound archive has no symbols (SYMB)\n");
        return NULL;
    }
    archive = calloc(1, sizeof(*archive));
    if (archive == NULL) {
        return NULL;
    }
    archive->read = read;
    archive->context = context;
    archive->info = read32(&r, 0x18);
    archive->fat = read32(&r, 0x20);
    read_names(&r, symb, read32(&r, symb + 8 + 4 * TABLE_SEQUENCE), 1, &archive->sequence);
    read_names(&r, symb, read32(&r, symb + 8 + 4 * TABLE_WAVE_ARCHIVE), 1, &archive->wave_archive);
    read_names(&r, symb, read32(&r, symb + 8 + 4 * TABLE_PLAYER), 1, &archive->player);
    read_names(&r, symb, read32(&r, symb + 8 + 4 * TABLE_STREAM_PLAYER), 1, &archive->stream_player);
    read_names(&r, symb, read32(&r, symb + 8 + 4 * TABLE_STREAM), 1, &archive->stream);
    table = read32(&r, symb + 8 + 4 * TABLE_SEQUENCE_ARCHIVE);
    read_names(&r, symb, table, 2, &archive->sequence_archive);
    if (r.ok && archive->sequence_archive.count > 0) {
        archive->entry = calloc((size_t)archive->sequence_archive.count, sizeof(Names));
        r.ok = archive->entry != NULL;
        for (int i = 0; r.ok && i < archive->sequence_archive.count; ++i) {
            read_names(&r, symb, read32(&r, symb + table + 8 + 8 * (uint32_t)i), 1, &archive->entry[i]);
        }
    }
    if (!r.ok) {
        fprintf(stderr, "sound: the sound archive's symbols end early\n");
        khdays_sound_archive_close(archive);
        return NULL;
    }
    return archive;
}

static const char *name_of(const Names *names, int n)
{
    return n >= 0 && n < names->count ? names->name[n] : "";
}

int khdays_sound_archive_find_entry(const KhdaysSoundArchive *archive, const char *sequence_archive, const char *entry,
                                    int *number, int *index)
{
    for (int n = 0; n < archive->sequence_archive.count; ++n) {
        if (strcmp(archive->sequence_archive.name[n], sequence_archive) != 0 || archive->entry == NULL) {
            continue;
        }
        for (int e = 0; e < archive->entry[n].count; ++e) {
            if (strcmp(archive->entry[n].name[e], entry) == 0) {
                *number = n;
                *index = e;
                return 1;
            }
        }
    }
    return 0;
}

const char *khdays_sound_archive_player_name(const KhdaysSoundArchive *archive, int player)
{
    return name_of(&archive->player, player);
}

const char *khdays_sound_archive_stream_player_name(const KhdaysSoundArchive *archive, int player)
{
    return name_of(&archive->stream_player, player);
}

/* A name, or the number in decimal for an unnamed one. */
static void name_or_number(char *out, int size, const char *name, int n)
{
    if (name[0] != '\0') {
        snprintf(out, (size_t)size, "%s", name);
    } else {
        snprintf(out, (size_t)size, "%d", n);
    }
}

int khdays_sound_archive_identifier(const KhdaysSoundArchive *archive, int kind, int number, int entry, char *out,
                                    int size)
{
    char a[NAME_CHARS + 16], b[NAME_CHARS + 16];
    switch (kind) {
    case KHDAYS_SOUND_SEQUENCE:
        name_or_number(a, sizeof(a), name_of(&archive->sequence, number), number);
        return snprintf(out, (size_t)size, "seq/%s", a) < size;
    case KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY:
        name_or_number(a, sizeof(a), name_of(&archive->sequence_archive, number), number);
        name_or_number(b, sizeof(b),
                       number >= 0 && number < archive->sequence_archive.count ? name_of(&archive->entry[number], entry)
                                                                               : "",
                       entry);
        return snprintf(out, (size_t)size, "seqarc/%s/%s", a, b) < size;
    case KHDAYS_SOUND_WAVE:
        name_or_number(a, sizeof(a), name_of(&archive->wave_archive, number), number);
        return snprintf(out, (size_t)size, "wave/%s/%d", a, entry) < size;
    case KHDAYS_SOUND_STREAM:
        name_or_number(a, sizeof(a), name_of(&archive->stream, number), number);
        return snprintf(out, (size_t)size, "strm/%s", a) < size;
    default: return 0;
    }
}

uint32_t khdays_sound_archive_read(const KhdaysSoundArchive *archive, uint32_t offset, void *destination, uint32_t size)
{
    return archive->read(archive->context, offset, destination, size);
}

/* The INFO table a kind's records are in. */
static int table_of(int kind)
{
    switch (kind) {
    case KHDAYS_SOUND_SEQUENCE: return TABLE_SEQUENCE;
    case KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY: return TABLE_SEQUENCE_ARCHIVE;
    case KHDAYS_SOUND_WAVE: return TABLE_WAVE_ARCHIVE;
    case KHDAYS_SOUND_STREAM: return TABLE_STREAM;
    case KHDAYS_SOUND_BANK: return TABLE_BANK;
    case KHDAYS_SOUND_PLAYER: return TABLE_PLAYER;
    default: return -1;
    }
}

int khdays_sound_archive_count(const KhdaysSoundArchive *archive, int kind)
{
    Reader r = {archive->read, archive->context, 1};
    const int table = table_of(kind);
    uint32_t at;
    if (table < 0) {
        return 0;
    }
    at = read32(&r, archive->info + 8 + 4 * (uint32_t)table);
    return at != 0 ? (int)read32(&r, archive->info + at) : 0;
}

uint32_t khdays_sound_archive_record(const KhdaysSoundArchive *archive, int kind, int n)
{
    Reader r = {archive->read, archive->context, 1};
    const int table = table_of(kind);
    uint32_t at, record;
    if (table < 0 || n < 0 || n >= khdays_sound_archive_count(archive, kind)) {
        return 0;
    }
    at = read32(&r, archive->info + 8 + 4 * (uint32_t)table);
    record = read32(&r, archive->info + at + 4 + 4 * (uint32_t)n);
    return record != 0 && r.ok ? archive->info + record : 0;
}

/* FAT: a block header, a count, then 16 bytes a file (NNSSndArcFileInfo:
 * offset, size, and two words the running game uses). */
int khdays_sound_archive_file(const KhdaysSoundArchive *archive, uint32_t file_id, uint32_t *offset, uint32_t *size)
{
    Reader r = {archive->read, archive->context, 1};
    const uint32_t count = read32(&r, archive->fat + 8);
    if (!r.ok || file_id >= count) {
        return 0;
    }
    *offset = read32(&r, archive->fat + 12 + 16 * file_id);
    *size = read32(&r, archive->fat + 12 + 16 * file_id + 4);
    return r.ok;
}
