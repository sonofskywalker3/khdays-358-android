/* Place independent images beside their source shapes, preserving the composition's order. */
#ifndef KHDAYS_ONESCREEN_IMAGES_H
#define KHDAYS_ONESCREEN_IMAGES_H

#include "../../ui/composition.h"

typedef struct KhdaysFramePackage KhdaysFramePackage;
/* Nonzero disables native banner crops/timing; native HUD visibility and placement stay the game's. */
int khdays_onescreen_information(const KhdaysFramePackage *package, KhdaysSituation *situation);

/* The resulting shapes stay valid until the next call; input is a fresh composition. */
void khdays_onescreen_images(const KhdaysFramePackage *package, const KhdaysSituation *situation,
                             KhdaysComposition *composition);

/* The 2D scale the lower screen is shown at where one of the `count` shapes (KO_WORDS words each) puts most
 * of it in the picture undimmed (a tutorial's page, a menu's lower screen laid over the picture), the
 * picture's own 2D being composed at `upper_scale`: that scale times the shape's, rounded up, `upper_scale`
 * at most. 0 when no shape shows that much of it: its pieces in a field's HUD are small crops, and stay at
 * the lower screen's own setting, as does a dimmed backdrop. */
int khdays_onescreen_lower_scale(const uint32_t *shapes, int count, int upper_scale);

#endif
