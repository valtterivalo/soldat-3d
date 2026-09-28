#include "game.h"
#include "bot_navigation.h"
#include "generated_rules.h"
#include "pose.h"
#include "ragdoll.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    if(argc>2)return EXIT_FAILURE;
    size_t first=0,last=world_map_count;
    if(argc==2){first=world_map_index(argv[1]);if(first==SIZE_MAX)return EXIT_FAILURE;last=first+1;}
    poses_init();
    ragdolls_init();
    size_t passed = 0, failed = 0, blocked = 0, travel_counts[NAV_JUMP + 1] = {0};
    uint64_t simulated = 0;
    unsigned longest = 0;
    for (size_t map = first; map < last; ++map) {
        world_load(map);
        NavNode *nodes = world_nav_nodes;
        NavLink *links = world_nav_links;
        size_t node_count = world_nav_node_count, link_count = world_nav_link_count;
        Game game;
        game_init(&game, 0x726f7574u, MODE_DEATHMATCH);
        for (int i = 2; i < ACTOR_COUNT; ++i) game.actors[i].life = INACTIVE;
        Actor initial = game.actors[0];
        for (size_t link = 0; link < link_count; ++link) {
            NavLink edge = links[link];
            unsigned states = world_gate_count ? 2 * TEAM_SPECTATOR : 1;
            for (unsigned state = 0; state < states; ++state) {
                WorldQuery query = {WORLD_TRACE_ACTOR, state / 2,
                    state % 2 ? WORLD_HAS_FLAG : WORLD_NO_FLAG};
                if (!world_nav_link_allows(&edge, query)) {
                    ++blocked;
                    continue;
                }
                game.actors[0] = initial;
                game.bots[0] = (BotMemory){0};
                game.tick = 0;
                Actor *actor = &game.actors[0];
                actor->position = actor->previous = nodes[edge.from].position;
                actor->velocity = actor->force = v3(0, 0, 0);
                actor->fuel = world_jet_fuel;
                actor->team = (Team)query.team;
                actor->carried_flag = query.flag == WORLD_HAS_FLAG ? FLAG_YELLOW : FLAG_NONE;
                game.actors[1].position = game.actors[1].previous = nodes[edge.to].position;
                NavNode pair[] = {nodes[edge.from], nodes[edge.to]};
                NavLink direct = edge;
                direct.from = 0;
                direct.to = 1;
                world_nav_nodes = pair;
                world_nav_node_count = 2;
                world_nav_links = &direct;
                world_nav_link_count = 1;
                Vec3 delta = sub(pair[1].position, pair[0].position);
                float distance = hypotf(delta.x, delta.z);
                float damping = SRC_EDAMPING;
                float ground_damping = damping * SRC_SURFACECOEFX;
                float run_speed = SRC_RUNSPEED * ground_damping / (1 - ground_damping);
                float fly_speed = SRC_FLYSPEED * damping / (1 - damping);
                float jet_acceleration = SRC_JETSPEED - SRC_GRAV;
                float jet_speed = jet_acceleration * damping / (1 - damping);
                float fall_speed = SRC_GRAV * damping / (1 - damping);
                float apex = fmaxf(edge.apex, fmaxf(pair[0].position.y, pair[1].position.y));
                float climb = apex - pair[0].position.y, descent = apex - pair[1].position.y;
                unsigned budget = (unsigned)ceilf(distance / fminf(run_speed, fly_speed) +
                    2 * SRC_MAX_VELOCITY / SRC_FLYSPEED +
                    climb / jet_speed + sqrtf(2 * climb / jet_acceleration) +
                    descent / fall_speed + sqrtf(2 * descent / SRC_GRAV)) +
                    (unsigned)(2 * actor->fuel_capacity + SRC_JUMP_FRAMES + SRC_SIDEJUMP_FRAMES);
                uint32_t previous = 0;
                float nearest = INFINITY;
                unsigned tick = 0;
                const char *failure = "arrival";
                for (; tick < budget; ++tick) {
                    Vec3 aim = sub(pair[1].position, actor->position);
                    Input input = {.yaw = atan2f(aim.x, aim.z)};
                    bot_navigation(&game, 0, pair[1].position, BOT_TRAVERSE, &input);
                    input.pressed = input.held & ~previous;
                    previous = input.held;
                    movement_step(actor, input, game.tick++);
                    ++simulated;
                    if (!world_pose_clear_for(actor->position, actor->pose, query)) {
                        failure = "collision overlap";
                        break;
                    }
                    if (actor->fuel < 0 || actor->fuel > actor->fuel_capacity) {
                        failure = "fuel bounds";
                        break;
                    }
                    float remaining = length(sub(actor->position, pair[1].position));
                    nearest = fminf(nearest, remaining);
                    if (remaining < 14 && fabsf(actor->position.y - pair[1].position.y) < 8 &&
                        actor->contact == GROUNDED) {
                        failure = NULL;
                        break;
                    }
                }
                ++travel_counts[edge.mode];
                if (failure) {
                    fprintf(stderr, "%s edge%zu %d->%d mode%d team%u flag%d: %s after%u/%u ticks, "
                        "nearest%.2f, from(%.2f,%.2f,%.2f), to(%.2f,%.2f,%.2f), "
                        "actor(%.2f,%.2f,%.2f), fuel%d/%d\n",
                        world_map_names[map], link, edge.from, edge.to, edge.mode, query.team, query.flag, failure, tick, budget, nearest,
                        pair[0].position.x, pair[0].position.y, pair[0].position.z,
                        pair[1].position.x, pair[1].position.y, pair[1].position.z,
                        actor->position.x, actor->position.y, actor->position.z, actor->fuel, actor->fuel_capacity);
                    ++failed;
                } else {
                    ++passed;
                    if (tick + 1 > longest) longest = tick + 1;
                }
                world_nav_nodes = nodes;
                world_nav_node_count = node_count;
                world_nav_links = links;
                world_nav_link_count = link_count;
            }
        }
        game_free(&game);
    }
    printf("Navigation: %zu maps, %zu passed, %zu failed, %zu gate rejections, WALK%zu JET%zu DROP%zu JUMP%zu, "
        "%llu movement ticks, longest%u ticks\n", last-first, passed, failed, blocked,
        travel_counts[NAV_WALK], travel_counts[NAV_JET], travel_counts[NAV_DROP], travel_counts[NAV_JUMP],
        (unsigned long long)simulated, longest);
    ragdolls_free();
    poses_free();
    world_free();
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
