/* The sound archive the game's sound comes from (SDAT, snd/sound_data.sdat): what the port needs of it
 * besides what the game's NitroSystem code reads -- its players' names, which say what they play
 * (categories.h), and its sounds', which name the replacements (replace.h). Plain C: the archive's
 * bytes come through a read callback. */
#ifndef KHDAYS_SOUND_ARCHIVE_H
#define KHDAYS_SOUND_ARCHIVE_H

#include <stdint.h>

/* `size` bytes of the archive at `offset` from its start; returns how many. */
typedef uint32_t (*KhdaysArchiveRead)(void *context, uint32_t offset, void *destination, uint32_t size);

typedef struct KhdaysSoundArchive KhdaysSoundArchive;

/* NULL, and a message, if it is not a sound archive. */
KhdaysSoundArchive *khdays_sound_archive_open(KhdaysArchiveRead read, void *context);
void khdays_sound_archive_close(KhdaysSoundArchive *archive);

/* A sequence player's name (by the archive's player number, NNSSndPlayer's) and a stream
 * player's; "" if it has none. */
const char *khdays_sound_archive_player_name(const KhdaysSoundArchive *archive, int player);
const char *khdays_sound_archive_stream_player_name(const KhdaysSoundArchive *archive, int player);

/* A sequence archive's entry by its names (as in "seqarc/<archive>/<entry>"): its archive's number and
 * its index there. Returns 0 when there is none of those names. */
int khdays_sound_archive_find_entry(const KhdaysSoundArchive *archive, const char *sequence_archive, const char *entry,
                                    int *number, int *index);

/* A sound's identifier, the path under
 * a mod's sounds/ without the extension: "seq/<name>", "seqarc/<archive>/
 * <entry>", "wave/<archive>/<index>", "strm/<name>" -- an unnamed one by
 * its number in decimal. 0 if it does not fit in `out`. */
enum {
    KHDAYS_SOUND_SEQUENCE,
    KHDAYS_SOUND_SEQUENCE_ARCHIVE_ENTRY, /* number: the archive; entry: its entry */
    KHDAYS_SOUND_WAVE,                   /* number: the wave archive; entry: the wave */
    KHDAYS_SOUND_STREAM
};
int khdays_sound_archive_identifier(const KhdaysSoundArchive *archive, int kind, int number, int entry, char *out,
                                    int size);

/* The records (INFO): how many there are of a kind -- one above (a
 * sequence archive's entries are in its file), or a bank or a player --
 * and where record `n` is in the archive (0: none). */
enum { KHDAYS_SOUND_BANK = 16, KHDAYS_SOUND_PLAYER };
int khdays_sound_archive_count(const KhdaysSoundArchive *archive, int kind);
uint32_t khdays_sound_archive_record(const KhdaysSoundArchive *archive, int kind, int n);

/* Where file `file_id` (a record's first word, NNSSndArc*Info.fileId) is in
 * the archive, from its FAT. 0 if there is no such file. */
int khdays_sound_archive_file(const KhdaysSoundArchive *archive, uint32_t file_id, uint32_t *offset, uint32_t *size);

/* The archive's bytes, through its read callback. */
uint32_t khdays_sound_archive_read(const KhdaysSoundArchive *archive, uint32_t offset, void *destination,
                                   uint32_t size);

#endif
