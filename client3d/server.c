#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "network.h"
#include "world.h"
#include "pose.h"
#include "ragdoll.h"
#include "pickups.h"
#include "lobby.h"
#include "generated_rules.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

static volatile sig_atomic_t running = 1;
static void stop(int signal_number) { (void)signal_number; running = 0; }

int main(int argc, char **argv) {
    unsigned short port = 23073;
    unsigned short lobby_port = LOBBY_PORT;
    int bots = 6, bonuses = 0;
    int score_limit = -1, time_limit = -1, friendly_fire = 0;
    uint64_t ticks = 0;
    const char *map = "Arena2";
    const char *directory = NULL, *server_name = "Soldat 3D";
    GameMode mode = MODE_DEATHMATCH;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--map") && i + 1 < argc) map = argv[++i];
        else if (!strcmp(argv[i], "--lobby") && i + 1 < argc) directory = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc) server_name = argv[++i];
        else if (!strcmp(argv[i], "--friendly-fire")) friendly_fire = 1;
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
            const char *name = argv[++i];
            int found = 0;
            for (int m = MODE_DEATHMATCH; m <= MODE_HTF; ++m)
                if (!strcmp(name, game_mode_ids[m])) { mode = (GameMode)m; found = 1; }
            if (!found) { fprintf(stderr, "Unknown mode %s\n", name); return 2; }
        } else if (i + 1 < argc && (!strcmp(argv[i], "--port") || !strcmp(argv[i], "--bots") ||
            !strcmp(argv[i], "--bonuses") || !strcmp(argv[i], "--ticks") || !strcmp(argv[i], "--lobby-port") ||
            !strcmp(argv[i], "--score-limit") || !strcmp(argv[i], "--time-limit"))) {
            const char *option = argv[i++];
            char *end;
            errno = 0;
            unsigned long long value = strtoull(argv[i], &end, 10);
            if (errno || end == argv[i] || *end || argv[i][0] == '-') { fprintf(stderr, "Invalid %s\n", option); return 2; }
            if (!strcmp(option, "--port") && value <= 65535) port = (unsigned short)value;
            else if (!strcmp(option, "--lobby-port") && value > 0 && value <= 65535) lobby_port = (unsigned short)value;
            else if (!strcmp(option, "--bots") && value < ACTOR_COUNT) bots = (int)value;
            else if (!strcmp(option, "--bonuses") && value <= 5) bonuses = (int)value;
            else if (!strcmp(option, "--ticks") && value > 0) ticks = value;
            else if (!strcmp(option, "--score-limit") && value <= INT_MAX) score_limit = (int)value;
            else if (!strcmp(option, "--time-limit") && value <= INT_MAX / (60*TICK_RATE)) time_limit = (int)value;
            else { fprintf(stderr, "Invalid %s\n", option); return 2; }
        } else {
            fprintf(stderr, "Usage: %s [--port 23073] [--name name] [--lobby hostname] [--lobby-port 23074] [--map Arena2] [--bots 6] [--mode deathmatch|pointmatch|teammatch|ctf|rambo|inf|htf] [--bonuses 0..5] [--score-limit N] [--time-limit minutes] [--friendly-fire] [--ticks N]\n", argv[0]);
            return 2;
        }
    }
    size_t map_index = world_map_index(map);
    if (map_index == SIZE_MAX) { fprintf(stderr, "Unknown map %s\n", map); return 2; }
    world_load(map_index);
    if (!world_supports_mode(mode)) {
        fprintf(stderr,"Map %s does not support %s. Choose a map with the required team and objective spawns.\n",map,game_mode_ids[mode]);return 2;
    }
    poses_init();
    ragdolls_init();
    Game game;
    game_init(&game, 0x501da7, mode);
    game.bonus_frequency = bonuses;
    for (int i = 0; i < ACTOR_COUNT; ++i)
        if (i == 0 || i > bots) game.actors[i].life = INACTIVE;
        else game.actors[i].life = ALIVE;
    game_set_mode(&game, mode);
    game.friendly_fire = friendly_fire;
    if (score_limit >= 0) game.score_limit = score_limit;
    if (time_limit >= 0) game.time_limit_ticks = time_limit * 60 * TICK_RATE;
    Network *network = network_host(port, -1, &game);
    network_set_name(network, server_name);
    LobbyHost *advertisement = lobby_host_open(network_socket(network), directory, lobby_port, server_name);
    signal(SIGINT, stop);
    signal(SIGTERM, stop);
    printf("Soldat 3D server UDP %u, map %s, %d bots, %d player slots\n",
        network_port(network), world_map_names[world_map_current], bots, ACTOR_COUNT);
    fflush(stdout);
    uint32_t previous[ACTOR_COUNT] = {0};
    double next = network_time();
    int intermission_ticks = 0;
    while (running && (!ticks || game.tick < ticks)) {
        network_receive(network, &game);
        lobby_host_update(advertisement, network_time());
        Input inputs[ACTOR_COUNT] = {0};
        for (int i = 0; i < ACTOR_COUNT; ++i) {
            if (network_remote(network, i)) continue;
            inputs[i] = bot_input(&game, i);
            inputs[i].pressed = inputs[i].held & ~previous[i];
            previous[i] = inputs[i].held;
        }
        network_inputs(network, inputs);
        if (game.phase == MATCH_PLAYING) game_step(&game, inputs);
        else {
            game.event_count = 0;
            ++game.tick;
            if (++intermission_ticks >= SRC_DEFAULT_MAPCHANGE_TIME) {
                game_restart(&game);
                intermission_ticks = 0;
            }
        }
        network_broadcast(network, &game);
        next += 1.0 / TICK_RATE;
        double delay = next - network_time();
        if (delay > 0) {
            struct timespec remaining = {.tv_sec = (time_t)delay, .tv_nsec = (long)((delay - (time_t)delay) * 1000000000)};
            while (nanosleep(&remaining, &remaining) != 0 && running)
                if (errno != EINTR) { perror("nanosleep"); return 1; }
        }
    }
    printf("Server stopped at tick %llu\n", (unsigned long long)game.tick);
    lobby_host_close(advertisement);
    network_close(network);
    game_free(&game);
    ragdolls_free();
    poses_free();
    world_free();
    return 0;
}
