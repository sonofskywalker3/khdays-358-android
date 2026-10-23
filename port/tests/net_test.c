/* The multiplayer session's delivery between two processes on this machine (the test starts itself
 * again as the host): rooms found, a join, records delivered once and in order with 30% of datagrams
 * dropped; then the controls, faster than the host's ticks and at the battle's own pace, none waiting
 * longer than half a second (docs/BUILD.md, "The port's tests"). */
#include "../runtime/net/net_session.h"
#include "../host/net/net.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define GGID 0x00800356u
#define RECORDS 300
#define PORT 12
#define JOIN_UP_TO 0x3c0

static unsigned char data[0x10000];

static int host(void)
{
    const unsigned char info[] = "test room";
    KhdaysNetEvent e;
    unsigned int frame = 0, next = 0, deliveries = 0, joined = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    if (!khdays_net_begin() || !khdays_net_host(GGID, info, sizeof(info), 3)) {
        fprintf(stderr, "host: no room\n");
        return 2;
    }
    khdays_net_join_records(PORT, JOIN_UP_TO);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_JOINING) {
                khdays_net_accept(e.aid, 1);
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == PORT) {
                /* one record, or several joined: each whole, in order */
                unsigned int at = 0, count = 0;
                if (e.size > JOIN_UP_TO) {
                    fprintf(stderr, "host: %u bytes handed at once, past %u\n", e.size, JOIN_UP_TO);
                    return 3;
                }
                while (at < e.size) {
                    unsigned int index;
                    memcpy(&index, data + at, sizeof(index));
                    if (index != next || e.size - at < 4 + index % 200) {
                        fprintf(stderr, "host: record %u (%u bytes left) where %u was due\n", index, e.size - at, next);
                        return 3;
                    }
                    at += 4 + index % 200;
                    ++next;
                    ++count;
                    if (next == RECORDS) {
                        const unsigned char done[] = "all in order";
                        khdays_net_send(1u << e.aid, 14, done, sizeof(done));
                    }
                }
                ++deliveries;
                joined += count > 1;
            } else if (e.kind == KHDAYS_NET_LEFT) {
                khdays_net_end();
                printf("net: the host was handed them in %u deliveries, %u of them joined\n", deliveries, joined);
                if (joined == 0) {
                    fprintf(stderr, "host: no records joined\n");
                    return 6;
                }
                return next == RECORDS ? 0 : 4;
            }
        }
        /* a frame of the host's while the guest sends five: records wait
         * together, to be joined */
        SDL_Delay(10);
    }
    fprintf(stderr, "host: %u of %u records, then nothing\n", next, RECORDS);
    return 5;
}

static int guest(void)
{
    KhdaysNetEvent e;
    unsigned int frame = 0, sent = 0;
    int joined = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    KhdaysUdp *stray;
    KhdaysNetAddress group = {0xefff2352u, 35820};
    if (!khdays_net_begin()) {
        fprintf(stderr, "guest: no network\n");
        return 2;
    }
    /* something that is not the port's, where the rooms are announced */
    stray = khdays_host_udp_open(0, 0);
    if (stray != NULL) {
        khdays_host_udp_join(stray, group.ip);
        khdays_host_udp_send(stray, &group, "not a room", 10);
    }
    khdays_net_scan(GGID, 1);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_ROOM && !joined) {
                if (e.size != 10 || memcmp(data, "test room", 10) != 0) {
                    fprintf(stderr, "guest: a room with the wrong information\n");
                    return 3;
                }
                khdays_net_scan(GGID, 0);
                khdays_net_join(e.id, "ssid", 4);
                joined = -1;
            } else if (e.kind == KHDAYS_NET_JOINED) {
                joined = 1;
            } else if (e.kind == KHDAYS_NET_REFUSED) {
                fprintf(stderr, "guest: refused (reason %u)\n", e.reason);
                return 4;
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == 14) {
                if (strcmp((const char *)data, "all in order") != 0) {
                    return 5;
                }
                unsigned int aid = 99;
                int ms = -2;
                /* the guest's one member, the host (aid 0), its round trip measured by now */
                if (khdays_net_round_trips(&aid, &ms, 1) != 1 || aid != 0 || ms < 0) {
                    fprintf(stderr, "guest: the round trip to the host not reported (aid %u, %d ms)\n", aid, ms);
                    return 6;
                }
                printf("net: %u records delivered once each, in order, with 30%% of the datagrams lost; "
                       "link level %d, round trip %d ms\n",
                       RECORDS, khdays_net_link_level(), ms);
                khdays_net_end();
                khdays_host_udp_close(stray);
                return 0;
            }
        }
        if (joined == 1 && sent < RECORDS) {
            unsigned char record[4 + 200];
            const unsigned int size = 4 + sent % 200;
            memset(record, (int)sent, sizeof(record));
            memcpy(record, &sent, sizeof(sent));
            khdays_net_send(1, PORT, record, size);
            ++sent;
        }
        SDL_Delay(2);
    }
    fprintf(stderr, "guest: %s, %u sent, then nothing\n", joined == 1 ? "joined" : "not joined", sent);
    return 6;
}

