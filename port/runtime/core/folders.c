/* folders.h. The rule is the setup screen's too (android/.../setup/GameFolder.kt). */
#include "folders.h"

#include <stdio.h>
#include <windows.h> /* host/posix/include/windows.h off Windows */

static char mods[1024] = "mods";
static char cutscenes[1024] = "data/cutscenes";

static int is_folder(const char *path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

void khdays_folders_choose(const char *folder)
{
    char path[1024];
    if (folder == NULL || folder[0] == '\0') {
        return;
    }
    if (!is_folder(folder)) {
        /* a memory card taken out, the access to it taken back: the app's own folder meanwhile */
        fprintf(stderr, "khdays-native: folders: %s cannot be read; the mods and HD videos are the app's own\n",
                folder);
        return;
    }
    snprintf(mods, sizeof(mods), "%s/mods", folder);
    /* the PC installer's folder as it was copied (data/cutscenes in it), or a cutscenes folder of its own */
    snprintf(path, sizeof(path), "%s/data/cutscenes", folder);
    snprintf(cutscenes, sizeof(cutscenes), is_folder(path) ? "%s/data/cutscenes" : "%s/cutscenes", folder);
    fprintf(stderr, "khdays-native: folders: mods from %s, HD videos from %s\n", mods, cutscenes);
}

const char *khdays_mods_folder(void)
{
    return mods;
}

const char *khdays_cutscenes_folder(void)
{
    return cutscenes;
}
