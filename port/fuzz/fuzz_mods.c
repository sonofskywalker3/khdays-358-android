/* Fuzz test of what a mod's folder gives the port before any script runs: its mod.ini and the load
 * order file (runtime/scripts/manifest.h, order.h). The first byte chooses which; the rest is the
 * file's text. */
#include "../runtime/scripts/folders.h"
#include "../runtime/scripts/manifest.h"
#include "../runtime/scripts/order.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The fingerprint's file reading is not under test: no files. */
int khdays_folders_files(const char *path, void (*each)(const char *relative, void *context), void *context)
{
    (void)path;
    (void)each;
    (void)context;
    return 0;
}

char *khdays_folders_read(const char *path, size_t *size)
{
    (void)path;
    (void)size;
    return NULL;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *text;
    if (size < 1) {
        return 0;
    }
    text = malloc(size);
    if (text == NULL) {
        return 0;
    }
    memcpy(text, data + 1, size - 1);
    text[size - 1] = 0;
    if (data[0] & 1) {
        KhdaysManifest m;
        char reason[200];
        if (khdays_manifest_parse(text, size - 1, &m, reason, sizeof(reason))) {
            khdays_manifest_free(&m);
        }
    } else {
        /* order.ini's text, merged with folders made from its own lines, moved about, written */
        KhdaysOrder o;
        const char *found[3] = {"Alpha", "Beta", text};
        char *out;
        khdays_order_parse(&o, text);
        khdays_order_merge(&o, found, 3);
        for (int i = 0; i < o.count; ++i) {
            khdays_order_move(&o, i, (data[0] & 2) ? 1 : -1);
        }
        out = khdays_order_text(&o);
        free(out);
        khdays_order_free(&o);
    }
    free(text);
    return 0;
}
