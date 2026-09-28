#include "replica.h"
#include "pose.h"
#include "ragdoll.h"
#include "world.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *message) {
    if (condition) return;
    fprintf(stderr, "replica: %s\n", message);
    exit(EXIT_FAILURE);
}

static void check_actor(Actor source, Actor replica) {
    source.nav_edge = source.nav_goal = source.nav_phase = 0;
    source.flag_grab_cooldown = source.multikills = source.multikill_ticks = 0;
    source.grenade_cooldown = source.hit_ticks = source.health_cooldown = 0;
    source.ragdoll = replica.ragdoll = (Ragdoll){0};
    check(!memcmp(&source, &replica, sizeof(source)), "living actor prediction and visible state remain exact");
}

int main(void) {
    world_init(); poses_init(); ragdolls_init();
    Game game, decoded = {0};
    game_init(&game, 931, MODE_DEATHMATCH);
    game.actors[0].force = v3(.00390625f, .48f, -.19599999f);
    game.actors[0].move_direction = v3(.25f, 0, -.75f);
    game.actors[0].nav_edge = 17;
    game.actors[0].nav_goal = 29;
    game.actors[0].motion_tick = UINT64_C(0x1abcdef12);
    game.actors[0].shot_sequence = UINT64_C(0x2abcdef12);
    game.actors[1].life = DEAD;
    ragdoll_start(&game.actors[1]);
    game.actors[1].ragdoll.severed = (1u << 1) | (1u << 3) | (1u << 19) | (1u << 20) | (1u << 22);
    game.actors[1].ragdoll.ticks = 73;
    for (int i = 0; i <= RAGDOLL_PART_COUNT; ++i) {
        Vec3 point = v3(-501.231f + i * .1853f, 94.189f - i * .0271f, i * -19.193f);
        game.actors[1].ragdoll.position[i] = point;
        game.actors[1].ragdoll.previous[i] = add(point, v3(1, 2, 3));
        game.actors[1].ragdoll.old_position[i] = add(point, v3(4, 5, 6));
    }
    game.projectile_count = game.projectile_capacity = 2;
    game.projectiles = calloc(2, sizeof(*game.projectiles));
    if (!game.projectiles) abort();
    game.projectiles[0] = (Projectile){.id = 31, .weapon = AK74, .owner = 0, .ticks = 41,
        .position = {100, 75.125f, -89}, .previous = {99, 74, -80}, .velocity = {3, -2, 15},
        .hit_mask = 123, .rewind_ticks = 3.5f};
    game.projectiles[1] = (Projectile){.id = 39, .weapon = BARRETT, .owner = 3, .ticks = 120,
        .position = {-100, 65.25f, 79}, .velocity = {13, 2, -15}};
    Snapshot untouched = snapshot_encode(&game), base = replica_encode(&game);
    Snapshot after = snapshot_encode(&game);
    check(untouched.size == after.size && !memcmp(untouched.data, after.data, after.size),
        "projection never changes authoritative actors, joints, projectiles or pickups");
    free(untouched.data); free(after.data);
    check(replica_decode(&decoded, base.data, base.size), "visible state decodes");
    for (int i = 0; i < ACTOR_COUNT; ++i) check_actor(game.actors[i], decoded.actors[i]);
    for (int i = 1; i <= 20; ++i) {
        Vec3 difference = sub(game.actors[1].ragdoll.position[i], decoded.actors[1].ragdoll.position[i]);
        check(fabsf(difference.x) <= 1.0f / 256 && fabsf(difference.y) <= 1.0f / 256 && fabsf(difference.z) <= 1.0f / 256,
            "corpse joint error is bounded to half of a 1/128 unit");
        check(!memcmp(&decoded.actors[1].ragdoll.position[i], &decoded.actors[1].ragdoll.previous[i], sizeof(Vec3)),
            "decoded corpse poses initialize interpolation history");
    }
    for (int link = 0; link < RAGDOLL_CONSTRAINT_COUNT; ++link)
        if (game.actors[1].ragdoll.severed & (1u << link))
            check(ragdoll_links[link].a >= 1 && ragdoll_links[link].a <= 20 &&
                ragdoll_links[link].b >= 1 && ragdoll_links[link].b <= 20,
                "severed wounds use retained visible joints");
    check(decoded.actors[1].ragdoll.position[0].x == 0 && decoded.actors[1].ragdoll.position[21].x == 0 &&
        decoded.actors[1].ragdoll.position[24].z == 0, "server constraint helpers do not travel over the wire");
    check(decoded.actors[1].ragdoll.severed == game.actors[1].ragdoll.severed &&
        decoded.actors[1].ragdoll.ticks == 73, "dismemberment and bleeding age survive projection");
    check(decoded.projectiles[0].id == 31 && decoded.projectiles[1].id == 39 &&
        !memcmp(&decoded.projectiles[0].position, &game.projectiles[0].position, sizeof(Vec3)) &&
        !memcmp(&decoded.projectiles[0].velocity, &game.projectiles[0].velocity, sizeof(Vec3)),
        "rendered projectile identities and motion remain exact");
    check(decoded.event_count == 0 && decoded.history == NULL && decoded.actors[0].nav_goal == 0 &&
        decoded.projectiles[0].rewind_ticks == 0, "server-only work is absent from replicas");
    Snapshot copy = replica_encode(&decoded);
    check(copy.size == base.size && !memcmp(copy.data, base.data, copy.size), "replica decoding preserves its wire projection");
    free(copy.data);
    Input input = {.forward = .8f, .right = .2f, .yaw = .75f, .pitch = -.15f, .held = INPUT_JETS | INPUT_FIRE};
    movement_step(&game.actors[0], input, game.actors[0].motion_tick);
    movement_step(&decoded.actors[0], input, decoded.actors[0].motion_tick);
    combat_predict_actor(&game, 0, input);
    combat_predict_actor(&decoded, 0, input);
    check_actor(game.actors[0], decoded.actors[0]);
    game.actors[2].life = DEAD; ragdoll_start(&game.actors[2]);
    game.actors[1].life = ALIVE;
    game.projectiles[0] = game.projectiles[1];
    game.projectiles[1] = (Projectile){.id = 71, .weapon = LAW, .owner = 0, .ticks = 50, .position = {12, 45, 69}, .velocity = {3, 2, 1}};
    Snapshot current = replica_encode(&game), delta = replica_delta_pack(&current, &base), restored = {0};
    check(replica_delta_unpack(&restored, &base, delta.data, delta.size), "delta handles births, deaths and projectile removals and insertions");
    check(restored.size == current.size && !memcmp(restored.data, current.data, current.size), "entity deltas restore exact projected bytes");
    Snapshot wrong = {malloc(base.size), base.size};
    if (!wrong.data) abort();
    memcpy(wrong.data, base.data, base.size); wrong.data[12] ^= 1;
    unsigned char *saved = restored.data;
    check(!replica_delta_unpack(&restored, &wrong, delta.data, delta.size) && restored.data == saved,
        "wrong acknowledged baseline rejects without replacing state");
    free(wrong.data);
    for (size_t size = 0; size < delta.size; ++size)
        check(!replica_delta_unpack(&restored, &base, delta.data, size) && restored.data == saved,
            "truncated deltas reject atomically");
    delta.data[delta.size - 1] ^= 1;
    check(!replica_delta_unpack(&restored, &base, delta.data, delta.size) && restored.data == saved, "corrupted deltas reject atomically");
    delta.data[delta.size - 1] ^= 1;
    free(restored.data); restored = (Snapshot){malloc(base.size), base.size};
    if (!restored.data) abort();
    memcpy(restored.data, base.data, base.size);
    check(replica_delta_unpack(&restored, &restored, delta.data, delta.size) &&
        restored.size == current.size && !memcmp(restored.data, current.data, current.size), "baseline replacement works in place");
    uint64_t tick = decoded.tick;
    for (size_t size = 0; size < current.size; size += 97)
        check(!replica_decode(&decoded, current.data, size) && decoded.tick == tick, "truncated replica schemas preserve the previous game");
    current.data[0] ^= 1;
    check(!replica_decode(&decoded, current.data, current.size) && decoded.tick == tick, "unknown replica schema rejects atomically");
    current.data[0] ^= 1;
    free(restored.data); free(delta.data); free(current.data); free(base.data);
    game_free(&game); game_free(&decoded);
    ragdolls_free(); poses_free(); world_free();
    puts("Replicas: exact prediction, bounded corpse poses, keyed entity deltas and atomic rejection passed");
    return 0;
}