/* --- the controls ------------------------------------------ */

#define CONTROLS 240

/* Control i: the game's gate-3 message (a header -- bit 0, gate 3 in bits
 * 1-5, 12 bytes in bits 6-15 -- then the packet): a sequence halfword, the
 * buttons (i / 10, in the bits above the slot), a command every seventh
 * (kind 1, its argument i) or none (kind 7). */
static unsigned int control(unsigned char *out, unsigned int i)
{
    const unsigned int header = 1u | (3u << 1) | (12u << 6);
    const unsigned int buttons = (i / 10) << 3;
    const unsigned int chosen = i % 7 == 3 ? 1u | (i << 4) : 7u;
    memset(out, 0, 14);
    out[0] = (unsigned char)header;
    out[1] = (unsigned char)(header >> 8);
    out[2] = (unsigned char)i;
    out[3] = (unsigned char)(i >> 8);
    out[4] = (unsigned char)buttons;
    out[5] = (unsigned char)(buttons >> 8);
    memcpy(out + 6, &chosen, 4);
    return 14;
}

static int host_controls(void)
{
    const unsigned char info[] = "test room";
    KhdaysNetEvent e;
    unsigned int frame = 0, received = 0, next_command = 3, next_buttons = 0, member = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    if (!khdays_net_begin() || !khdays_net_host(GGID, info, sizeof(info), 3)) {
        fprintf(stderr, "host: no room\n");
        return 2;
    }
    khdays_net_join_records(PORT, JOIN_UP_TO);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_JOINING) {
                khdays_net_accept(e.aid, 1);
                member = e.aid;
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == PORT) {
                unsigned int at = 0, controls = 0;
                while (at + 2 <= e.size) {
                    unsigned int header, length;
                    if (data[at] == 0) {
                        ++at;
                        continue;
                    }
                    header = (unsigned int)data[at] | (unsigned int)data[at + 1] << 8;
                    length = header >> 6;
                    if (((header >> 1) & 31u) == 3 && length == 12) {
                        unsigned int chosen,
                            buttons = ((unsigned int)data[at + 4] | (unsigned int)data[at + 5] << 8) >> 3;
                        memcpy(&chosen, data + at + 6, 4);
                        ++controls;
                        ++received;
                        if ((chosen & 15u) == 1) {
                            if ((chosen >> 4) != next_command) {
                                fprintf(stderr, "host: command %u where %u was due\n", chosen >> 4, next_command);
                                return 3;
                            }
                            next_command += 7;
                        }
                        if (buttons != next_buttons) {
                            if (buttons != next_buttons + 1) {
                                fprintf(stderr, "host: buttons %u after %u: a change lost\n", buttons, next_buttons);
                                return 3;
                            }
                            next_buttons = buttons;
                        }
                    }
                    at += 2 + length;
                }
                if (controls > 1) {
                    fprintf(stderr, "host: %u control packets handed at once\n", controls);
                    return 3;
                }
                if (next_command >= CONTROLS && next_buttons == (CONTROLS - 1) / 10) {
                    const unsigned char done[] = "all in order";
                    khdays_net_send(1u << member, 14, done, sizeof(done));
                }
            } else if (e.kind == KHDAYS_NET_LEFT) {
                khdays_net_end();
                printf("net: %u of %u control packets handed, one a tick, every command and change in order\n",
                       received, CONTROLS);
                return next_command >= CONTROLS ? 0 : 4;
            }
        }
        if (frame % 2 == 0) {
            /* the host's battle tick: its state on the game's port (gate 0) -- from the first frame, as
             * the battle ticks before a guest's first control can come (to nobody until one joins) */
            const unsigned char tick[6] = {(unsigned char)((1u | (4u << 6)) & 0xffu),
                                           (unsigned char)((1u | (4u << 6)) >> 8)};
            khdays_net_send(member != 0 ? 1u << member : 0u, PORT, tick, sizeof(tick));
        }
        SDL_Delay(10);
    }
    fprintf(stderr, "host: commands up to %u, buttons %u, then nothing\n", next_command, next_buttons);
    return 5;
}

