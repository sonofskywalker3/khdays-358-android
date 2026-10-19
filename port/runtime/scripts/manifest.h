/* A scripted mod's manifest, mod.ini: `key = value` lines, `#` comments, UTF-8. */
#ifndef KHDAYS_SCRIPTS_MANIFEST_H
#define KHDAYS_SCRIPTS_MANIFEST_H

#include <stddef.h>

enum KhdaysModKind { KHDAYS_MOD_GAMEPLAY, KHDAYS_MOD_VISUAL };

/* The interface version this port gives the mods. */
#define KHDAYS_MODS_API 1

typedef struct KhdaysManifest {
    char *id, *name, *version, *author, *description;
    char *main; /* under scripts/; scripts/main.lua when not given */
    int kind;   /* KhdaysModKind */
    int api;
} KhdaysManifest;

/* Reads mod.ini's text into `out` (its strings to khdays_manifest_free). Returns 1, or 0 with the
 * reason the mod is refused in `reason`: "mod.ini: <key> missing or not valid". The api version and
 * the main script's presence are the loader's to check. */
int khdays_manifest_parse(const char *text, size_t size, KhdaysManifest *out, char *reason, size_t reason_size);

void khdays_manifest_free(KhdaysManifest *m);

/* A mod's fingerprint: SHA-256 over mod.ini and every file under scripts/, each as its path from the
 * mod's folder, a 0 byte and its bytes, in byte order of the paths. Returns 0 when one of them cannot
 * be read. */
int khdays_manifest_fingerprint(const char *folder, unsigned char digest[32]);

#endif
