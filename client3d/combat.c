#include "game.h"
#include "pose.h"
#include "pickups.h"
#include "ragdoll.h"
#include "objectives.h"
#include "world.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>

void combat_event(Game *game, GameEvent event) {
    if (game->event_count == game->event_capacity) {
        size_t capacity = game->event_capacity ? game->event_capacity * 2 : 64;
        GameEvent *events = realloc(game->events, capacity * sizeof(*events));
        if (!events) {
            perror("realloc combat events");
            abort();
        }
        game->events = events;
        game->event_capacity = capacity;
    }
    game->events[game->event_count++] = event;
    ++game->next_event_id;
}

static WorldHit bullet_trace(Vec3 start, Vec3 end, Team team) {
    return world_trace_for(start,end,v3(0,0,0),(WorldQuery){WORLD_TRACE_BULLET,team,WORLD_NO_FLAG});
}

static void projectile_create(Game *game, int owner, WeaponId weapon, Vec3 origin, Vec3 velocity, Rewind rewind) {
    if (game->projectile_count == game->projectile_capacity) {
        size_t capacity = game->projectile_capacity ? game->projectile_capacity * 2 : 64;
        Projectile *projectiles = realloc(game->projectiles, capacity * sizeof(*projectiles));
        if (!projectiles) {
            perror("realloc projectiles");
            abort();
        }
        game->projectiles = projectiles;
        game->projectile_capacity = capacity;
    }
    game->projectiles[game->projectile_count++] = (Projectile){
        .position = origin, .previous = origin, .initial = origin, .hit_spot = origin, .velocity = velocity,
        .owner = owner, .weapon = weapon, .ticks = weapons[weapon].timeout,
        .hit_multiply = weapons[weapon].hit_multiply, .rewind = rewind,
        .id = ++game->next_projectile_id
    };
}

Vec3 actor_muzzle(const Actor *actor) {
    Vec3 points[21];
    actor_pose(actor,points);
    Vec3 shoulder=scale(add(points[10],points[11]),.5f);
    Vec3 barrel=pose_muzzle(actor,points);
    WorldHit hit=bullet_trace(shoulder,barrel,actor->team);
    return add(shoulder,scale(sub(barrel,shoulder),hit.fraction));
}

static int accumulate_bink(int accumulated, int bink) {
    return accumulated + bink - (int)nearbyintf((float)(accumulated * accumulated) /
                                               (float)(10 * bink + accumulated));
}

static void damage_actor(Game *game, int target, int owner, WeaponId weapon, float amount, int bone) {
    Actor *actor = &game->actors[target];
    if (actor->life == INACTIVE || actor->bonus == BONUS_FLAMEGOD) return;
    if (owner != target && game_team_mode(game->mode) && !game->friendly_fire &&
        actor->team == game->actors[owner].team) return;
    if (game->mode == MODE_RAMBO && owner != target) {
        for (int i = 0; i < ACTOR_COUNT; ++i) {
            WeaponId id = game->actors[i].slots[game->actors[i].active_slot].id;
            if (i != owner && i != target && game->actors[i].life == ALIVE && (id == BOW || id == BOW2)) return;
        }
    }
    float damage = amount;
    if (actor->vest > 0) {
        actor->vest -= nearbyintf(.33f * amount);
        damage = nearbyintf(.25f * amount);
    }
    if (owner != target && game->actors[owner].bonus == BONUS_BERSERKER) damage = 4 * amount;
    actor->health -= damage;
    if (actor->life == DEAD) {
        ragdoll_dismember(actor, bone);
        if (game->actors[owner].bonus==BONUS_BERSERKER)
            actor->ragdoll.severed|=(1u<<1)|(1u<<3)|(1u<<19)|(1u<<20)|(1u<<22);
        return;
    }
    if (amount > 0) {
        actor->hit_ticks = 8;
        int bink = weapons[actor->slots[actor->active_slot].id].bink;
        if (bink > 0) actor->bink_count = accumulate_bink(actor->bink_count, bink);
        combat_event(game, (GameEvent){EVENT_HIT, actor->position, owner, target, weapon});
    }
    if (actor->health >= 1) return;
    ragdoll_start(actor);
    actor->life = DEAD;
    ragdoll_dismember(actor, bone);
    if (game->actors[owner].bonus==BONUS_BERSERKER)
        actor->ragdoll.severed|=(1u<<1)|(1u<<3)|(1u<<19)|(1u<<20)|(1u<<22);
    objectives_kill(game, target, owner, weapon);
    actor->deaths++;
    combat_event(game, (GameEvent){EVENT_KILL, actor->position, owner, target, weapon});
    pickups_drop_weapon(game, target);
    actor->vest = 0;
    actor->bonus = BONUS_NONE;
    actor->bonus_ticks = 0;
}