static int guest_controls(void)
{
    KhdaysNetEvent e;
    unsigned int frame = 0, sent = 0;
    int joined = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    if (!khdays_net_begin()) {
        fprintf(stderr, "guest: no network\n");
        return 2;
    }
    khdays_net_scan(GGID, 1);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_ROOM && !joined) {
                khdays_net_scan(GGID, 0);
                khdays_net_join(e.id, "ssid", 4);
                joined = -1;
            } else if (e.kind == KHDAYS_NET_JOINED) {
                joined = 1;
            } else if (e.kind == KHDAYS_NET_REFUSED) {
                fprintf(stderr, "guest: refused (reason %u)\n", e.reason);
                return 4;
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == 14) {
                khdays_net_end();
                return strcmp((const char *)data, "all in order") == 0 ? 0 : 5;
            }
        }
        if (joined == 1 && sent < CONTROLS) {
            unsigned char packet[16];
            khdays_net_send(1, PORT, packet, control(packet, sent));
            ++sent;
        }
        SDL_Delay(3); /* about three of them a frame of the host's */
    }
    fprintf(stderr, "guest: %s, %u controls sent, then nothing\n", joined == 1 ? "joined" : "not joined", sent);
    return 6;
}

/* --- the controls at the battle's pace, the host sending more than once a tick --- */

#define PACED 900      /* controls: 18 s of them, one a tick */
#define LATEST_MS 500u /* the longest any may wait */

static int host_paced(void)
{
    const unsigned char info[] = "test room";
    KhdaysNetEvent e;
    unsigned int frame = 0, received = 0, member = 0, latest = 0, last_index = 0;
    int told = 0;
    Uint64 done_at = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    if (!khdays_net_begin() || !khdays_net_host(GGID, info, sizeof(info), 3)) {
        fprintf(stderr, "host: no room\n");
        return 2;
    }
    khdays_net_join_records(PORT, JOIN_UP_TO);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_JOINING) {
                khdays_net_accept(e.aid, 1);
                member = e.aid;
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == PORT) {
                unsigned int at = 0;
                while (at + 2 <= e.size) {
                    unsigned int header, length;
                    if (data[at] == 0) {
                        ++at;
                        continue;
                    }
                    header = (unsigned int)data[at] | (unsigned int)data[at + 1] << 8;
                    length = header >> 6;
                    if (((header >> 1) & 31u) == 3 && length == 12) {
                        /* when the guest sent it (the PC's clock, GetTickCount64: the two processes'
                         * own SDL clocks start apart): how long it took */
                        unsigned int sent_ms, waited;
                        memcpy(&sent_ms, data + at + 10, 4);
                        waited = (unsigned int)GetTickCount64() - sent_ms;
                        latest = waited > latest ? waited : latest;
                        last_index = (unsigned int)data[at + 2] | (unsigned int)data[at + 3] << 8;
                        ++received;
                    }
                    at += 2 + length;
                }
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == 14 && done_at == 0) {
                done_at = SDL_GetTicks(); /* the guest sent its last: those still on the way, a second more */
            } else if (e.kind == KHDAYS_NET_LEFT) {
                khdays_net_end();
                return latest <= LATEST_MS ? 0 : 4;
            }
        }
        if (done_at != 0 && !told && SDL_GetTicks() - done_at > 1000) {
            const unsigned char verdict[] = "paced";
            printf("net: %u of %u controls at the battle's pace handed, up to control %u, none later than %u ms "
                   "(at most %u ms allowed)\n",
                   received, PACED, last_index, latest, LATEST_MS);
            if (latest > LATEST_MS || received < PACED / 2) {
                fprintf(stderr, "host: a control %u ms late, %u handed\n", latest, received);
                return 3;
            }
            khdays_net_send(1u << member, 14, verdict, sizeof(verdict));
            told = 1; /* the guest leaves */
        }
        {
            /* every frame a message of the game's reliable channel (gate 11, 2 bytes), every other one
             * the battle's tick (gate 0): two sends a tick */
            const unsigned char reliable[4] = {(unsigned char)((1u | (11u << 1) | (2u << 6)) & 0xffu),
                                               (unsigned char)((1u | (11u << 1) | (2u << 6)) >> 8)};
            const unsigned char tick[6] = {(unsigned char)((1u | (4u << 6)) & 0xffu),
                                           (unsigned char)((1u | (4u << 6)) >> 8)};
            if (frame % 2 == 0) {
                khdays_net_send(member != 0 ? 1u << member : 0u, PORT, tick, sizeof(tick));
            }
            khdays_net_send(member != 0 ? 1u << member : 0u, PORT, reliable, sizeof(reliable));
        }
        SDL_Delay(10);
    }
    fprintf(stderr, "host: %u controls, then nothing (latest %u ms)\n", received, latest);
    return 5;
}

