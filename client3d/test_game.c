#include "game.h"
#include "world.h"
#include "bot_navigation.h"
#include "pose.h"
#include "ragdoll.h"
#include "snapshot.h"
#include <string.h>
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void check(int pass, const char *contract, uint64_t tick, size_t index) {
    if (pass) return;
    fprintf(stderr, "%s failed at tick %llu, index %zu\n", contract,
        (unsigned long long)tick, index);
    exit(EXIT_FAILURE);
}

static void same_game(const Game *left, const Game *right) {
    Snapshot a = snapshot_encode(left), b = snapshot_encode(right);
    check(a.size == b.size && !memcmp(a.data, b.data, a.size), "deterministic complete state", left->tick, 0);
    free(a.data);
    free(b.data);
}

static void traverse(int from, int to, int fuel) {
    Game game;
    game_init(&game, 0x726f7574u, MODE_DEATHMATCH);
    for (int i = 2; i < ACTOR_COUNT; ++i) game.actors[i].life = INACTIVE;
    Actor *bot = &game.actors[0];
    bot->position = bot->previous = world_nav_nodes[from].position;
    bot->velocity = bot->force = v3(0, 0, 0);
    bot->fuel = fuel;
    game.actors[1].position = game.actors[1].previous = world_nav_nodes[to].position;
    uint32_t previous = 0;
    float nearest = INFINITY;
    unsigned tick;
    for (tick = 0; tick < 60 * TICK_RATE; ++tick) {
        Vec3 aim = sub(world_nav_nodes[to].position, bot->position);
        Input input = {.yaw = atan2f(aim.x, aim.z)};
        bot_navigation(&game, 0, world_nav_nodes[to].position, BOT_TRAVERSE, &input);
        input.pressed = input.held & ~previous;
        previous = input.held;
        movement_step(bot, input, game.tick++);
        check(world_pose_clear(bot->position, bot->pose), "route remains outside collision geometry", tick, from);
        float distance = length(sub(bot->position, world_nav_nodes[to].position));
        nearest = fminf(nearest, distance);
        if (distance < 14 && fabsf(bot->position.y - world_nav_nodes[to].position.y) < 8) break;
    }
    if (tick == 60 * TICK_RATE)
        fprintf(stderr, "Route %d -> %d stopped at %.2f %.2f %.2f, nearest %.2f, goal %.2f %.2f %.2f\n",
            from, to, bot->position.x, bot->position.y, bot->position.z, nearest,
            world_nav_nodes[to].position.x, world_nav_nodes[to].position.y, world_nav_nodes[to].position.z);
    check(tick < 60 * TICK_RATE, "bot traverses the arena route", tick, (size_t)to);
    printf("route %d -> %d traversed in %u ticks\n", from, to, tick + 1);
    game_free(&game);
}