void combat_environment(Game *game,int index,unsigned type) {
    Actor *actor=&game->actors[index];
    switch (type) {
        case 5: damage_actor(game,index,index,NOWEAPON,50+actor->health,12);break;
        case 6: damage_actor(game,index,index,NOWEAPON,450+actor->health,12);break;
        case 7: case 9:
            if ((int)(game_random(game)*10)==0) actor->health-=5;
            if (actor->health<1) damage_actor(game,index,index,NOWEAPON,10,12);
            if (type==9 && (int)(game_random(game)*3)==0 && (int)(game_random(game)*3)==0)
                projectile_create(game,index,FLAMER,add(actor->position,v3(0,3,0)),scale(actor->velocity,-1),(Rewind){0});
            break;
        case 8:
            if (actor->health<SRC_DEFAULT_HEALTH && game->tick%12==0)
                damage_actor(game,index,index,NOWEAPON,-2,12);
            break;
        case 19:
            projectile_create(game,index,M79,add(actor->position,v3(0,3,0)),v3(0,0,0),(Rewind){0});
            damage_actor(game,index,index,M79,4000,12);
            actor->health=-600;
            if (actor->life==DEAD) ragdoll_dismember(actor,12);
            break;
        case 20:
            if (actor->carried_flag != FLAG_NONE && (int)(game_random(game)*10)==0) actor->health-=10;
            if (actor->health<1) damage_actor(game,index,index,NOWEAPON,10,12);
            break;
        default: break;
    }
}

enum { SOURCE_PARTS=7, HIT_PARTS=11 };
typedef enum { HIT_SPHERE,HIT_CAPSULE } HitShape;
typedef struct { Vec3 center,end;float radius,modifier;int bone;HitShape shape; } BodyPoint;

static void body_points(Life life, Vec3 aim, const WeaponDef *weapon, const Vec3 bones[21],
    BodyPoint points[HIT_PARTS]) {
    const int indices[SOURCE_PARTS]={12,11,10,6,5,4,3};
    for (int i=0;i<SOURCE_PARTS;i++) {
        points[i]=(BodyPoint){.center=bones[indices[i]],.end=bones[indices[i]],.radius=SRC_PART_RADIUS,
            .shape=HIT_SPHERE};
        points[i].bone=indices[i];
        points[i].modifier=i==0 ? weapon->modifier_head :
                           i<5 ? weapon->modifier_chest : weapon->modifier_legs;
    }
    for(int side=0;side<2;++side) {
        int elbow=side ? 14 : 13,wrist=side ? 15 : 16;
        Vec3 forearm=sub(bones[wrist],bones[elbow]);
        Vec3 hand=life==DEAD ? sub(bones[side ? 19 : 20],bones[wrist]) : aim;
        hand=scale(hand,1/length(hand));
        points[SOURCE_PARTS+2*side]=(BodyPoint){
            .center=sub(bones[elbow],scale(forearm,ACTOR_FOREARM_OVERLAP/length(forearm))),
            .end=bones[wrist],.radius=ACTOR_FOREARM_RADIUS,.modifier=weapon->modifier_chest,
            .bone=elbow,.shape=HIT_CAPSULE};
        points[SOURCE_PARTS+2*side+1]=(BodyPoint){
            .center=sub(bones[wrist],scale(hand,ACTOR_HAND_BACK)),
            .end=add(bones[wrist],scale(hand,ACTOR_HAND_FRONT)),.radius=ACTOR_HAND_RADIUS,
            .modifier=weapon->modifier_chest,.bone=wrist,.shape=HIT_CAPSULE};
    }
}

enum { HISTORY_FRAMES = 2 * SRC_MAX_OLDPOS + 1 };
typedef struct {
    Vec3 bones[21];
    float yaw, pitch;
    uint32_t spawn_id;
    Life life;
    int protected;
} HistoricalActor;
typedef struct { uint64_t tick; HistoricalActor actors[ACTOR_COUNT]; } HistoryFrame;
struct CombatHistory { HistoryFrame frames[HISTORY_FRAMES]; };

void combat_history_record(Game *game) {
    if (!game->history) return;
    HistoryFrame *frame = &game->history->frames[game->tick % HISTORY_FRAMES];
    frame->tick = game->tick;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        const Actor *actor = &game->actors[i];
        HistoricalActor *saved = &frame->actors[i];
        saved->spawn_id = actor->spawn_id;
        saved->life = actor->life;
        saved->protected = actor->life == ALIVE && actor->spawn_protection_ticks >= 0;
        saved->yaw = actor->yaw;
        saved->pitch = actor->pitch;
        if (actor->life != INACTIVE) actor_pose(actor, saved->bones);
    }
}

void combat_history_enable(Game *game) {
    if (game->history) return;
    game->history = calloc(1, sizeof(*game->history));
    if (!game->history) abort();
    combat_history_record(game);
}

static int historical_body(const Game *game, int target, const WeaponDef *weapon,
    Rewind rewind, BodyPoint points[HIT_PARTS]) {
    const Actor *actor = &game->actors[target];
    Vec3 bones[21];
    if (rewind.mode == REWIND_NONE) {
        actor_pose(actor, bones);
        body_points(actor->life, direction(actor->yaw, actor->pitch), weapon, bones, points);
        return 1;
    }
    if (!game->history) {
        fprintf(stderr, "Rendered rewind requires combat history\n");
        abort();
    }
    uint64_t before = rewind.before_tick, after = rewind.after_tick;
    float fraction = rewind.fraction;
    uint64_t elapsed = game->tick + 1 - rewind.applied_tick;
    double advance = (double)(after - before) * fraction + (double)elapsed;
    if (advance > (double)(after - before)) {
        uint64_t whole = (uint64_t)floor(advance);
        before += whole;
        after = before + (advance > (double)whole);
        fraction = (float)(advance - whole);
    } else if (before != after) fraction = (float)((double)fraction + (double)elapsed / (after - before));
    const HistoryFrame *a = &game->history->frames[before % HISTORY_FRAMES];
    const HistoryFrame *b = &game->history->frames[after % HISTORY_FRAMES];
    if (a->tick != before || b->tick != after) return 0;
    const HistoricalActor *left = &a->actors[target], *right = &b->actors[target];
    if (left->spawn_id != actor->spawn_id || right->spawn_id != actor->spawn_id ||
        left->life != actor->life || right->life != actor->life || left->protected || right->protected) return 0;
    for (int i = 1; i <= 20; ++i)
        bones[i] = add(left->bones[i], scale(sub(right->bones[i], left->bones[i]), fraction));
    float yaw = left->yaw + atan2f(sinf(right->yaw - left->yaw), cosf(right->yaw - left->yaw)) * fraction;
    float pitch = left->pitch + (right->pitch - left->pitch) * fraction;
    body_points(actor->life, direction(yaw, pitch), weapon, bones, points);
    return 1;
}