static int guest_paced(void)
{
    KhdaysNetEvent e;
    unsigned int frame = 0, sent = 0;
    int joined = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    if (!khdays_net_begin()) {
        fprintf(stderr, "guest: no network\n");
        return 2;
    }
    khdays_net_scan(GGID, 1);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_ROOM && !joined) {
                khdays_net_scan(GGID, 0);
                khdays_net_join(e.id, "ssid", 4);
                joined = -1;
            } else if (e.kind == KHDAYS_NET_JOINED) {
                joined = 1;
            } else if (e.kind == KHDAYS_NET_REFUSED) {
                fprintf(stderr, "guest: refused (reason %u)\n", e.reason);
                return 4;
            } else if (e.kind == KHDAYS_NET_RECORD && e.port == 14) {
                khdays_net_end();
                return strcmp((const char *)data, "paced") == 0 ? 0 : 5;
            }
        }
        if (joined == 1 && sent < PACED && frame % 2 == 0) {
            /* one a tick of the host's (every other frame of 10 ms), the time it was sent in it */
            unsigned char packet[16];
            const unsigned int size = control(packet, sent);
            const unsigned int now = (unsigned int)GetTickCount64();
            memcpy(packet + 10, &now, 4);
            khdays_net_send(1, PORT, packet, size);
            if (++sent == PACED) {
                const unsigned char last[] = "last";
                khdays_net_send(1, 14, last, sizeof(last));
            }
        }
        SDL_Delay(10);
    }
    fprintf(stderr, "guest: %s, %u controls sent, then nothing\n", joined == 1 ? "joined" : "not joined", sent);
    return 6;
}

/* --- the rooms' details (the port's list of rooms) ------------------------ */

#define ROOM_NAME "Sala de prueba \xc3\xb1"
#define ROOM_PASSWORD "secreto"

/* A room with a name and a password: it takes whoever proves it, until one has joined and left. */
static int host_locked(void)
{
    /* as the game's: WMGameInfo's 16 bytes, then its user information, the host's name in UTF-16 */
    const unsigned char info[16 + 10] = {[16] = 't', [18] = 'e', [20] = 's', [22] = 't'};
    KhdaysNetEvent e;
    unsigned int frame = 0;
    int joined = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    khdays_net_room_details(ROOM_NAME, ROOM_PASSWORD);
    if (!khdays_net_begin() || !khdays_net_host(GGID, info, sizeof(info), 3)) {
        fprintf(stderr, "host: no room\n");
        return 2;
    }
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_JOINING) {
                khdays_net_accept(e.aid, 1);
            } else if (e.kind == KHDAYS_NET_JOINED) {
                joined = 1;
            } else if (e.kind == KHDAYS_NET_LEFT && joined) {
                khdays_net_end();
                return 0;
            }
        }
        SDL_Delay(5);
    }
    fprintf(stderr, "host: %s\n", joined ? "the guest never left" : "nobody joined");
    return 5;
}

/* The guest: the room's name, players, password and ping as the list shows them; a wrong password
 * refused, the right one taken. */
