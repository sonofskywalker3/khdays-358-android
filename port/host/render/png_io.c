/* Reading and writing PNG files (see png_io.h). stb's code is compiled here
 * with its warnings turned off, since it is not the port's code. */
#include "png_io.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STBI_ONLY_PNG
#define STBI_WINDOWS_UTF8 /* paths are UTF-8, as SDL gives them */
#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

uint8_t *khdays_png_read(const char *path, int *width, int *height, const char **error)
{
    int channels = 0;
    uint8_t *pixels = stbi_load(path, width, height, &channels, 4);
    if (pixels == NULL && error != NULL) {
        *error = stbi_failure_reason();
    }
    return pixels;
}

uint8_t *khdays_png_read_memory(const uint8_t *png, int size, int *width, int *height, const char **error)
{
    int channels = 0;
    uint8_t *pixels = stbi_load_from_memory(png, size, width, height, &channels, 4);
    if (pixels == NULL && error != NULL) {
        *error = stbi_failure_reason();
    }
    return pixels;
}

void khdays_png_free(uint8_t *pixels)
{
    stbi_image_free(pixels);
}

const char *khdays_png_write(const char *path, const uint8_t *rgba, int width, int height)
{
    return stbi_write_png(path, width, height, 4, rgba, width * 4) ? NULL : "the file could not be written";
}

/* --- Indexed PNGs: the 2D's tile sheets keep the game's palettes ------------------------
 *
 * stb_image always turns indexed PNGs into RGBA, so these are read and
 * written here (stb's zlib does the compression). */

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* PNG's Paeth predictor: whichever of left (a), up (b) and up-left (c) is
 * closest to a + b - c. */
static int paeth(int a, int b, int c)
{
    const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/* Undoes a row's PNG filter in place (at the depths read here, one byte is
 * one unit). `up` is the row above, already undone, or NULL for the first.
 * Returns 0 for an unknown filter. */
static int unfilter(uint8_t *row, const uint8_t *up, size_t stride, int filter)
{
    if (filter > 4) {
        return 0;
    }
    for (size_t i = 0; i < stride; ++i) {
        const int a = i > 0 ? row[i - 1] : 0;
        const int b = up != NULL ? up[i] : 0;
        const int c = up != NULL && i > 0 ? up[i - 1] : 0;
        if (filter == 1) {
            row[i] = (uint8_t)(row[i] + a);
        } else if (filter == 2) {
            row[i] = (uint8_t)(row[i] + b);
        } else if (filter == 3) {
            row[i] = (uint8_t)(row[i] + ((a + b) >> 1));
        } else if (filter == 4) {
            row[i] = (uint8_t)(row[i] + paeth(a, b, c));
        }
    }
    return 1;
}

uint8_t *khdays_png_read_indexed(const char *path, int *width, int *height, uint8_t palette[256 * 4],
                                 const char **error)
{
    size_t size = 0, idat_size = 0;
    uint8_t *file = (uint8_t *)SDL_LoadFile(path, &size);
    uint8_t *idat = NULL, *raw = NULL, *out = NULL;
    uint32_t w = 0, h = 0, depth = 0, type = 0, interlace = 0;
    int raw_len = 0;
    const char *why = NULL;
    if (palette != NULL) { /* black and opaque until the file says otherwise */
        memset(palette, 0, 256 * 4);
        for (int i = 0; i < 256; ++i)
            palette[i * 4 + 3] = 255;
    }
    if (file == NULL) {
        why = "the file could not be read";
        goto done;
    }
    if (size < 8 || memcmp(file, "\x89PNG\r\n\x1a\n", 8) != 0) {
        why = "not a PNG";
        goto done;
    }
    /* the chunks: the header, the palette, its transparency and the image data */
    for (size_t at = 8; at + 12 <= size;) {
        const uint32_t len = be32(file + at);
        const uint8_t *tag = file + at + 4, *data = file + at + 8;
        if (len > size - at - 12) {
            why = "a chunk runs past the file's end";
            goto done;
        }
        if (memcmp(tag, "IHDR", 4) == 0 && len >= 13) {
            w = be32(data);
            h = be32(data + 4);
            depth = data[8];
            type = data[9];
            interlace = data[12];
        } else if (memcmp(tag, "PLTE", 4) == 0 && palette != NULL) {
            for (uint32_t i = 0; i < len / 3 && i < 256; ++i) {
                palette[i * 4] = data[i * 3];
                palette[i * 4 + 1] = data[i * 3 + 1];
                palette[i * 4 + 2] = data[i * 3 + 2];
            }
        } else if (memcmp(tag, "tRNS", 4) == 0 && palette != NULL) {
            for (uint32_t i = 0; i < len && i < 256; ++i)
                palette[i * 4 + 3] = data[i];
        } else if (memcmp(tag, "IDAT", 4) == 0) {
            uint8_t *grown = (uint8_t *)realloc(idat, idat_size + len);
            if (grown == NULL) {
                why = "no memory";
                goto done;
            }
            idat = grown;
            memcpy(idat + idat_size, data, len);
            idat_size += len;
        } else if (memcmp(tag, "IEND", 4) == 0) {
            break;
        }
        at += 12 + (size_t)len;
    }
    if (type != 3) {
        why = "not indexed";
        goto done;
    }
    /* (the size check keeps the image's byte count within the int stb's zlib takes) */
    if (w == 0 || h == 0 || w > 16384 || h > 16384 || (depth != 1 && depth != 2 && depth != 4 && depth != 8) ||
        interlace != 0 || idat == NULL) {
        why = "an indexed PNG this reader does not take (interlaced, or no image data)";
        goto done;
    }
    {
        const size_t stride = ((size_t)w * depth + 7) / 8;
        raw = (uint8_t *)stbi_zlib_decode_malloc_guesssize_headerflag((const char *)idat, (int)idat_size,
                                                                      (int)((stride + 1) * h), &raw_len, 1);
        if (raw == NULL || (size_t)raw_len < (stride + 1) * h) {
            why = "its image data could not be decompressed";
            goto done;
        }
        out = (uint8_t *)malloc((size_t)w * h);
        if (out == NULL) {
            why = "no memory";
            goto done;
        }
        /* each row: its filter undone, then its pixels unpacked to a byte each */
        for (uint32_t y = 0; y < h; ++y) {
            uint8_t *row = raw + y * (stride + 1) + 1; /* after the row's filter byte */
            const uint8_t *up = y > 0 ? raw + (y - 1) * (stride + 1) + 1 : NULL;
            if (!unfilter(row, up, stride, row[-1])) {
                why = "a row with an unknown filter";
                free(out);
                out = NULL;
                goto done;
            }
            for (uint32_t x = 0; x < w; ++x) {
                const size_t bit = (size_t)x * depth;
                out[(size_t)y * w + x] = (uint8_t)((row[bit / 8] >> (8 - depth - (bit % 8))) & ((1u << depth) - 1u));
            }
        }
        *width = (int)w;
        *height = (int)h;
    }
done:
    if (out == NULL && error != NULL)
        *error = why;
    free(raw);
    free(idat);
    SDL_free(file);
    return out;
}

static uint32_t crc_table[256];

/* The CRC-32 PNG puts after each chunk (the table is filled on first use). */
static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    if (crc_table[1] == 0) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t v = i;
            for (int k = 0; k < 8; ++k)
                v = (v & 1u) ? 0xedb88320u ^ (v >> 1) : v >> 1;
            crc_table[i] = v;
        }
    }
    for (size_t i = 0; i < n; ++i)
        c = crc_table[(c ^ p[i]) & 0xffu] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