static float sphere_fraction(Vec3 start, Vec3 delta, Vec3 center, float radius) {
    Vec3 relative = sub(start, center);
    float a = dot(delta, delta);
    float b = dot(relative, delta);
    float c = dot(relative, relative) - radius * radius;
    if (a == 0) return c <= 0 ? 0 : INFINITY;
    float discriminant = b * b - a * c;
    if (discriminant < 0) return INFINITY;
    float contact = c <= 0 ? 0 : (-b - sqrtf(discriminant)) / a;
    return contact < 0 ? INFINITY : contact;
}

static float body_fraction(Vec3 start,Vec3 delta,const BodyPoint *part,float expansion) {
    float radius=part->radius+expansion;
    float fraction=sphere_fraction(start,delta,part->center,radius);
    if(part->shape==HIT_SPHERE)return fraction;
    fraction=fminf(fraction,sphere_fraction(start,delta,part->end,radius));
    Vec3 axis=sub(part->end,part->center),offset=sub(start,part->center);
    float squared=dot(axis,axis);
    if(squared==0)return fraction;
    float along=dot(axis,offset),projection=dot(axis,delta);
    float a=squared*dot(delta,delta)-projection*projection;
    float b=squared*dot(delta,offset)-along*projection;
    float c=squared*dot(offset,offset)-along*along-radius*radius*squared;
    if(along>=0 && along<=squared && c<=0)return 0;
    float discriminant=b*b-a*c;
    if(a>0 && discriminant>=0) {
        float contact=(-b-sqrtf(discriminant))/a;
        float position=along+contact*projection;
        if(contact>=0 && position>=0 && position<=squared)fraction=fminf(fraction,contact);
    }
    return fraction;
}

Vec3 combat_aim_target(const Game *game, int shooter, Vec3 start, Vec3 end, const Vec3 poses[ACTOR_COUNT][21]) {
    Vec3 delta = sub(end, start);
    float fraction = bullet_trace(start,end,game->actors[shooter].team).fraction;
    const Actor *actor = &game->actors[shooter];
    const WeaponDef *weapon = &weapons[actor->slots[actor->active_slot].id];
    for (int i = 0; i < ACTOR_COUNT; i++) {
        if (i == shooter || game->actors[i].life != ALIVE || game->actors[i].spawn_protection_ticks >= 0) continue;
        BodyPoint points[HIT_PARTS];
        Vec3 bones[21];
        if (!poses) actor_pose(&game->actors[i], bones);
        body_points(game->actors[i].life, direction(game->actors[i].yaw, game->actors[i].pitch),
            weapon, poses ? poses[i] : bones, points);
        for (int part = 0; part < HIT_PARTS; part++)
            fraction = fminf(fraction, body_fraction(start,delta,&points[part],0));
    }
    return add(start, scale(delta, fraction));
}

static void explode(Game *game, Projectile projectile, WeaponId damage_weapon, float radius) {
    const WeaponDef *weapon = &weapons[damage_weapon];
    combat_event(game, (GameEvent){EVENT_EXPLOSION, projectile.position,
                                 projectile.owner, -1, projectile.weapon});
    for (int i=0;i<3;++i) {
        Flag *flag=&game->flags[i];
        if (flag->state==FLAG_ABSENT || (game->mode==MODE_INF && i==0)) continue;
        Vec3 offset=sub(flag->position,projectile.position);
        float distance=length(offset);
        if (distance<radius) flag->velocity=add(flag->velocity,
            scale(offset,.5f*SRC_EXPLOSION_IMPACT_MULTIPLY/(distance+1)));
    }
    for (int i = 0; i < ACTOR_COUNT; i++) {
        Actor *actor = &game->actors[i];
        if (actor->life != ALIVE) continue;
        BodyPoint points[HIT_PARTS];
        if (!historical_body(game, i, weapon, projectile.rewind, points)) continue;
        int nearest = 0;
        float distance = length(sub(projectile.position, points[0].center));
        for (int j = 1; j < SOURCE_PARTS; j++) {
            float candidate = length(sub(projectile.position, points[j].center));
            if (candidate < distance) {
                distance = candidate;
                nearest = j;
            }
        }
        if (distance >= radius) continue;
        Vec3 push = scale(sub(points[nearest].center, projectile.position),
                          SRC_EXPLOSION_IMPACT_MULTIPLY / (distance + 1));
        float multiplier = points[nearest].modifier;
        if (projectile.weapon == CLUSTER || projectile.weapon == M2) multiplier *= 0.5f;
        else push.y *= 2;
        actor->velocity = add(actor->velocity, push);
        if (actor->spawn_protection_ticks < 0)
            damage_actor(game, i, projectile.owner, projectile.weapon,
                         weapon->hit_multiply * multiplier / (distance + 1), points[nearest].bone);
    }
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        Actor *actor=&game->actors[i];
        if (actor->life!=DEAD) continue;
        ragdoll_explosion(actor,projectile.position,radius);
        float last_distance=-1;
        for (int bone=1;bone<=16;++bone) {
            float distance=length(sub(projectile.position,actor->ragdoll.position[bone]));
            if (distance<radius) last_distance=distance;
        }
        if (last_distance<0) continue;
        if (damage_weapon==M79) last_distance=fmaxf(last_distance,20.0000001f);
        float multiplier=projectile.weapon==CLUSTER || projectile.weapon==M2?.5f:1;
        damage_actor(game,i,projectile.owner,projectile.weapon,
            weapon->hit_multiply*multiplier/(last_distance+1),1);
    }
}

