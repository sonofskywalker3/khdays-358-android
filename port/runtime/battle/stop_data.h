/* The Stop magic's data (stop_data.c): what the game left unmade of its sixth magic -- the levels' numbers, the
 * moogle's recipes and the words -- read from the port's own file, stop_magic.txt.
 * docs/RUNTIME_MAP.md, "The magic". Plain C: no game, no system calls. */
#ifndef KHDAYS_STOP_DATA_H
#define KHDAYS_STOP_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#define KHDAYS_STOP_LEVELS 3      /* Stop, Stopra, Stopga: the three places the battle's tables left */
#define KHDAYS_STOP_LANGUAGES 5   /* the game's: en, es, fr, de, it */
#define KHDAYS_STOP_INGREDIENTS 4 /* a recipe's, as the game's recipes' records hold them (database 0x1f) */

typedef struct KhdaysStopLevel {
    int set;      /* given in the file */
    int id;       /* its panel's item id */
    int ticks;    /* how long it holds an enemy, in battle ticks (30 a second) */
    double reach; /* how far from its caster it holds enemies, in the game's units */
    int price;    /* its panel's price */
} KhdaysStopLevel;

typedef struct KhdaysStopMaterial {
    int id, count;
} KhdaysStopMaterial;

/* A recipe: the moogle lists it once its first two ingredients have been owned (Ov026_IsRecipeListed). */
typedef struct KhdaysStopRecipe {
    int set;
    int munny;
    KhdaysStopMaterial materials[KHDAYS_STOP_INGREDIENTS];
    int material_count;
} KhdaysStopRecipe;

/* One language's words for a level, UTF-8, with the game's highlight (\x02 ... \x01) and line breaks. */
typedef struct KhdaysStopWords {
    char *name;
    char *panel; /* the panel's description (the camp, the shop) */
    char *magic; /* the magic's description (the magic's own list) */
} KhdaysStopWords;

typedef struct KhdaysStopData {
    KhdaysStopLevel levels[KHDAYS_STOP_LEVELS];
    int magic_lv; /* how much longer each Magic LV above 1 makes the hold, in hundredths */
    KhdaysStopRecipe recipes[KHDAYS_STOP_LEVELS];
    KhdaysStopWords words[KHDAYS_STOP_LANGUAGES][KHDAYS_STOP_LEVELS];
} KhdaysStopData;

/* The game's languages in the file's order, by their two letters. */
extern const char *const khdays_stop_languages[KHDAYS_STOP_LANGUAGES];

/* Reads `text` (`size` bytes) into `out`. Returns 0, or the first bad line's number with the reason in `why`. */
int khdays_stop_data_read(const char *text, unsigned size, KhdaysStopData *out, char *why, int why_size);

/* Whether each part is whole: the panels (every level and every word), the shop (every recipe). A part missing
 * anything is left out whole. */
int khdays_stop_data_panels_whole(const KhdaysStopData *d);
int khdays_stop_data_shop_whole(const KhdaysStopData *d);

void khdays_stop_data_free(KhdaysStopData *d);

#ifdef __cplusplus
}
#endif

#endif