/* Writes one chunk at `out` (which has room for 12 + len bytes): its length,
 * tag, data and CRC. Returns the bytes written. */
static size_t put_chunk(uint8_t *out, const char *tag, const uint8_t *data, uint32_t len)
{
    uint32_t crc;
    out[0] = (uint8_t)(len >> 24);
    out[1] = (uint8_t)(len >> 16);
    out[2] = (uint8_t)(len >> 8);
    out[3] = (uint8_t)len;
    memcpy(out + 4, tag, 4);
    if (len != 0)
        memcpy(out + 8, data, len);
    crc = crc32_of(out + 4, len + 4u);
    out[8 + len] = (uint8_t)(crc >> 24);
    out[9 + len] = (uint8_t)(crc >> 16);
    out[10 + len] = (uint8_t)(crc >> 8);
    out[11 + len] = (uint8_t)crc;
    return 12u + len;
}

const char *khdays_png_write_indexed(const char *path, const uint8_t *indices, int width, int height,
                                     const uint8_t *palette, int colours, int transparent0)
{
    const size_t stride = (size_t)width + 1;
    uint8_t *rows = (uint8_t *)malloc(stride * (size_t)height), *zipped = NULL, *file = NULL;
    uint8_t header[13];
    const uint8_t clear0 = 0;
    int zipped_len = 0;
    size_t at = 0;
    const char *why = NULL;
    if (colours < 1)
        colours = 1;
    if (colours > 256)
        colours = 256;
    if (rows == NULL)
        return "no memory";
    for (int y = 0; y < height; ++y) {
        rows[(size_t)y * stride] = 0; /* no filter */
        memcpy(rows + (size_t)y * stride + 1, indices + (size_t)y * (size_t)width, (size_t)width);
    }
    zipped = stbi_zlib_compress(rows, (int)(stride * (size_t)height), &zipped_len, 8);
    free(rows);
    if (zipped == NULL)
        return "the image could not be compressed";
    /* the signature, then the IHDR, PLTE, tRNS, IDAT and IEND chunks (each 12 bytes plus its data) */
    file = (uint8_t *)malloc(8 + 25 + 12 + (size_t)colours * 3 + 13 + 12 + (size_t)zipped_len + 12);
    if (file == NULL) {
        STBIW_FREE(zipped);
        return "no memory";
    }
    memcpy(file, "\x89PNG\r\n\x1a\n", 8);
    at = 8;
    for (int i = 0; i < 4; ++i) {
        header[i] = (uint8_t)((uint32_t)width >> (24 - 8 * i));
        header[4 + i] = (uint8_t)((uint32_t)height >> (24 - 8 * i));
    }
    header[8] = 8; /* bits per pixel */
    header[9] = 3; /* indexed */
    header[10] = header[11] = header[12] = 0;
    at += put_chunk(file + at, "IHDR", header, 13);
    at += put_chunk(file + at, "PLTE", palette, (uint32_t)colours * 3u);
    if (transparent0)
        at += put_chunk(file + at, "tRNS", &clear0, 1);
    at += put_chunk(file + at, "IDAT", zipped, (uint32_t)zipped_len);
    at += put_chunk(file + at, "IEND", NULL, 0);
    STBIW_FREE(zipped);
    if (!SDL_SaveFile(path, file, at))
        why = "the file could not be written";
    free(file);
    return why;
}
