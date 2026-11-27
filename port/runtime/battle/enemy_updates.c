/* The enemies' updates (enemy_updates.h). */
#include "enemy_updates.h"
#include "../input/replay.h"
#include "../../host/app/host.h"

#include <stdlib.h>

int khdays_enemy_far_updates(void)
{
    /* decided once, at the first enemy's update or the recording's start, after the host has looked at the card
     * (the game thread starts once it has): the same for the whole session, whatever the host does later */
    static int ds_way = -1;
    if (ds_way < 0) {
        const char *v = getenv("KHDAYS_DS_ENEMY_UPDATES");
        if (v != NULL && v[0] != '\0') {
            ds_way = v[0] == '1';
        } else if (khdays_replay_playing() && khdays_replay_enemy_ds() >= 0) {
            ds_way = khdays_replay_enemy_ds(); /* as the recording was made */
        } else {
            ds_way = (khdays_host_graphics_flags() & KHDAYS_HOST_ENEMIES_DS) != 0;
        }
    }
    return ds_way;
}