static int guest_locked(void)
{
    KhdaysNetEvent e;
    KhdaysNetRoomView room = {0};
    char name[64];
    unsigned int frame = 0;
    int stage = 0; /* 0 looking, 1 the wrong password sent, 2 the right one sent */
    const Uint64 until = SDL_GetTicks() + 60000;
    if (!khdays_net_begin()) {
        fprintf(stderr, "guest: no network\n");
        return 2;
    }
    khdays_net_scan(GGID, 1);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_REFUSED) {
                if (stage != 1 || e.reason != KHDAYS_NET_REFUSE_PASSWORD) {
                    fprintf(stderr, "guest: refused (reason %u) at stage %d\n", e.reason, stage);
                    return 3;
                }
                khdays_net_scan(GGID, 1);
                stage = 2;
                khdays_net_join_password(ROOM_PASSWORD);
                khdays_net_join(room.id, "ssid", 4);
            } else if (e.kind == KHDAYS_NET_JOINED) {
                if (stage != 2) {
                    fprintf(stderr, "guest: joined with the wrong password\n");
                    return 4;
                }
                printf("net: a room's name, players (%d of %d), password and ping (%d ms) seen; the wrong "
                       "password refused, the right one taken\n",
                       room.players, room.most, room.ping_ms);
                khdays_net_end();
                return 0;
            }
        }
        if (stage == 0 && khdays_net_room_count() == 1 && khdays_net_room_at(0, &room, name, sizeof(name)) >= 0 &&
            room.ping_ms >= 0) {
            if (strcmp(name, ROOM_NAME) != 0 || !room.locked || room.players != 1 || room.most != 4 ||
                memcmp(room.host, L"test", 8) != 0) {
                fprintf(stderr, "guest: the room seen as \"%s\", locked %d, %d of %d players\n", name, room.locked,
                        room.players, room.most);
                return 5;
            }
            khdays_net_scan(GGID, 0);
            khdays_net_join_password("wrong");
            khdays_net_join(room.id, "ssid", 4);
            stage = 1;
        }
        SDL_Delay(2);
    }
    fprintf(stderr, "guest: stage %d, then nothing\n", stage);
    return 6;
}

/* --- gameplay mods (runtime/scripts): a room takes only the host's ---------- */

#define MODS_FINGERPRINT 0x1234u
#define MODS_LISTED 200 /* more than one datagram holds: the refusal says how many it left out */

/* The host's 200 gameplay mods: two named, then fillers of 40-letter names. */
static void host_mods(void)
{
    static unsigned char list[MODS_LISTED * 48];
    unsigned int at = 0;
    const char *names[2][2] = {{"Double munny", "1.0"}, {"Infinite HP", "2"}};
    for (int m = 0; m < MODS_LISTED; ++m) {
        char filler[48];
        const char *name = m < 2 ? names[m][0] : filler, *version = m < 2 ? names[m][1] : "1";
        SDL_snprintf(filler, sizeof(filler), "A mod with a long name for filling %05d", m);
        list[at++] = (unsigned char)SDL_strlen(name);
        SDL_memcpy(list + at, name, SDL_strlen(name));
        at += (unsigned int)SDL_strlen(name);
        list[at++] = (unsigned char)SDL_strlen(version);
        SDL_memcpy(list + at, version, SDL_strlen(version));
        at += (unsigned int)SDL_strlen(version);
    }
    khdays_net_set_mods(MODS_FINGERPRINT, list, at, MODS_LISTED);
}

static int host_modded(void)
{
    const unsigned char info[16 + 10] = {[16] = 'm'};
    KhdaysNetEvent e;
    unsigned int frame = 0;
    int joined = 0;
    const Uint64 until = SDL_GetTicks() + 60000;
    host_mods();
    khdays_net_set_stop_magic(0); /* and without the Stop magic */
    if (!khdays_net_begin() || !khdays_net_host(GGID, info, sizeof(info), 3)) {
        fprintf(stderr, "host: no room\n");
        return 2;
    }
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_JOINING) {
                khdays_net_accept(e.aid, 1);
            } else if (e.kind == KHDAYS_NET_JOINED) {
                joined = 1;
            } else if (e.kind == KHDAYS_NET_LEFT && joined) {
                khdays_net_end();
                return 0;
            }
        }
        SDL_Delay(5);
    }
    fprintf(stderr, "host: %s\n", joined ? "the guest never left" : "nobody joined");
    return 5;
}

/* The guest: with other gameplay mods refused, told the host's; with the same but the Stop magic on, refused
 * and told the host plays without it; with the same and Stop off, taken. */