WeaponSpread combat_spread(const Actor *actor, uint32_t held) {
    const WeaponState *state = &actor->slots[actor->active_slot];
    const WeaponDef *weapon = &weapons[state->id];
    float spread = 0;
    if (state->id != EAGLE && state->id != SPAS12) {
        spread = weapon->bullet_spread;
        if (actor->pose == PRONE) spread /= 1.625f;
        else if (actor->pose == CROUCHING) spread /= 1.3f;
    }
    float moveacc = 0;
    if (((held & INPUT_JETS) && actor->fuel > 0) || actor->animation == MOVE_RUN ||
        actor->animation == MOVE_JUMP || actor->animation == MOVE_SIDEJUMP ||
        actor->animation == MOVE_ROLL || actor->animation == MOVE_ROLLBACK)
        moveacc = weapon->movement_acc * 7;
    else if ((actor->contact == AIRBORNE && actor->pose == STANDING) ||
             actor->animation == MOVE_GETUP)
        moveacc = weapon->movement_acc * 3;
    float inaccuracy = fminf(SRC_MAX_INACCURACY,
                            0.25f * (actor->bink_count * 0.01f + moveacc + spread));
    float deviation = SRC_MAX_INACCURACY * sinf(inaccuracy / SRC_MAX_INACCURACY * 1.57079632679f);
    float pellet_angle = state->id == SPAS12 ? atanf(weapon->bullet_spread / weapon->speed) :
        state->id == EAGLE ? asinf(sqrtf(3) * weapon->bullet_spread / weapon->speed) : 0;
    return (WeaponSpread){deviation, pellet_angle, atan2f(deviation, 1 - deviation) + pellet_angle};
}

static void fire_weapon(Game *game, int index, Input input) {
    Actor *actor = &game->actors[index];
    WeaponState *state = &actor->slots[actor->active_slot];
    const WeaponDef *weapon = &weapons[state->id];
    Vec3 forward = direction(actor->yaw, actor->pitch);
    Vec3 right = direction(actor->yaw - 1.57079632679f, 0);
    Vec3 origin = actor_muzzle(actor);
    WeaponSpread spread = combat_spread(actor, input.held);
    float deviation = spread.aim_deviation;
    float axial = 1 + (game_random(game) * 2 - 1) * deviation;
    float radial = (game_random(game) * 2 - 1) * deviation;
    float azimuth = game_random(game) * 6.28318530718f;
    Vec3 up = v3(-sinf(actor->pitch) * sinf(actor->yaw), cosf(actor->pitch),
        -sinf(actor->pitch) * cosf(actor->yaw));
    Vec3 tangent = add(scale(right, cosf(azimuth)), scale(up, sinf(azimuth)));
    Vec3 aim = add(scale(forward, axial), scale(tangent, radial));
    Vec3 velocity = add(scale(aim, weapon->speed / length(aim)),
                        scale(actor->velocity, weapon->inherited_velocity));
    Vec3 pellet_right = right, pellet_up = up;
    if (state->id == SPAS12) {
        aim = scale(aim, 1 / length(aim));
        pellet_right = sub(right, scale(aim, dot(right, aim)));
        pellet_right = scale(pellet_right, 1 / length(pellet_right));
        pellet_up = v3(pellet_right.y * aim.z - pellet_right.z * aim.y,
            pellet_right.z * aim.x - pellet_right.x * aim.z, pellet_right.x * aim.y - pellet_right.y * aim.x);
    }
    int pellets = state->id == SPAS12 ? 6 : state->id == EAGLE ? 2 : 1;
    for (int i = 0; i < pellets; i++) {
        Vec3 pellet = velocity;
        Vec3 muzzle = origin;
        if (state->id == SPAS12) {
            float radius = tanf(spread.pellet_angle) * sqrtf(game_random(game));
            float angle = game_random(game) * 6.28318530718f;
            Vec3 offset = add(scale(pellet_right, radius * cosf(angle)), scale(pellet_up, radius * sinf(angle)));
            pellet = add(scale(add(aim, offset), weapon->speed / sqrtf(1 + radius * radius)),
                scale(actor->velocity, weapon->inherited_velocity));
        } else if (state->id == EAGLE)
            pellet = add(pellet, v3((game_random(game) * 2 - 1) * weapon->bullet_spread,
                                   (game_random(game) * 2 - 1) * weapon->bullet_spread,
                                   (game_random(game) * 2 - 1) * weapon->bullet_spread));
        if (state->id == EAGLE && i == 1) muzzle = add(muzzle, scale(right, -3));
        if (state->id == KNIFE || state->id == NOWEAPON) {
            muzzle = add(origin, scale(forward, 10));
            pellet = scale(forward, 0.1f);
        } else if (state->id == CHAINSAW || state->id == FLAMER) {
            muzzle = add(muzzle, scale(velocity, 2));
        }
        Vec3 bones[21];
        actor_pose(actor,bones);
        Vec3 shoulder=scale(add(bones[10],bones[11]),.5f);
        WorldHit obstruction=bullet_trace(shoulder,muzzle,actor->team);
        Vec3 barrel = sub(muzzle, shoulder);
        float clearance = obstruction.fraction;
        for (int target = 0; target < ACTOR_COUNT; ++target) {
            const Actor *other = &game->actors[target];
            if (target == index || other->life == INACTIVE || (other->life == ALIVE && other->spawn_protection_ticks >= 0)) continue;
            BodyPoint points[HIT_PARTS];
            if (!historical_body(game, target, weapon, input.rewind, points)) continue;
            for (int part = 0; part < HIT_PARTS; ++part)
                clearance = fminf(clearance, body_fraction(shoulder,barrel,&points[part],0));
        }
        muzzle=add(shoulder,scale(barrel,clearance));
        projectile_create(game, index, state->id, muzzle, pellet, input.rewind);
    }
    if (state->id == SPAS12)
        actor->velocity = sub(actor->velocity, v3(velocity.x * 0.0412f,
                                                 velocity.y * 0.041f, velocity.z * 0.0412f));
    if (state->id == MINIGUN) {
        int jetting = (input.held & INPUT_JETS) && actor->fuel > 0;
        actor->velocity = sub(actor->velocity, v3(velocity.x * (jetting ? 0.0012f : 0.0082f) * 0.6f,
            velocity.y * (jetting ? 0.0009f : 0.0078f), velocity.z * (jetting ? 0.0012f : 0.0082f) * 0.6f));
    }
    combat_event(game, (GameEvent){EVENT_SHOT, origin, index, -1, state->id});
    if (state->id == KNIFE || state->id == NOWEAPON) return;
    state->ammo--;
    state->fire_count = weapon->fire_interval;
    state->phase = WEAPON_READY;
    state->reload_count = weapon->reload_time;
    actor->burst_count++;
    if (weapon->bink < 0)
        actor->bink_count = accumulate_bink(actor->bink_count,
            actor->pose == STANDING ? -weapon->bink : (int)nearbyintf(-weapon->bink * 0.5f));
}

