#include "world.h"
#include "generated_rules.h"

static void animate(Actor *actor, MoveAnimation animation, int frame) {
    if (actor->animation == animation) return;
    actor->animation = animation;
    actor->animation_tick = frame;
}

unsigned movement_step(Actor *actor, Input input, uint64_t tick) {
    actor->previous = actor->position;
    actor->velocity = add(actor->velocity, actor->force);
    actor->velocity.y -= SRC_GRAV;
    actor->position = add(actor->position, actor->velocity);
    actor->velocity = scale(actor->velocity, SRC_EDAMPING);
    actor->force = v3(0, 0, 0);
    actor->yaw = input.yaw;
    actor->pitch = input.pitch;

    Vec3 facing = direction(input.yaw, 0);
    Vec3 movement = v3(-input.right * cosf(input.yaw) + input.forward * sinf(input.yaw),
        0, input.forward * cosf(input.yaw) + input.right * sinf(input.yaw));
    float amount = length(movement);
    if (amount > 1) movement = scale(movement, 1 / amount);

    if ((input.held & INPUT_JETS) &&
        ((actor->animation == MOVE_SIDEJUMP && dot(movement, facing) < 0) ||
        (actor->animation == MOVE_ROLLBACK && (input.held & INPUT_JUMP)))) {
        actor->move_direction = scale(facing, -1);
        animate(actor, MOVE_ROLLBACK, 1);
    } else if ((input.held & INPUT_JETS) && actor->fuel > 0) {
        if (actor->contact == GROUNDED) {
            actor->force.y = 2.5f * SRC_JETSPEED;
        } else if (actor->pose == PRONE) {
            actor->force = add(actor->force, scale(facing, SRC_JETSPEED / 2));
        } else {
            actor->force.y += SRC_JETSPEED;
        }
        if (actor->animation != MOVE_GETUP && actor->animation != MOVE_ROLL &&
            actor->animation != MOVE_ROLLBACK && actor->pose != PRONE)
            animate(actor, MOVE_IDLE, 1);
        actor->fuel--;
    }

    if (input.pressed & INPUT_PRONE) {
        if (actor->pose != PRONE && actor->animation != MOVE_GETUP) {
            animate(actor, MOVE_PRONE, 1);
        } else if (actor->animation == MOVE_PRONEMOVE ||
            (actor->animation == MOVE_PRONE && actor->animation_tick > 23)) {
            animate(actor, MOVE_GETUP, 9);
        }
    }

    if (actor->animation == MOVE_GETUP) {
        if (actor->animation_tick > 20 && actor->contact == GROUNDED &&
            (input.held & INPUT_JUMP)) {
            int frame = actor->animation_tick;
            animate(actor, amount > 0 ? MOVE_SIDEJUMP : MOVE_JUMP,
                frame - (amount > 0 ? 20 : 15));
        } else if (actor->animation_tick > 23) {
            animate(actor, amount > 0 ? MOVE_RUN : MOVE_IDLE, 1);
        }
    }

    if (actor->contact == GROUNDED &&
        ((input.pressed & INPUT_ROLL) ||
        ((input.held & INPUT_CROUCH) && amount > 0 && actor->animation == MOVE_RUN))) {
        actor->move_direction = amount > 0 ? scale(movement, 1 / length(movement)) : facing;
        animate(actor, dot(actor->move_direction, facing) < 0 ? MOVE_ROLLBACK : MOVE_ROLL, 1);
    }

    if (actor->animation == MOVE_ROLL || actor->animation == MOVE_ROLLBACK) {
        float force = actor->contact == GROUNDED ? SRC_ROLLSPEED : 2 * SRC_FLYSPEED;
        if (actor->animation_tick == 1 && actor->contact == GROUNDED)
            force = 2 * SRC_CROUCHRUNSPEED;
        actor->force.x = actor->move_direction.x * force;
        actor->force.z = actor->move_direction.z * force;
        if (actor->animation == MOVE_ROLLBACK && actor->animation_tick > 1 &&
            actor->animation_tick < 8 && (input.held & INPUT_JUMP)) {
            actor->force.y += SRC_JUMPDIRSPEED * 1.5f;
            actor->force.x *= 0.5f;
            actor->force.z *= 0.5f;
            actor->velocity.x *= 0.8f;
            actor->velocity.z *= 0.8f;
        }
        int frames = actor->animation == MOVE_ROLL ? SRC_ROLL_FRAMES : SRC_ROLLBACK_FRAMES;
        if (actor->animation_tick >= frames)
            animate(actor, (input.held & INPUT_CROUCH) ? MOVE_CROUCH :
                (amount > 0 ? MOVE_RUN : MOVE_IDLE), 1);
    } else if (actor->animation == MOVE_PRONE || actor->animation == MOVE_PRONEMOVE) {
        if (actor->contact == GROUNDED &&
            (actor->animation == MOVE_PRONEMOVE || actor->animation_tick > 25)) {
            if (amount > 0) {
                animate(actor, MOVE_PRONEMOVE, 1);
                int frame = (actor->animation_tick + 1) / 2;
                if (frame < 4 || frame > 14) {
                    actor->force.x = movement.x * SRC_PRONESPEED;
                    actor->force.z = movement.z * SRC_PRONESPEED;
                }
            } else {
                animate(actor, MOVE_PRONE, 26);
            }
        }
    } else if (actor->animation != MOVE_GETUP) {
        if (input.held & INPUT_CROUCH) {
            if (actor->contact == GROUNDED) {
                animate(actor, MOVE_CROUCH, 1);
                actor->force.x = movement.x * SRC_CROUCHRUNSPEED;
                actor->force.z = movement.z * SRC_CROUCHRUNSPEED;
            }
        } else if (input.held & INPUT_JUMP) {
            if (amount > 0) {
                if ((actor->contact == GROUNDED && actor->animation != MOVE_SIDEJUMP) ||
                    (actor->animation == MOVE_JUMP && actor->animation_tick < 10))
                    animate(actor, MOVE_SIDEJUMP, 1);
                if (actor->animation == MOVE_SIDEJUMP && actor->animation_tick > 3 &&
                    actor->animation_tick < 11) {
                    actor->force.x = movement.x * SRC_JUMPDIRSPEED;
                    actor->force.z = movement.z * SRC_JUMPDIRSPEED;
                    actor->force.y = SRC_JUMPDIRSPEED / 1.2f;
                }
            } else {
                if (actor->contact == GROUNDED)
                    animate(actor, MOVE_JUMP, 1);
                if (actor->animation == MOVE_JUMP && actor->animation_tick > 8 &&
                    actor->animation_tick < 15)
                    actor->force.y = SRC_JUMPSPEED;
            }
        } else if (amount > 0) {
            animate(actor, MOVE_RUN, 1);
            float force = actor->contact == GROUNDED ? SRC_RUNSPEED : SRC_FLYSPEED;
            actor->force.x = movement.x * force;
            actor->force.z = movement.z * force;
            if (actor->contact == GROUNDED) actor->force.y = SRC_RUNSPEEDUP;
        } else {
            animate(actor, MOVE_IDLE, 1);
        }
    }

    Pose pose = actor->animation == MOVE_CROUCH ? CROUCHING : STANDING;
    if (actor->animation == MOVE_PRONE || actor->animation == MOVE_PRONEMOVE)
        pose = PRONE;
    if (actor_height(pose) > actor_height(actor->pose) &&
        !world_pose_clear_for(actor->previous,pose,(WorldQuery){WORLD_TRACE_ACTOR,actor->team,actor->carried_flag!=FLAG_NONE ? WORLD_HAS_FLAG : WORLD_NO_FLAG})) {
        pose = actor->pose;
        animate(actor, pose == PRONE ? MOVE_PRONE : MOVE_CROUCH, pose == PRONE ? 26 : 1);
    }
    actor->pose = pose;

    actor->animation_tick++;
    if (actor->animation == MOVE_PRONE && actor->animation_tick > 26)
        actor->animation_tick = 26;
    if (actor->animation == MOVE_PRONEMOVE && actor->animation_tick > 2 * SRC_PRONEMOVE_FRAMES)
        actor->animation_tick = 1;
    if ((actor->animation == MOVE_JUMP && actor->animation_tick > SRC_JUMP_FRAMES) ||
        (actor->animation == MOVE_SIDEJUMP && actor->animation_tick > SRC_SIDEJUMP_FRAMES))
        animate(actor, amount > 0 ? MOVE_RUN : MOVE_IDLE, 1);

    actor->contact = world_move(actor);
    unsigned surface=world_contact_type(actor);
    if (actor->contact == GROUNDED && surface!=4 && surface!=18) {
        if (actor->animation == MOVE_IDLE || actor->animation == MOVE_PRONEMOVE ||
            (actor->animation == MOVE_CROUCH && amount == 0) ||
            (actor->animation == MOVE_PRONE && actor->animation_tick > 24)) {
            actor->velocity = v3(0, 0, 0);
        } else {
            float friction = actor->animation == MOVE_CROUCH
                ? SRC_CROUCHMOVESURFACECOEFX : SRC_SURFACECOEFX;
            actor->velocity.x *= friction;
            actor->velocity.z *= friction;
            actor->velocity.y *= SRC_SURFACECOEFX;
        }
    }

    if (actor->fuel < actor->fuel_capacity && !(input.held & INPUT_JETS) &&
        (actor->contact == GROUNDED || tick % 2 == 0))
        actor->fuel++;

    float horizontal_speed = sqrtf(actor->velocity.x * actor->velocity.x +
        actor->velocity.z * actor->velocity.z);
    if (horizontal_speed > SRC_MAX_VELOCITY) {
        actor->velocity.x *= SRC_MAX_VELOCITY / horizontal_speed;
        actor->velocity.z *= SRC_MAX_VELOCITY / horizontal_speed;
    }
    actor->velocity.y = fmaxf(-SRC_MAX_VELOCITY, fminf(SRC_MAX_VELOCITY, actor->velocity.y));
    return surface;
}