static int guest_modded(void)
{
    KhdaysNetEvent e;
    KhdaysNetRoomView room = {0};
    char name[64], told[2048];
    unsigned int frame = 0;
    int stage = 0, more = 0; /* 0 looking, 1 sent with other mods, 2 with the host's and Stop, 3 without Stop */
    const Uint64 until = SDL_GetTicks() + 60000;
    khdays_net_set_mods(0x5678u, (const unsigned char *)"\x01x\x01y", 4, 1);
    if (!khdays_net_begin()) {
        fprintf(stderr, "guest: no network\n");
        return 2;
    }
    khdays_net_scan(GGID, 1);
    while (SDL_GetTicks() < until) {
        ++frame;
        while (khdays_net_next(&e, data, sizeof(data), frame)) {
            if (e.kind == KHDAYS_NET_REFUSED) {
                int shown = 1;
                if ((stage != 1 && stage != 2) || e.reason != KHDAYS_NET_REFUSE_MODS) {
                    fprintf(stderr, "guest: refused (reason %u) at stage %d\n", e.reason, stage);
                    return 3;
                }
                if (!khdays_net_last_refusal_stop_off()) {
                    fprintf(stderr, "guest: not told the host plays without Stop (stage %d)\n", stage);
                    return 8;
                }
                if (stage == 2) {
                    khdays_net_set_stop_magic(0);
                    khdays_net_scan(GGID, 1);
                    stage = 3;
                    khdays_net_join(room.id, "ssid", 4);
                    continue;
                }
                khdays_net_last_refusal_mods(told, sizeof(told), &more);
                for (const char *c = told; *c; ++c) {
                    shown += *c == ',';
                }
                if (SDL_strncmp(told, "Double munny 1.0, Infinite HP 2, A mod", 38) != 0 || more <= 0 ||
                    shown + more != MODS_LISTED) {
                    fprintf(stderr, "guest: told \"%.80s\", %d shown, %d more\n", told, shown, more);
                    return 7;
                }
                host_mods(); /* the same mods now */
                khdays_net_scan(GGID, 1);
                stage = 2;
                khdays_net_join(room.id, "ssid", 4);
            } else if (e.kind == KHDAYS_NET_JOINED) {
                if (stage != 3) {
                    fprintf(stderr, "guest: joined with other gameplay mods or the Stop magic (stage %d)\n", stage);
                    return 4;
                }
                printf("net: a guest with other gameplay mods refused and told the host's (%d of them left "
                       "out of one answer), one with the same but Stop refused and told, one without Stop taken\n",
                       more);
                khdays_net_end();
                return 0;
            }
        }
        if (stage == 0 && khdays_net_room_count() == 1 && khdays_net_room_at(0, &room, name, sizeof(name)) >= 0) {
            khdays_net_scan(GGID, 0);
            khdays_net_join(room.id, "ssid", 4);
            stage = 1;
        }
        SDL_Delay(2);
    }
    fprintf(stderr, "guest: stage %d, then nothing\n", stage);
    return 6;
}

/* The guest here, the host started again as `argument`. */
static int pair(const char *exe, const char *argument, int (*guest_side)(void))
{
    char command[MAX_PATH + 32];
    STARTUPINFOA startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    DWORD code = 1;
    int result;
    snprintf(command, sizeof(command), "\"%s\" %s", exe, argument);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        fprintf(stderr, "cannot start the host\n");
        return 1;
    }
    result = guest_side();
    WaitForSingleObject(process.hProcess, 70000);
    GetExitCodeProcess(process.hProcess, &code);
    if (result != 0 || code != 0) {
        fprintf(stderr, "%s: guest %d, host %lu\n", argument, result, code);
        TerminateProcess(process.hProcess, 1);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    SetEnvironmentVariableA("KHDAYS_NET_LOSS", "0.3");
    SetEnvironmentVariableA("KHDAYS_NET_SERVER", ""); /* the local network only: no test reaches the internet */
    if (argc > 1 && strcmp(argv[1], "--host") == 0) {
        return host();
    }
    if (argc > 1 && strcmp(argv[1], "--host-controls") == 0) {
        return host_controls();
    }
    if (argc > 1 && strcmp(argv[1], "--host-paced") == 0) {
        return host_paced();
    }
    if (argc > 1 && strcmp(argv[1], "--host-locked") == 0) {
        return host_locked();
    }
    if (argc > 1 && strcmp(argv[1], "--host-modded") == 0) {
        return host_modded();
    }
    if (pair(argv[0], "--host", guest) != 0 || pair(argv[0], "--host-controls", guest_controls) != 0 ||
        pair(argv[0], "--host-paced", guest_paced) != 0) {
        return 1;
    }
    if (pair(argv[0], "--host-locked", guest_locked) != 0) {
        return 1;
    }
    return pair(argv[0], "--host-modded", guest_modded);
}
