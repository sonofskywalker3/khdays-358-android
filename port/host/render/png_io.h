/* Reading and writing PNG files (png_io.c, using stb_image and
 * stb_image_write): the HD replacements players put in their mods, and the
 * dumps that show them what the game draws. */
#ifndef KHDAYS_PNG_IO_H
#define KHDAYS_PNG_IO_H

#include <stdint.h>

/* Reads a PNG as 8-bit RGBA (width * height * 4 bytes, rows top to bottom).
 * Returns the pixels, to be freed with khdays_png_free, or NULL with the
 * reason in *error. */
uint8_t *khdays_png_read(const char *path, int *width, int *height, const char **error);

/* The same, from `size` bytes of PNG in memory. */
uint8_t *khdays_png_read_memory(const uint8_t *png, int size, int *width, int *height, const char **error);

void khdays_png_free(uint8_t *pixels);

/* Writes 8-bit RGBA pixels as a PNG. Returns NULL on success, or the reason
 * it failed. */
const char *khdays_png_write(const char *path, const uint8_t *rgba, int width, int height);

/* Reads an indexed PNG (colour type 3, any bit depth, not interlaced) as one palette index per pixel,
 * its palette into `palette` as RGBA (256 entries, black past the file's; NULL if not needed).
 * Returns the indices (khdays_png_free), or NULL with the reason in *error ("not indexed"). */
uint8_t *khdays_png_read_indexed(const char *path, int *width, int *height, uint8_t palette[256 * 4],
                                 const char **error);

/* Writes palette indices (one per pixel) as an indexed PNG whose palette is
 * the first `colours` RGB entries of `palette` (3 bytes each); index 0 is
 * transparent when `transparent0`. Returns NULL on success, or the reason it
 * failed. */
const char *khdays_png_write_indexed(const char *path, const uint8_t *indices, int width, int height,
                                     const uint8_t *palette, int colours, int transparent0);

#endif