static void actor_controls(Game *game, int i, Input input) {
    Actor *actor = &game->actors[i];
    size_t first_event = game->event_count;
    if (actor->spawn_protection_ticks >= 0) input.held &= ~INPUT_FIRE;
    if (actor->bink_count > 0) actor->bink_count--;
    if ((input.pressed & INPUT_SWITCH) && actor->switch_ticks == 0 && actor->bonus != BONUS_FLAMEGOD)
        actor->switch_ticks = 1;
    if (actor->switch_ticks == 25) {
        actor->active_slot = 1 - actor->active_slot;
        actor->slots[actor->active_slot].startup_count = weapons[actor->slots[actor->active_slot].id].startup_time;
        actor->burst_count = 0;
        actor->melee_frame = 0;
    }
    WeaponState *state = &actor->slots[actor->active_slot];
    const WeaponDef *weapon = &weapons[state->id];
    int rolling = actor->animation == MOVE_ROLL || actor->animation == MOVE_ROLLBACK;
    if ((input.pressed & INPUT_THROW) && !rolling && actor->switch_ticks == 0 &&
        actor->bonus != BONUS_FLAMEGOD && state->id != NOWEAPON && state->id != BOW &&
        state->id != BOW2 && !(input.held & INPUT_GRENADE)) actor->throw_frame = 1;
    if (actor->throw_frame > 0) {
        int frame = state->id == KNIFE ? (actor->throw_frame + 1) / 2 : actor->throw_frame;
        if (state->id == KNIFE && (!(input.held & INPUT_THROW) || frame == 16)) {
            float charge = fminf(16, fmaxf(8, (float)frame)) / 16;
            Vec3 velocity = add(scale(direction(actor->yaw,actor->pitch), weapons[THROWNKNIFE].speed * 1.5f * charge),
                scale(actor->velocity,weapons[THROWNKNIFE].inherited_velocity));
            Vec3 muzzle = actor_muzzle(actor);
            projectile_create(game,i,THROWNKNIFE,muzzle,velocity,input.rewind);
            pickups_weapon(actor,actor->active_slot,NOWEAPON,weapons[NOWEAPON].ammo);
            combat_event(game,(GameEvent){EVENT_SHOT,muzzle,i,-1,THROWNKNIFE});
            actor->throw_frame=0;
        } else if (state->id != KNIFE && frame == 19) {
            pickups_drop_weapon(game,i);
            actor->throw_frame=0;
        } else ++actor->throw_frame;
        state=&actor->slots[actor->active_slot];
        weapon=&weapons[state->id];
    }
    int melee = state->id == KNIFE || state->id == NOWEAPON;
    if (rolling || actor->switch_ticks > 0) actor->melee_frame = 0;
    if (melee && (input.held & INPUT_FIRE) && actor->melee_frame == 0 &&
        !rolling && actor->switch_ticks == 0 && actor->throw_frame == 0) actor->melee_frame = 1;
    if (actor->melee_frame > 0) {
        if (actor->melee_frame == 11) {
            fire_weapon(game, i, input);
            actor->melee_frame++;
        }
        if (++actor->melee_frame >= SRC_PUNCH_FRAMES) actor->melee_frame = 0;
    }
    if (!(input.held & INPUT_FIRE)) {
        if (state->id == MINIGUN && state->startup_count < weapon->startup_time) ++state->startup_count;
        else state->startup_count = weapon->startup_time;
        actor->burst_count = 0;
    }
    if (!melee && (input.held & INPUT_FIRE) && state->ammo > 0 && state->fire_count == 0 &&
        (!rolling || state->id == CHAINSAW) && actor->switch_ticks == 0 && actor->throw_frame == 0 &&
        (weapon->fire_mode != 2 || actor->burst_count == 0) &&
        (state->id != LAW || (actor->contact == GROUNDED &&
            (actor->pose == CROUCHING || actor->pose == PRONE)))) {
        if (state->startup_count > 0) state->startup_count--;
        else fire_weapon(game, i, input);
    }
    if ((input.pressed & INPUT_RELOAD) && state->ammo < weapon->ammo &&
        (!rolling || state->id == CHAINSAW)) {
        state->phase = WEAPON_RELOADING;
        if (state->id != SPAS12) {
            state->ammo = 0;
            state->reload_count = weapon->reload_time;
        } else state->reload_count = (SRC_RELOAD_FRAMES - 2) * 2 + 1;
        actor->burst_count = 0;
    }
    if (state->fire_count > 0 && (state->ammo > 0 || state->id == SPAS12)) state->fire_count--;
    if (state->id == SPAS12) {
        if (state->ammo == 0 && !(input.held & INPUT_FIRE) && state->fire_count == 0 &&
            state->phase == WEAPON_READY) {
            state->phase = WEAPON_RELOADING;
            state->reload_count = (SRC_RELOAD_FRAMES - 2) * 2 + 2;
        }
        if (state->phase == WEAPON_RELOADING && state->fire_count == 0 && !rolling &&
            actor->switch_ticks == 0 && (!(input.held & INPUT_FIRE) || state->ammo == 0)) {
            if (--state->reload_count == 0) {
                state->ammo++;
                state->reload_count = (SRC_RELOAD_FRAMES - 2) * 2;
                if (state->ammo == weapon->ammo) state->phase = WEAPON_READY;
            }
        }
    } else if (state->ammo == 0 && (!rolling || state->id == CHAINSAW) && actor->switch_ticks == 0) {
        state->phase = WEAPON_RELOADING;
        state->fire_count = weapon->fire_interval;
        if (state->id == M79 && state->reload_count == (int)(weapon->reload_time * 0.8f)) state->reload_count--;
        if (--state->reload_count == 0) {
            state->ammo = weapon->ammo;
            state->reload_count = weapon->reload_time;
            state->startup_count = weapon->startup_time;
            state->phase = WEAPON_READY;
        }
    }
    if (actor->switch_ticks > 0 && ++actor->switch_ticks > SRC_CHANGE_FRAMES) actor->switch_ticks = 0;
    if (!(input.held & INPUT_GRENADE) && actor->grenade_charge == -1) actor->grenade_charge = 0;
    if ((input.pressed & INPUT_GRENADE) && actor->grenades > 0 &&
        actor->grenade_charge == 0 && !rolling) actor->grenade_charge = 1;
    if (actor->grenade_charge > 0) {
        int charge = actor->grenade_charge;
        if (!(input.held & INPUT_GRENADE) || charge == 36) {
            if (charge >= 15 && actor->spawn_protection_ticks < 0) {
                Vec3 aim = direction(actor->yaw, actor->pitch);
                float horizontal = sqrtf(aim.x * aim.x + aim.z * aim.z);
                float arc = 0.125f * (1 - fabsf(aim.y));
                float vertical = aim.y + sinf(horizontal * 1.57079632679f) * arc;
                float adjusted_horizontal = horizontal - sinf(aim.y * 1.57079632679f) * arc;
                aim = v3(sinf(actor->yaw) * adjusted_horizontal, vertical, cosf(actor->yaw) * adjusted_horizontal);
                WeaponId grenade = actor->grenade_weapon;
                float speed = charge / weapons[grenade].speed * (charge < 24 ? 0.65f : 1);
                Vec3 velocity = add(scale(aim, speed / length(aim)),
                                    scale(actor->velocity, weapons[grenade].inherited_velocity));
                Vec3 origin = actor_muzzle(actor);
                projectile_create(game, i, grenade, origin, velocity, input.rewind);
                actor->grenades--;
                combat_event(game, (GameEvent){EVENT_SHOT, origin, i, -1, grenade});
            }
            actor->grenade_charge = input.held & INPUT_GRENADE ? -1 : 0;
        } else actor->grenade_charge++;
    }
    for (size_t event = first_event; event < game->event_count; ++event)
        if (game->events[event].kind == EVENT_SHOT || game->events[event].kind == EVENT_DROP) actor->shot_sequence = input.command_sequence;
}

