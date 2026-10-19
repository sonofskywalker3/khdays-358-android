/* Reading UTF-8 text one character at a time, for the game's 16-bit text. */
#ifndef KHDAYS_UTF8_H
#define KHDAYS_UTF8_H

/* The character at *p, moving *p past it; one outside 16 bits comes back as '?'. */
static inline unsigned short khdays_utf8_next(const unsigned char **p)
{
    unsigned c = *(*p)++;
    int more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    c &= more == 3 ? 0x07u : more == 2 ? 0x0fu : more == 1 ? 0x1fu : 0x7fu;
    for (; more > 0 && (**p & 0xc0) == 0x80; --more) {
        c = (c << 6) | (*(*p)++ & 0x3fu);
    }
    return c > 0xffffu ? (unsigned short)'?' : (unsigned short)c;
}

#endif
