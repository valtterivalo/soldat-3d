#include "game.h"
#include "world.h"
#include "generated_rules.h"
#include <stdio.h>
#include <stdlib.h>

static void equal(const char *contract, float actual, float expected) {
    if (fabsf(actual - expected) < 0.0001f) return;
    fprintf(stderr, "%s: got %.8f, expected %.8f\n", contract, actual, expected);
    exit(EXIT_FAILURE);
}

static Actor airborne(void) {
    return (Actor){
        .position = {0, 10000, 0},
        .health = SRC_DEFAULT_HEALTH,
        .fuel_capacity = 120,
        .fuel = 120,
        .animation_tick = 1
    };
}

int main(void) {
    world_init();
    Actor actor = airborne();
    float position = actor.position.y;
    float velocity = 0;
    for (uint64_t tick = 0; tick < 60; tick++) {
        velocity -= SRC_GRAV;
        position += velocity;
        velocity *= SRC_EDAMPING;
        movement_step(&actor, (Input){0}, tick);
        equal("freefall position before damping", actor.position.y, position);
        equal("freefall velocity after damping", actor.velocity.y, velocity);
    }
    equal("normal mode has no falling damage", actor.health, SRC_DEFAULT_HEALTH);

    actor = airborne();
    actor.contact = GROUNDED;
    for (int frame = 1; frame <= 15; frame++) {
        movement_step(&actor, (Input){.held = INPUT_JUMP}, (uint64_t)frame);
        equal("vertical jump force window", actor.force.y,
            frame >= 9 && frame <= 14 ? SRC_JUMPSPEED : 0);
    }

    actor = airborne();
    actor.contact = GROUNDED;
    for (int frame = 1; frame <= 11; frame++) {
        movement_step(&actor, (Input){.forward = 1, .held = INPUT_JUMP}, (uint64_t)frame);
        equal("sidejump forward force window", actor.force.z,
            frame >= 4 && frame <= 10 ? SRC_JUMPDIRSPEED : 0);
        equal("sidejump vertical force window", actor.force.y,
            frame >= 4 && frame <= 10 ? SRC_JUMPDIRSPEED / 1.2f : 0);
    }

    actor = airborne();
    actor.fuel = 3;
    for (uint64_t tick = 0; tick < 20; tick++) {
        movement_step(&actor, (Input){.held = INPUT_JETS}, tick);
        equal("jet consumes one fuel each active tick", (float)actor.fuel,
            tick < 3 ? (float)(2 - (int)tick) : 0);
        equal("jet thrust stops at empty tank", actor.force.y, tick < 3 ? SRC_JETSPEED : 0);
    }
    for (uint64_t tick = 20; tick < 40; tick++)
        movement_step(&actor, (Input){0}, tick);
    equal("airborne jet regeneration every other tick", (float)actor.fuel, 10);

    actor = airborne();
    actor.contact = GROUNDED;
    movement_step(&actor, (Input){.held = INPUT_JETS}, 0);
    equal("grounded jet takeoff thrust", actor.force.y, 2.5f * SRC_JETSPEED);

    actor = airborne();
    movement_step(&actor, (Input){.forward = 1, .held = INPUT_JUMP | INPUT_JETS}, 0);
    equal("source jet and jump combination suspends air steering", actor.force.z, 0);
    movement_step(&actor, (Input){.forward = 1, .held = INPUT_JETS}, 1);
    equal("releasing jump restores jet air steering", actor.force.z, SRC_FLYSPEED);
    equal("releasing jump retains jet lift", actor.force.y, SRC_JETSPEED);

    actor = airborne();
    actor.pose = PRONE;
    actor.animation = MOVE_PRONE;
    actor.animation_tick = 26;
    movement_step(&actor, (Input){.held = INPUT_JETS}, 0);
    equal("prone jet has no upward thrust", actor.force.y, 0);
    equal("prone jet horizontal thrust", actor.force.z, SRC_JETSPEED / 2);

    Actor straight = airborne();
    Actor diagonal = airborne();
    straight.contact = GROUNDED;
    diagonal.contact = GROUNDED;
    movement_step(&straight, (Input){.forward = 1}, 0);
    movement_step(&diagonal, (Input){.forward = 1, .right = 1}, 0);
    equal("3D diagonal input preserves run force", length(diagonal.force), length(straight.force));
    equal("ground run force", straight.force.z, SRC_RUNSPEED);
    equal("ground run upward force", straight.force.y, SRC_RUNSPEEDUP);
    movement_step(&straight, (Input){.forward = 1}, 1);
    equal("air control force", straight.force.z, SRC_FLYSPEED);

    actor = airborne();
    movement_step(&actor, (Input){.right = 1}, 0);
    equal("camera right at yaw zero points negative X", actor.force.x, -SRC_FLYSPEED);
    equal("camera right at yaw zero preserves Z", actor.force.z, 0);
    actor = airborne();
    movement_step(&actor, (Input){.right = 1, .yaw = acosf(-1) / 2}, 0);
    equal("camera right at yaw ninety preserves X", actor.force.x, 0);
    equal("camera right at yaw ninety points positive Z", actor.force.z, SRC_FLYSPEED);
    actor = airborne();
    movement_step(&actor, (Input){.forward = 1, .yaw = acosf(-1) / 2}, 0);
    equal("camera forward at yaw ninety points positive X", actor.force.x, SRC_FLYSPEED);
    equal("camera forward at yaw ninety preserves Z", actor.force.z, 0);

    actor = airborne();
    actor.velocity = v3(10, 0, 10);
    movement_step(&actor, (Input){0}, 0);
    equal("3D horizontal speed cap preserves original maximum",
        sqrtf(actor.velocity.x * actor.velocity.x + actor.velocity.z * actor.velocity.z),
        SRC_MAX_VELOCITY);

    actor = airborne();
    actor.position = add(world_nav_nodes[0].position,v3(0,30,0));
    for (uint64_t tick = 0; tick < 120; tick++)
        movement_step(&actor, (Input){0}, tick);
    equal("arena floor supports player", (float)actor.contact, GROUNDED);
    actor.fuel = 0;
    for (uint64_t tick = 120; tick < 130; tick++)
        movement_step(&actor, (Input){0}, tick);
    equal("grounded fuel regenerates every tick", (float)actor.fuel, 10);
    movement_step(&actor, (Input){.forward = 1}, 130);
    movement_step(&actor, (Input){.forward = 1}, 131);
    equal("ground run applies both source damping factors", actor.velocity.z,
        SRC_RUNSPEED * SRC_EDAMPING * SRC_SURFACECOEFX);
    movement_step(&actor, (Input){0}, 132);
    equal("standing stops horizontal ground momentum", length(actor.velocity), 0);

    Vec3 ceiling_probe=add(world_nav_nodes[0].position,v3(0,7,0));
    WorldHit ceiling=world_trace(ceiling_probe,add(ceiling_probe,v3(0,10000,0)),v3(0,0,0));
    equal("enclosed arena has a physical ceiling",ceiling.box>=0,1);
    Vec3 under_ceiling=add(ceiling_probe,v3(0,10000*ceiling.fraction-8,0));
    actor = airborne();
    actor.position = under_ceiling;
    actor.pose = PRONE;
    actor.animation = MOVE_PRONE;
    actor.animation_tick = 26;
    movement_step(&actor, (Input){.pressed = INPUT_PRONE}, 0);
    equal("underpass ceiling blocks standing from prone", actor.pose, PRONE);
    equal("blocked standing retains prone movement state", actor.animation, MOVE_PRONE);
    equal("blocked standing leaves player outside roof", world_pose_clear(actor.position, actor.pose), 1);
    actor.position = v3(0, 10000, 0);
    movement_step(&actor, (Input){.pressed = INPUT_PRONE}, 1);
    equal("standing succeeds after clearing roof", actor.pose, STANDING);

    actor = airborne();
    actor.position = sub(under_ceiling,v3(0,3,0));
    actor.pose = CROUCHING;
    actor.animation = MOVE_CROUCH;
    movement_step(&actor, (Input){0}, 0);
    equal("underpass ceiling blocks standing from crouch", actor.pose, CROUCHING);
    equal("blocked standing leaves crouching player outside roof", world_pose_clear(actor.position, actor.pose), 1);

    world_free();

    puts("movement contracts passed");
    return EXIT_SUCCESS;
}
