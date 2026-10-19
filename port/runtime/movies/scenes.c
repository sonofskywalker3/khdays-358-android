/* The scene map's files and the recognition of the running scene (scenes.h).
 *
 * The event archives (the ROM's files ev/EV_<world>.p2) hold the scenes' scripts, one entry each; a
 * script begins with a word, its data's offset, then its steps. The event's machine runs a scene's
 * steps from where it read them, unchanged, so the running script is its ROM entry byte for byte
 * (RUNTIME_MAP 21): it is found among the entries of the same first word. */
#include "scenes.h"
#include "../core/rom.h"
#include "../core/session_log.h"
#include "../core/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char khdays_scenes_port[];
extern const unsigned int khdays_scenes_port_size;

/* --- The map -------------------------------------------------------------------------------- */

static KhdaysSceneMap map;
static int map_read;

static void report(const char *file, int line, const char *why)
{
    fprintf(stderr, "scenes: %s, line %d skipped: %s\n", file, line, why);
    khdays_log("scenes: %s, line %d skipped: %s", file, line, why);
}

static char *file_text(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    char *text = NULL;
    long length;
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        text = (char *)malloc((size_t)length + 1);
        if (text != NULL && fread(text, 1, (size_t)length, f) != (size_t)length) {
            free(text);
            text = NULL;
        }
        *size = (size_t)length;
    }
    fclose(f);
    return text;
}

const KhdaysSceneMap *khdays_scenes_map(void)
{
    if (!map_read) {
        char path[1024];
        map_read = 1;
        khdays_scene_map_read(&map, (const char *)khdays_scenes_port, khdays_scenes_port_size,
                              "port/movies/hd_scenes.txt", report);
        if (khdays_mods_find("cutscenes/scenes.txt", path, (int)sizeof(path))) {
            size_t size = 0;
            char *text = file_text(path, &size);
            if (text != NULL) {
                khdays_scene_map_read(&map, text, size, path, report);
                free(text);
            } else {
                report(path, 0, "cannot be read");
            }
        }
        khdays_log("scenes: %d scenes in the map", map.count);
    }
    return &map;
}

/* --- The event archives' entries ------------------------------------------------------------- */

typedef struct Script {
    char key[48];
    uint32_t first; /* its first word */
    uint8_t *bytes;
    uint32_t size;
} Script;

static Script *scripts;
static int script_count, script_capacity;
static int indexed;

static void add_script(const char *key, uint8_t *bytes, uint32_t size)
{
    if (script_count == script_capacity) {
        const int capacity = script_capacity ? script_capacity * 2 : 1024;
        Script *grown = (Script *)realloc(scripts, (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            free(bytes);
            return;
        }
        scripts = grown;
        script_capacity = capacity;
    }
    snprintf(scripts[script_count].key, sizeof(scripts[script_count].key), "%s", key);
    memcpy(&scripts[script_count].first, bytes, 4);
    scripts[script_count].bytes = bytes;
    scripts[script_count].size = size;
    ++script_count;
}

/* Every entry of every ev/EV_<world>.p2 the ROM's file list names. */
static void index_archives(void)
{
    KhdaysRomReader *reader = khdays_rom_reader_new();
    const int files = khdays_rom_file_count();
    indexed = 1;
    for (int id = 0; id < files; ++id) {
        char path[256], world[32];
        u32 offset, bytes;
        const char *name;
        size_t length;
        if (!khdays_rom_file_info(id, path, (int)sizeof(path), &offset, &bytes)) {
            continue;
        }
        name = path[0] == '/' ? path + 1 : path;
        length = strlen(name);
        if (strncmp(name, "ev/EV_", 6) != 0 || length < 10 || strcmp(name + length - 3, ".p2") != 0 ||
            length - 9 >= sizeof(world)) {
            continue;
        }
        memcpy(world, name + 6, length - 9);
        world[length - 9] = '\0';
        for (int entry = 0;; ++entry) {
            u8 *data = NULL;
            u32 size = 0;
            char key[48];
            if (!khdays_rom_file_data(reader, name, entry, &data, &size)) {
                break;
            }
            if (size < 8) {
                free(data);
                continue;
            }
            snprintf(key, sizeof(key), "EV_%s#%d", world, entry);
            add_script(key, data, size);
        }
    }
    khdays_log("scenes: %d scripts of the event archives indexed", script_count);
}

int khdays_scenes_recognise(uint32_t commands, char *key, int size)
{
    const uint32_t start = commands - 4u;
    uint32_t first;
    if (!indexed) {
        index_archives();
    }
    if (!khdays_game_ram(start)) {
        return 0;
    }
    memcpy(&first, KHDAYS_W(start), 4);
    for (int i = 0; i < script_count; ++i) {
        const Script *s = &scripts[i];
        if (s->first == first && khdays_game_ram(start + s->size - 1u) &&
            memcmp(KHDAYS_W(start), s->bytes, s->size) == 0) {
            snprintf(key, (size_t)size, "%s", s->key);
            return 1;
        }
    }
    return 0;
}