int main(void) {
    world_init();
    poses_init();
    ragdolls_init();
    Game sniper;
    game_init(&sniper, 0x736e6970u, MODE_DEATHMATCH);
    for (int i=1;i<ACTOR_COUNT;++i) sniper.actors[i].life=INACTIVE;
    Actor *shooter=&sniper.actors[0];
    game_select_loadout(shooter,BARRETT,COLT);
    for (int spawn=0;spawn<2;++spawn) {
        check(shooter->slots[0].ammo==weapons[BARRETT].ammo && shooter->slots[0].fire_count==0 &&
            shooter->slots[0].startup_count==0,"spawned Barrett is loaded and ready",sniper.tick,0);
        shooter->position=shooter->previous=v3(0,10000,0);
        Input fire[ACTOR_COUNT]={0};
        fire[0].held=INPUT_FIRE;
        while (shooter->spawn_protection_ticks>0) {
            game_step(&sniper,fire);
            check(shooter->slots[0].ammo==weapons[BARRETT].ammo,
                "spawn protection still prevents protected firing",sniper.tick,0);
        }
        game_step(&sniper,fire);
        check(shooter->slots[0].ammo==weapons[BARRETT].ammo-1,
            "Barrett fires on the first unprotected tick without a loading delay",sniper.tick,0);
        if (spawn==0) check(game_respawn(&sniper,0)==SPAWN_READY,"sniper respawns",sniper.tick,0);
    }
    game_free(&sniper);
    Game falling;
    game_init(&falling, 0x66616c6cu, MODE_DEATHMATCH);
    Actor *corpse = &falling.actors[0];
    corpse->slots[1].ammo = 2;
    game_select_loadout(corpse, MP5, COLT);
    check(corpse->slots[0].id == MP5 && corpse->slots[1].ammo == 2,
        "spawn weapon selection preserves unchanged magazine", 0, 0);
    corpse->slots[0].ammo = 3;
    corpse->spawn_protection_ticks = -1;
    game_select_loadout(corpse, AK74, LAW);
    check(corpse->slots[0].id == MP5 && corpse->slots[0].ammo == 3 && corpse->slots[1].id == COLT,
        "midfight loadout selection cannot grant weapons or ammunition", 0, 0);
    game_respawn(&falling, 0);
    check(corpse->slots[0].id == AK74 && corpse->slots[1].id == LAW,
        "respawn equips the queued loadout", 0, 0);
    corpse->position = corpse->previous = add(world_nav_nodes[0].position,v3(0,35,0));
    corpse->velocity = v3(0, 0, 0);
    corpse->force = v3(0, 99, 0);
    ragdoll_start(corpse);
    float original_head_y = corpse->ragdoll.position[12].y;
    corpse->life = DEAD;
    corpse->health = 0;
    corpse->respawn_ticks = SRC_RESPAWNTIME;
    Input idle[ACTOR_COUNT] = {0};
    game_step(&falling, idle);
    for (int tick = 1; tick < 60; ++tick) game_step(&falling, idle);
    check(corpse->life == DEAD && corpse->ragdoll.position[12].y < original_head_y &&
        corpse->ragdoll.ticks == 60, "dead actor simulates a falling articulated body", falling.tick, 0);
    check(corpse->respawn_ticks == SRC_RESPAWNTIME - 60, "falling preserves respawn countdown", falling.tick, 0);
    for (int tick = 60; tick < SRC_RESPAWNTIME - 1; ++tick) game_step(&falling, idle);
    check(corpse->life == DEAD, "dead actor waits the full respawn interval", falling.tick, 0);
    game_step(&falling, idle);
    check(corpse->life == ALIVE && corpse->health == SRC_DEFAULT_HEALTH,
        "landed corpse respawns on the original tick", falling.tick, 0);
    int respawn_marker = 0;
    for (size_t i = 0; i < world_team_spawn_count(TEAM_NONE); ++i)
        respawn_marker |= length(sub(corpse->position, world_team_spawn(TEAM_NONE, i))) == 0;
    check(respawn_marker, "respawn uses an original neutral spawn", falling.tick, 0);
    game_free(&falling);
    int summit = 0, start = 0, opposite = 0;
    float lowest=INFINITY;
    for (size_t i = 0; i < world_nav_node_count; ++i) {
        if (world_nav_nodes[i].position.y > world_nav_nodes[summit].position.y) summit = (int)i;
        lowest=fminf(lowest,world_nav_nodes[i].position.y);
    }
    float farthest = -1;
    for (size_t i = 0; i < world_nav_node_count; ++i) {
        if (world_nav_nodes[i].position.y > lowest + 10) continue;
        Vec3 delta = sub(world_nav_nodes[i].position, world_nav_nodes[summit].position);
        if (dot(delta, delta) > farthest) { farthest = dot(delta, delta); start = (int)i; }
    }
    farthest = -1;
    for (size_t i = 0; i < world_nav_node_count; ++i) {
        if (world_nav_nodes[i].position.y > lowest + 10) continue;
        Vec3 delta = sub(world_nav_nodes[i].position, world_nav_nodes[start].position);
        if (dot(delta, delta) > farthest) { farthest = dot(delta, delta); opposite = (int)i; }
    }
    traverse(start, summit, world_jet_fuel);
    traverse(summit, opposite, world_jet_fuel);
    int longest_jet = -1;
    farthest = -1;
    for (size_t i = 0; i < world_nav_link_count; ++i) {
        const NavLink *edge = &world_nav_links[i];
        Vec3 delta = sub(world_nav_nodes[edge->to].position, world_nav_nodes[edge->from].position);
        if (edge->mode != NAV_JET || delta.y <= 0) continue;
        float distance = delta.x * delta.x + delta.z * delta.z;
        if (distance > farthest) { farthest = distance; longest_jet = (int)i; }
    }
    check(longest_jet >= 0, "arena has an elevated jet crossing", 0, 0);
    traverse(world_nav_links[longest_jet].from, world_nav_links[longest_jet].to, 0);
    Game games[2];
    game_init(&games[0], 0x534f4c44u, MODE_DEATHMATCH);
    game_init(&games[1], 0x534f4c44u, MODE_DEATHMATCH);
    uint32_t previous_held[2][ACTOR_COUNT] = {0};
    unsigned shots = 0, hits = 0, kills = 0, respawns = 0;
    size_t peak_projectiles = 0;
    float deepest = 0, highest = 0;
    float minimum_z[ACTOR_COUNT], maximum_z[ACTOR_COUNT], minimum_y[ACTOR_COUNT], maximum_y[ACTOR_COUNT];
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        minimum_z[i] = maximum_z[i] = games[0].actors[i].position.z;
        minimum_y[i] = maximum_y[i] = games[0].actors[i].position.y;
    }
    clock_t started = clock();
    same_game(&games[0], &games[1]);
    for (uint64_t tick = 0; tick < 3600; tick++) {
        for (int run = 0; run < 2; run++) {
            Game *game = &games[run];
            Input inputs[ACTOR_COUNT];
            Life previous_life[ACTOR_COUNT];
            for (int index = 0; index < ACTOR_COUNT; index++) {
                inputs[index] = bot_input(game, index);
                inputs[index].pressed = inputs[index].held & ~previous_held[run][index];
                previous_held[run][index] = inputs[index].held;
                previous_life[index] = game->actors[index].life;
            }
            game_step(game, inputs);
            for (size_t index = 0; index < ACTOR_COUNT; index++) {
                const Actor *actor = &game->actors[index];
                if (actor->life == ALIVE && !world_pose_clear(actor->position, actor->pose)) {
                    fprintf(stderr, "Player overlaps map at tick %llu, actor %zu, position %.6f %.6f %.6f\n",
                        (unsigned long long)tick, index, actor->position.x, actor->position.y, actor->position.z);
                    exit(EXIT_FAILURE);
                }
                check(actor->fuel >= 0 && actor->fuel <= actor->fuel_capacity,
                    "jet fuel stays within source map capacity", tick, index);
                check(actor->health <= SRC_DEFAULT_HEALTH,
                    "health stays within original maximum", tick, index);
                check(actor->life != ALIVE || actor->health > 0,
                    "alive player has positive health", tick, index);
                for (int slot = 0; slot < 2; slot++)
                    check(actor->slots[slot].ammo >= 0 &&
                        actor->slots[slot].ammo <= weapons[actor->slots[slot].id].ammo,
                        "ammunition stays within source magazine capacity", tick, index);
                if (run == 0) {
                    if (previous_life[index] == DEAD && actor->life == ALIVE) respawns++;
                    deepest = fminf(deepest, actor->position.y);
                    highest = fmaxf(highest, actor->position.y);
                    minimum_z[index] = fminf(minimum_z[index], actor->position.z);
                    maximum_z[index] = fmaxf(maximum_z[index], actor->position.z);
                    minimum_y[index] = fminf(minimum_y[index], actor->position.y);
                    maximum_y[index] = fmaxf(maximum_y[index], actor->position.y);
                }
            }
        }
        same_game(&games[0], &games[1]);
        const Game *game = &games[0];
        if (game->projectile_count > peak_projectiles) peak_projectiles = game->projectile_count;
        for (size_t index = 0; index < game->event_count; index++) {
            shots += game->events[index].kind == EVENT_SHOT;
            hits += game->events[index].kind == EVENT_HIT;
            kills += game->events[index].kind == EVENT_KILL;
        }
    }
    check(shots > 0 && hits > 0 && kills > 0, "bot match produces combat and kills", 3600, 0);
    check(respawns > 0, "bot match completes death and respawn cycle", 3600, 0);
    int traversed_depth = 0, traversed_height = 0;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        traversed_depth += maximum_z[i] - minimum_z[i] > 80;
        traversed_height += maximum_y[i] - minimum_y[i] > 30;
    }
    check(traversed_depth > 1 && traversed_height > 1, "bots traverse depth routes and multiple levels", 3600, 0);
    double elapsed = (double)(clock() - started) / CLOCKS_PER_SEC;
    printf("3600 ticks, 7 bots, deterministic replay: %u shots, %u hits, %u kills, "
        "%u respawns, %zu peak projectiles, height %.2f..%.2f, %.3f CPU seconds for both runs\n",
        shots, hits, kills, respawns, peak_projectiles, deepest, highest, elapsed);
    game_free(&games[0]);
    game_free(&games[1]);
    ragdolls_free();
    poses_free();
    world_free();
    return EXIT_SUCCESS;
}
