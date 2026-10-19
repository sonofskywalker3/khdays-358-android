/* The mods' load order and which are on: order.ini in the player's saves folder, a `<folder> = on|off`
 * line per mod folder seen, in load order. */
#ifndef KHDAYS_SCRIPTS_ORDER_H
#define KHDAYS_SCRIPTS_ORDER_H

typedef struct KhdaysOrderEntry {
    char *folder;
    int on;
    int present; /* the folder is in mods/ now */
} KhdaysOrderEntry;

typedef struct KhdaysOrder {
    KhdaysOrderEntry *entries;
    int count, capacity;
} KhdaysOrder;

/* Reads order.ini's text (NULL: none yet). */
void khdays_order_parse(KhdaysOrder *o, const char *text);

/* Marks the listed folders found in mods/ (`found`, in name order) present, and adds the ones not
 * listed after the others, on. */
void khdays_order_merge(KhdaysOrder *o, const char *const *found, int count);

/* Moves the present entry `index` one place among the present ones (`by` -1 or +1). */
void khdays_order_move(KhdaysOrder *o, int index, int by);

/* order.ini's text: the present folders in order, then the absent ones; to free(). */
char *khdays_order_text(const KhdaysOrder *o);

void khdays_order_free(KhdaysOrder *o);

#endif