void combat_predict_actor(Game *game, int index, Input input) {
    if (game->actors[index].life != ALIVE) return;
    Game prediction = {.random = game->random, .tick = game->tick, .max_grenades = game->max_grenades};
    prediction.actors[index] = game->actors[index];
    actor_controls(&prediction, index, input);
    game->actors[index] = prediction.actors[index];
    for (size_t i = 0; i < prediction.event_count; ++i) combat_event(game, prediction.events[i]);
    game_free(&prediction);
}

void combat_step(Game *game, const Input inputs[ACTOR_COUNT]) {
    for (int i = 0; i < ACTOR_COUNT; ++i)
        if (game->actors[i].life == ALIVE) actor_controls(game, i, inputs[i]);

    size_t surviving = 0;
    for (size_t i = 0; i < game->projectile_count; i++) {
        Projectile projectile = game->projectiles[i];
        for (int flag=0;flag<3;++flag) if (projectile.flag_hit_ticks[flag]>0) --projectile.flag_hit_ticks[flag];
        const WeaponDef *weapon = &weapons[projectile.weapon];
        enum { PROJECTILE_FLYING, PROJECTILE_CONSUMED } disposition = PROJECTILE_FLYING;
        projectile.previous = projectile.position;
        for (;;) {
            Vec3 end = add(projectile.position, projectile.velocity);
            WorldHit wall = bullet_trace(projectile.position,end,game->actors[projectile.owner].team);
            int victim = -1;
            float fraction = wall.fraction;
            float hit_modifier = 1;
            int hit_bone = 5;
            int age = weapon->timeout - projectile.ticks;
            for (int target = 0; target < ACTOR_COUNT; target++) {
                Actor *actor = &game->actors[target];
                int owner_delay = projectile.weapon == FRAGGRENADE ? 50 : 20;
                if (actor->life == INACTIVE || projectile.weapon == CLUSTERGRENADE ||
                    (actor->life == ALIVE && actor->spawn_protection_ticks >= 0) ||
                    (projectile.weapon == FLAMER && target == projectile.owner) ||
                    (projectile.hit_mask & (1u << target)) ||
                    (target == projectile.owner && age <= owner_delay)) continue;
                BodyPoint points[HIT_PARTS];
                if (!historical_body(game, target, weapon, projectile.rewind, points)) continue;
                for (int part = 0; part < HIT_PARTS; part++) {
                    float contact = body_fraction(projectile.position,projectile.velocity,&points[part],
                        projectile.weapon == FRAGGRENADE ? 1 : 0);
                    if (contact > fraction || (victim >= 0 && contact == fraction)) continue;
                    fraction = contact;
                    victim = target;
                    hit_modifier = points[part].modifier;
                    hit_bone = points[part].bone;
                }
            }
            if (projectile.weapon!=FRAGGRENADE && projectile.ticks<SRC_BULLET_TIMEOUT-1) {
                for (int flag_index=0;flag_index<3;++flag_index) {
                    Flag *flag=&game->flags[flag_index];
                    if (flag->state==FLAG_ABSENT || flag->carrier==projectile.owner ||
                        projectile.flag_hit_ticks[flag_index]>0 || (game->mode==MODE_INF && flag_index!=1)) continue;
                    float contact=sphere_fraction(projectile.position,projectile.velocity,flag->position,SRC_FLAG_PART_RADIUS);
                    if (contact>fraction) continue;
                    Vec3 push=scale(sub(projectile.velocity,flag->velocity),weapon->push*SRC_THING_PUSH_MULTIPLIER);
                    flag->position=add(flag->position,push);
                    flag->velocity=add(flag->velocity,push);
                    projectile.flag_hit_ticks[flag_index]=SRC_THING_COLLISION_COOLDOWN;
                    break;
                }
            }
            if (victim >= 0) {
                projectile.position = add(projectile.position, scale(projectile.velocity, fraction));
                if (projectile.weapon == FRAGGRENADE || projectile.weapon == CLUSTER) {
                    explode(game, projectile, FRAGGRENADE, projectile.weapon == CLUSTER ?
                        SRC_CLUSTERGRENADE_EXPLOSION_RADIUS : SRC_FRAGGRENADE_EXPLOSION_RADIUS);
                    disposition = PROJECTILE_CONSUMED;
                    break;
                }
                if (projectile.weapon == M79 || projectile.weapon == LAW || projectile.weapon == BOW2) {
                    explode(game, projectile, M79, SRC_M79GRENADE_EXPLOSION_RADIUS);
                    damage_actor(game, victim, projectile.owner, projectile.weapon,
                                 length(projectile.velocity) * projectile.hit_multiply, hit_bone);
                    disposition = PROJECTILE_CONSUMED;
                    break;
                }
                if (projectile.weapon == FLAMER) {
                    Actor *actor = &game->actors[victim];
                    Vec3 bones[21];
                    actor_pose(actor,bones);
                    projectile.position=bones[hit_bone];
                    projectile.velocity=actor->life==ALIVE?actor->velocity:v3(0,0,0);
                    if (projectile.ticks<3 && projectile.ricochets<2) {
                        if (projectile.hit_multiply>=weapons[FLAMER].hit_multiply/3) {
                            projectile.ticks=SRC_FLAMER_TIMEOUT-1;
                            ++projectile.ricochets;
                            projectile_create(game,projectile.owner,FLAMER,projectile.position,scale(actor->velocity,-1),projectile.rewind);
                            game->projectiles[game->projectile_count-1].hit_multiply=2*projectile.hit_multiply/3;
                        }
                        if (actor->health>=0)
                            damage_actor(game,victim,projectile.owner,FLAMER,projectile.hit_multiply,hit_bone);
                    }
                    break;
                }
                float speed = length(projectile.velocity);
                float damage = speed * projectile.hit_multiply * hit_modifier;
                if (projectile.weapon == THROWNKNIFE) damage = speed * projectile.hit_multiply * 0.01f;
                Vec3 impulse=scale(projectile.velocity,weapon->push);
                if (game->actors[victim].life==DEAD) ragdoll_hit(&game->actors[victim],hit_bone,impulse);
                else game->actors[victim].velocity=add(game->actors[victim].velocity,impulse);
                damage_actor(game, victim, projectile.owner, projectile.weapon, damage, hit_bone);
                if (projectile.weapon==THROWNKNIFE) {
                    pickups_spawn(game,PICKUP_WEAPON,KNIFE,projectile.position,v3(0,0,0),1,projectile.owner);
                    disposition=PROJECTILE_CONSUMED;
                    break;
                }
                projectile.hit_mask |= 1u << victim;
                if (game->actors[victim].life == DEAD || speed > 23)
                    projectile.velocity = scale(projectile.velocity, 0.75f);
                else if (speed > 5 && speed / weapon->speed >= 0.9f)
                    projectile.velocity = scale(projectile.velocity, 0.66f);
                else {
                    disposition = PROJECTILE_CONSUMED;
                    break;
                }
                continue;
            } else if (wall.box >= 0) {
                projectile.position = add(projectile.position, scale(projectile.velocity, wall.fraction));
                combat_event(game, (GameEvent){EVENT_IMPACT, projectile.position, projectile.owner, -1, projectile.weapon});
                if (projectile.weapon == CLUSTERGRENADE) {
                    Vec3 origin=sub(projectile.position,projectile.velocity);
                    for (int cluster=0;cluster<5;++cluster) {
                        Vec3 velocity=v3(.75f*projectile.velocity.x-2.5f+(int)(game_random(game)*50)*.1f,
                            -.75f*projectile.velocity.y+2.5f-(int)(game_random(game)*25)*.1f,
                            .75f*projectile.velocity.z-2.5f+(int)(game_random(game)*50)*.1f);
                        projectile_create(game,projectile.owner,CLUSTER,origin,velocity,projectile.rewind);
                        game->projectiles[game->projectile_count-1].hit_multiply=weapons[FRAGGRENADE].hit_multiply*.5f;
                    }
                    combat_event(game,(GameEvent){EVENT_EXPLOSION,projectile.position,projectile.owner,-1,CLUSTERGRENADE});
                    disposition=PROJECTILE_CONSUMED;
                    break;
                }
                if (projectile.weapon == CLUSTER || projectile.weapon == THROWNKNIFE) {
                    if (projectile.weapon==CLUSTER)
                        explode(game,projectile,FRAGGRENADE,SRC_CLUSTERGRENADE_EXPLOSION_RADIUS);
                    else pickups_spawn(game,PICKUP_WEAPON,KNIFE,
                        add(projectile.position,scale(wall.normal,1)),v3(0,0,0),1,projectile.owner);
                    disposition=PROJECTILE_CONSUMED;
                    break;
                }
                if (projectile.weapon == FRAGGRENADE || projectile.weapon == FLAMER) {
                    projectile.velocity = scale(sub(projectile.velocity,
                        scale(wall.normal, 2 * dot(projectile.velocity, wall.normal))), SRC_GRENADE_SURFACECOEF);
                    projectile.position = add(projectile.position, scale(wall.normal, 0.01f));
                    if (projectile.weapon==FLAMER && projectile.ticks>16) projectile.ticks=16;
                } else {
                    Vec3 ricochet = add(scale(projectile.velocity, 25.0f / 35.0f),
                                       scale(wall.normal, length(projectile.velocity) * 10.0f / 35.0f));
                    if (dot(ricochet, wall.normal) > 0 &&
                        length(sub(projectile.position, projectile.hit_spot)) > 50) {
                        projectile.velocity = ricochet;
                        projectile.hit_spot = projectile.position;
                        projectile.position = add(projectile.position, scale(wall.normal, 0.01f));
                        projectile.ricochets++;
                    } else {
                        if (projectile.weapon == M79 || projectile.weapon == LAW || projectile.weapon == BOW2)
                            explode(game, projectile, M79, SRC_M79GRENADE_EXPLOSION_RADIUS);
                        disposition = PROJECTILE_CONSUMED;
                        break;
                    }
                }
            }
            break;
        }
        if (disposition == PROJECTILE_CONSUMED) continue;
        if (--projectile.ticks == 0) {
            if (projectile.weapon==CLUSTER)
                explode(game,projectile,FRAGGRENADE,SRC_CLUSTERGRENADE_EXPLOSION_RADIUS);
            if (projectile.weapon == FRAGGRENADE || projectile.weapon == M79 || projectile.weapon == LAW || projectile.weapon == BOW2)
                explode(game, projectile, FRAGGRENADE, SRC_FRAGGRENADE_EXPLOSION_RADIUS);
            continue;
        }
        if (projectile.ticks % 6 == 0 && projectile.weapon != BARRETT && projectile.weapon != M79 &&
            projectile.weapon != KNIFE && projectile.weapon != LAW) {
            float distance = length(sub(projectile.position, projectile.initial));
            if ((projectile.degrade_count == 0 && distance > 500) ||
                (projectile.degrade_count == 1 && distance > 900)) {
                projectile.hit_multiply *= 0.5f;
                projectile.degrade_count++;
            }
        }
        projectile.velocity.y -= SRC_BULLET_GRAVITY;
        if (projectile.weapon==FLAMER) projectile.velocity.y+=.15f;
        projectile.position = add(projectile.position, projectile.velocity);
        projectile.velocity = scale(projectile.velocity, SRC_BULLET_DAMPING);
        game->projectiles[surviving++] = projectile;
    }
    game->projectile_count = surviving;
}
