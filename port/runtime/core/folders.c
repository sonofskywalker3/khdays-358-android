/* folders.h. The rule is the setup screen's too (android/.../setup/GameFolder.kt). */
#include "folders.h"

#include <stdio.h>
#include <windows.h> /* host/posix/include/windows.h off Windows */
#if defined(__ANDROID__)
#include <unistd.h>
#endif

static char mods[1024] = "mods";
static char cutscenes[1024] = "data/cutscenes";

static int is_folder(const char *path)
{
    const DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* A chosen folder that can be read; NULL for none (or one that cannot be read, said). */
static const char *readable(const char *folder, const char *what)
{
    if (folder == NULL || folder[0] == '\0') {
        return NULL;
    }
    if (!is_folder(folder)) {
        /* a memory card taken out, the access to it taken back: the app's own folder meanwhile */
        fprintf(stderr, "khdays-native: folders: %s cannot be read; the %s are the app's own\n", folder, what);
        return NULL;
    }
    return folder;
}

void khdays_folders_choose_mods(const char *folder)
{
    if (readable(folder, "mods") == NULL) {
        return;
    }
    snprintf(mods, sizeof(mods), "%s", folder);
    fprintf(stderr, "khdays-native: folders: mods from %s\n", mods);
}

void khdays_folders_choose_hd(const char *folder)
{
    char path[1024];
#if defined(__ANDROID__)
    {
        /* the app's own named whole: SDL opens a relative path from the app's private folder, not the
         * working folder (the theater's chapters, their lengths and music were never found there) */
        char here[768];
        if (getcwd(here, sizeof(here)) != NULL) {
            snprintf(cutscenes, sizeof(cutscenes), "%s/data/cutscenes", here);
        }
    }
#endif
    if (readable(folder, "HD videos") == NULL) {
        return;
    }
    /* the PC installer's folder as it was copied (data/cutscenes in it), or a cutscenes folder of its own */
    snprintf(path, sizeof(path), "%s/data/cutscenes", folder);
    snprintf(cutscenes, sizeof(cutscenes), is_folder(path) ? "%s/data/cutscenes" : "%s/cutscenes", folder);
    fprintf(stderr, "khdays-native: folders: HD videos from %s\n", cutscenes);
}

const char *khdays_mods_folder(void)
{
    return mods;
}

const char *khdays_cutscenes_folder(void)
{
    return cutscenes;
}
