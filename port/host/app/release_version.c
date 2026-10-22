/* host/app/release_version.h. */
#include "release_version.h"

#include <ctype.h>
#include <string.h>

/* Reads a version: its three numbers (missing ones 0) and whether it is a pre-release. 0 when `s`
 * is no version. */
static int parse(const char *s, unsigned long n[3], int *pre)
{
    n[0] = n[1] = n[2] = 0;
    *pre = 0;
    if (s == NULL) {
        return 0;
    }
    if (*s == 'v' || *s == 'V') {
        ++s;
    }
    for (int i = 0; i < 3; ++i) {
        unsigned long v = 0;
        if (!isdigit((unsigned char)*s)) {
            return 0;
        }
        while (isdigit((unsigned char)*s)) {
            v = v * 10 + (unsigned long)(*s - '0');
            ++s;
        }
        n[i] = v;
        if (*s != '.' || i == 2) {
            break;
        }
        ++s;
    }
    if (*s == '-') {
        *pre = s[1] != '\0';
        return *pre;
    }
    return *s == '\0' || *s == '+';
}

int khdays_version_valid(const char *s)
{
    unsigned long n[3];
    int pre;
    return parse(s, n, &pre);
}

int khdays_version_compare(const char *a, const char *b)
{
    unsigned long x[3], y[3];
    int pa, pb;
    const int va = parse(a, x, &pa), vb = parse(b, y, &pb);
    if (!va || !vb) {
        return va - vb;
    }
    for (int i = 0; i < 3; ++i) {
        if (x[i] != y[i]) {
            return x[i] < y[i] ? -1 : 1;
        }
    }
    return pb - pa;
}

static const char *skip_space(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

int khdays_release_tag(const char *json, char *tag, size_t size)
{
    static const char key[] = "\"tag_name\"";
    const char *p = json != NULL ? strstr(json, key) : NULL;
    size_t length = 0;
    if (p == NULL || tag == NULL || size == 0) {
        return 0;
    }
    p = skip_space(p + sizeof(key) - 1);
    if (*p != ':') {
        return 0;
    }
    p = skip_space(p + 1);
    if (*p != '"') {
        return 0;
    }
    ++p;
    /* a version's characters only: the tag goes into the player's text and a web address */
    while (p[length] != '"') {
        const char c = p[length];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_' || c == '+')) {
            return 0;
        }
        ++length;
    }
    if (length == 0 || length + 1 > size) {
        return 0;
    }
    memcpy(tag, p, length);
    tag[length] = '\0';
    return khdays_version_valid(tag);
}

/* ---- The installer among the release's files ---- */

#define INSTALLER_PREFIX "KH358-2-Days-Port-Setup-"
#define DIGEST_PREFIX "sha256:"

/* Just past the JSON string at `p` (its opening quote); NULL when it never closes. */
static const char *string_end(const char *p)
{
    for (++p; *p != '\0'; ++p) {
        if (*p == '\\') {
            if (p[1] == '\0') {
                return NULL;
            }
            ++p;
        } else if (*p == '"') {
            return p + 1;
        }
    }
    return NULL;
}

/* Just past the JSON value at `p` (an object, an array, a string, a number or a word); NULL when it
 * is cut short or not JSON. `depth` stops a nesting with no end. */
static const char *value_end(const char *p, int depth)
{
    p = skip_space(p);
    if (depth > 64) {
        return NULL;
    }
    if (*p == '"') {
        return string_end(p);
    }
    if (*p == '{' || *p == '[') {
        const char close = *p == '{' ? '}' : ']';
        p = skip_space(p + 1);
        if (*p == close) {
            return p + 1;
        }
        for (;;) {
            if (close == '}') {
                if (*p != '"' || (p = string_end(p)) == NULL || *(p = skip_space(p)) != ':') {
                    return NULL;
                }
                ++p;
            }
            if ((p = value_end(p, depth + 1)) == NULL) {
                return NULL;
            }
            p = skip_space(p);
            if (*p != ',') {
                return *p == close ? p + 1 : NULL;
            }
            p = skip_space(p + 1);
        }
    }
    {
        const char *start = p;
        while (isalnum((unsigned char)*p) || *p == '-' || *p == '+' || *p == '.') {
            ++p;
        }
        return p > start ? p : NULL;
    }
}

