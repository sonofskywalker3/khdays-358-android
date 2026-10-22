/* SHA-256 (FIPS 180-4): the mods' fingerprints (runtime/scripts), the update's installer checked
 * (host/app/updates.c). */
#ifndef KHDAYS_SHA256_H
#define KHDAYS_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct KhdaysSha256 {
    uint32_t state[8];
    uint64_t length; /* bytes taken so far */
    unsigned char block[64];
    unsigned int used; /* bytes waiting in `block` */
} KhdaysSha256;

void khdays_sha256_begin(KhdaysSha256 *s);
void khdays_sha256_add(KhdaysSha256 *s, const void *data, size_t size);
void khdays_sha256_end(KhdaysSha256 *s, unsigned char digest[32]);

/* The digest of one buffer. */
void khdays_sha256(const void *data, size_t size, unsigned char digest[32]);

#endif
