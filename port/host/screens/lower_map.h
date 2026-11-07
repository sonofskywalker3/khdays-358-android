/* lower_map.h: a layout's map entry (lower_layout.h KhdaysLowerLayout map), for the files that read it. */
#ifndef KHDAYS_LOWER_MAP_H
#define KHDAYS_LOWER_MAP_H

#define COL_BITS 12 /* a map entry: the picture's row above these bits, its column in them */
#define COL_MASK ((1 << COL_BITS) - 1)
#define COPY_BIT (1 << 30) /* a map entry of a repeated row's copy: drawn from the copy table where it says */

#endif