/* The JSON string at `p` into `to` (`size` bytes with its NUL), when it has no escapes and fits. */
static int copy_string(const char *p, const char *end, char *to, size_t size)
{
    const size_t n = (size_t)(end - p) - 2;
    if (*p != '"' || n + 1 > size || memchr(p + 1, '\\', n) != NULL) {
        return 0;
    }
    memcpy(to, p + 1, n);
    to[n] = '\0';
    return 1;
}

/* One asset's object (from its '{' to just past its '}'): its fields into `f`; 1 when it is the
 * installer with all of them. */
static int read_asset(const char *p, KhdaysReleaseFile *f)
{
    char digest[80] = "";
    memset(f, 0, sizeof(*f));
    f->size = -1;
    p = skip_space(p + 1);
    while (*p == '"') {
        const char *key = p, *key_end = string_end(p), *value, *end;
        if (key_end == NULL || *(value = skip_space(key_end)) != ':') {
            return 0;
        }
        value = skip_space(value + 1);
        if ((end = value_end(value, 1)) == NULL) {
            return 0;
        }
#define KEY_IS(k) ((size_t)(key_end - key) == sizeof(k) + 1 && memcmp(key + 1, k, sizeof(k) - 1) == 0)
        if (KEY_IS("name")) {
            copy_string(value, end, f->name, sizeof(f->name));
        } else if (KEY_IS("browser_download_url")) {
            copy_string(value, end, f->url, sizeof(f->url));
        } else if (KEY_IS("digest")) {
            copy_string(value, end, digest, sizeof(digest));
        } else if (KEY_IS("size") && isdigit((unsigned char)*value)) {
            f->size = 0;
            for (const char *d = value; d < end && isdigit((unsigned char)*d); ++d) {
                f->size = f->size * 10 + (*d - '0');
            }
        }
#undef KEY_IS
        p = skip_space(end);
        if (*p == ',') {
            p = skip_space(p + 1);
        }
    }
    {
        const size_t name = strlen(f->name), prefix = sizeof(INSTALLER_PREFIX) - 1, hex = 64;
        if (name <= prefix + 4 || strncmp(f->name, INSTALLER_PREFIX, prefix) != 0 ||
            strcmp(f->name + name - 4, ".exe") != 0 || f->url[0] == '\0' || f->size <= 0 ||
            strncmp(digest, DIGEST_PREFIX, sizeof(DIGEST_PREFIX) - 1) != 0 ||
            strlen(digest) != sizeof(DIGEST_PREFIX) - 1 + hex) {
            return 0;
        }
        for (size_t i = 0; i < hex; ++i) {
            const char c = digest[sizeof(DIGEST_PREFIX) - 1 + i];
            if (!isxdigit((unsigned char)c)) {
                return 0;
            }
            f->sha256[i] = (char)tolower((unsigned char)c);
        }
        f->sha256[hex] = '\0';
    }
    return 1;
}

int khdays_release_installer(const char *json, KhdaysReleaseFile *out)
{
    const char *p = json != NULL ? skip_space(json) : NULL;
    if (p == NULL || *p != '{' || out == NULL) {
        return 0;
    }
    /* the top object's members, to its "assets" array */
    p = skip_space(p + 1);
    while (*p == '"') {
        const char *key_end = string_end(p), *value, *end;
        const int assets = key_end != NULL && key_end - p == 8 && memcmp(p, "\"assets\"", 8) == 0;
        if (key_end == NULL || *(value = skip_space(key_end)) != ':') {
            return 0;
        }
        value = skip_space(value + 1);
        if ((end = value_end(value, 0)) == NULL) {
            return 0;
        }
        if (assets && *value == '[') {
            const char *item = skip_space(value + 1);
            while (*item == '{') {
                const char *item_end = value_end(item, 1);
                if (item_end == NULL) {
                    return 0;
                }
                if (read_asset(item, out)) {
                    return 1;
                }
                item = skip_space(item_end);
                if (*item == ',') {
                    item = skip_space(item + 1);
                }
            }
            return 0;
        }
        p = skip_space(end);
        if (*p == ',') {
            p = skip_space(p + 1);
        }
    }
    return 0;
}
