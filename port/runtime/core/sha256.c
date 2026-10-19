/* SHA-256 (sha256.h), as FIPS 180-4 describes it. */
#include "sha256.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void compress(KhdaysSha256 *s, const unsigned char *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, h;
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = s->state[0], b = s->state[1], c = s->state[2], d = s->state[3];
    e = s->state[4], f = s->state[5], g = s->state[6], h = s->state[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    s->state[0] += a, s->state[1] += b, s->state[2] += c, s->state[3] += d;
    s->state[4] += e, s->state[5] += f, s->state[6] += g, s->state[7] += h;
}

void khdays_sha256_begin(KhdaysSha256 *s)
{
    static const uint32_t start[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->state, start, sizeof(start));
    s->length = 0;
    s->used = 0;
}

void khdays_sha256_add(KhdaysSha256 *s, const void *data, size_t size)
{
    const unsigned char *p = data;
    s->length += size;
    while (size > 0) {
        size_t take = 64 - s->used;
        if (take > size) {
            take = size;
        }
        memcpy(s->block + s->used, p, take);
        s->used += (unsigned int)take;
        p += take;
        size -= take;
        if (s->used == 64) {
            compress(s, s->block);
            s->used = 0;
        }
    }
}

void khdays_sha256_end(KhdaysSha256 *s, unsigned char digest[32])
{
    const uint64_t bits = s->length * 8;
    unsigned char tail[8];
    s->block[s->used++] = 0x80;
    if (s->used > 56) {
        memset(s->block + s->used, 0, 64 - s->used);
        compress(s, s->block);
        s->used = 0;
    }
    memset(s->block + s->used, 0, 56 - s->used);
    for (int i = 0; i < 8; ++i) {
        tail[i] = (unsigned char)(bits >> (56 - i * 8));
    }
    memcpy(s->block + 56, tail, 8);
    compress(s, s->block);
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = (unsigned char)(s->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(s->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(s->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)s->state[i];
    }
}

void khdays_sha256(const void *data, size_t size, unsigned char digest[32])
{
    KhdaysSha256 s;
    khdays_sha256_begin(&s);
    khdays_sha256_add(&s, data, size);
    khdays_sha256_end(&s, digest);
}
