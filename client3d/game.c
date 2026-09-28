#include "game.h"
#include "world.h"
#include "generated_rules.h"
#include "ragdoll.h"
#include "pickups.h"
#include "objectives.h"
#include "pose.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

const char *const game_mode_names[7] = {
    "Deathmatch", "Pointmatch", "Teammatch", "Capture the Flag", "Rambomatch", "Infiltration", "Hold the Flag"
};
const char *const game_mode_ids[7] = {"deathmatch", "pointmatch", "teammatch", "ctf", "rambo", "inf", "htf"};
const char *const game_team_names[6] = {"Auto", "Alpha", "Bravo", "Charlie", "Delta", "Spectator"};

float game_random(Game *game) {
    uint32_t x = game->random;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    game->random = x;
    return (float)(x >> 8) / 16777216.0f;
}

static WeaponState weapon_ready(WeaponId id) {
    const WeaponDef *def = &weapons[id];
    return (WeaponState){
        .id = id, .ammo = id == M79 ? 0 : def->ammo,
        .fire_count = id == BARRETT ? 0 : def->fire_interval, .reload_count = def->reload_time,
        .startup_count = def->startup_time, .phase = WEAPON_READY
    };
}

void game_equip(Actor *actor, WeaponId primary, WeaponId secondary) {
    WeaponId ids[2] = {primary, secondary};
    for (int i = 0; i < 2; ++i) {
        actor->loadout[i] = ids[i];
        actor->slots[i] = weapon_ready(ids[i]);
    }
    actor->active_slot = 0;
}

void game_select_loadout(Actor *actor, WeaponId primary, WeaponId secondary) {
    if (actor->loadout[0] == primary && actor->loadout[1] == secondary) return;
    WeaponId current = actor->slots[actor->active_slot].id;
    WeaponId ids[2] = {primary, secondary};
    for (int i = 0; i < 2; ++i) {
        if (actor->loadout[i] == ids[i]) continue;
        actor->loadout[i] = ids[i];
        if (actor->life != ALIVE || actor->spawn_protection_ticks < 0 || current == BOW || current == BOW2)
            continue;
        actor->slots[i] = weapon_ready(ids[i]);
        if (i == 0) actor->active_slot = 0;
    }
}

typedef struct { Vec3 position;float body,enemy;size_t order; } SpawnSite;

static int spawn_order(const void *a,const void *b) {
    const SpawnSite *left=a,*right=b;
    if(left->body!=right->body)return left->body>right->body ? -1 : 1;
    if(left->enemy!=right->enemy)return left->enemy>right->enemy ? -1 : 1;
    return left->order<right->order ? -1 : left->order>right->order;
}

SpawnResult game_respawn(Game *game, int index) {
    Actor *actor = &game->actors[index];
    const int kills = actor->kills, deaths = actor->deaths;
    const WeaponId primary = actor->loadout[0], secondary = actor->loadout[1];
    const Team team = actor->team;
    const int captures = actor->captures;
    const uint32_t spawn_id = actor->spawn_id + 1;
    size_t count = world_team_spawn_count(team);
    assert(team != TEAM_SPECTATOR && count > 0);
    SpawnSite available[count];
    size_t available_count = 0;
    float farthest_enemy=0;
    for (size_t candidate = 0; candidate < count; ++candidate) {
        Vec3 position = world_team_spawn(team, candidate);
        int occupied = 0;float body=INFINITY,enemy=INFINITY;
        for (int other = 0; other < ACTOR_COUNT; ++other) {
            if (other == index || game->actors[other].life != ALIVE) continue;
            Vec3 delta = sub(position, game->actors[other].position);
            if (fabsf(delta.y) < actor_height(STANDING) &&
                delta.x * delta.x + delta.z * delta.z < 4 * SRC_PART_RADIUS * SRC_PART_RADIUS) {
                occupied = 1;
                break;
            }
            float distance=dot(delta,delta);body=fminf(body,distance);
            if(!game_team_mode(game->mode) || game->actors[other].team!=team)enemy=fminf(enemy,distance);
        }
        if (!occupied) {
            available[available_count++]=(SpawnSite){position,body,enemy,candidate};
            farthest_enemy=fmaxf(farthest_enemy,enemy);
        }
    }
    if (!available_count) {
        actor->respawn_ticks = 1;
        return SPAWN_BLOCKED;
    }
    float clearance=SRC_FRAGGRENADE_EXPLOSION_RADIUS+SRC_PART_RADIUS;
    float required=fminf(farthest_enemy,clearance*clearance);
    size_t first=(size_t)(game_random(game)*(float)count),eligible=0;
    for(size_t i=0;i<available_count;++i)if(available[i].enemy>=required) {
        available[i].order=(available[i].order+count-first)%count;
        available[eligible++]=available[i];
    }
    qsort(available,eligible,sizeof(*available),spawn_order);
    Vec3 targets[ACTOR_COUNT][3],body[21];
    for(int other=0;other<ACTOR_COUNT;++other) {
        const Actor *opponent=&game->actors[other];
        if(other==index || opponent->life!=ALIVE || (game_team_mode(game->mode) && opponent->team==team))continue;
        actor_pose(opponent,body);
        targets[other][0]=body[12];targets[other][1]=scale(add(body[10],body[11]),.5f);targets[other][2]=scale(add(body[5],body[6]),.5f);
    }
    Actor standing={.pose=STANDING};actor_pose(&standing,body);
    Vec3 offsets[]={body[12],scale(add(body[10],body[11]),.5f),scale(add(body[5],body[6]),.5f)};
    size_t choice=0;
    for(size_t i=0;i<eligible;++i) {
        int exposed=0;
        for(int other=0;other<ACTOR_COUNT && !exposed;++other) {
            const Actor *opponent=&game->actors[other];
            if(other==index || opponent->life!=ALIVE || (game_team_mode(game->mode) && opponent->team==team))continue;
            for(unsigned part=0;part<3;++part) {
                Vec3 point=add(available[i].position,offsets[part]);
                if(!world_occluded_for(targets[other][1],point,(WorldQuery){WORLD_TRACE_BULLET,opponent->team,WORLD_NO_FLAG}) ||
                    !world_occluded_for(add(available[i].position,offsets[1]),targets[other][part],
                        (WorldQuery){WORLD_TRACE_BULLET,team,WORLD_NO_FLAG})) {
                    exposed=1;break;
                }
            }
        }
        if(!exposed){choice=i;break;}
    }
    Vec3 spawn=available[choice].position;
    *actor = (Actor){
        .position = spawn, .previous = spawn, .team = team, .captures = captures,
        .spawn_id = spawn_id, .motion_tick = game->tick,
        .health = SRC_DEFAULT_HEALTH, .contact = AIRBORNE, .pose = STANDING,
        .life = ALIVE, .fuel = world_jet_fuel, .fuel_capacity = world_jet_fuel,
        .spawn_protection_ticks = SRC_DEFAULT_CEASEFIRE_TIME,
        .nav_edge = -1, .nav_goal = -1,
        .kills = kills, .deaths = deaths, .grenades = game->max_grenades / 2,
        .grenade_weapon = FRAGGRENADE
    };
    game_equip(actor, primary, secondary);
    return SPAWN_READY;
}

int game_team_mode(GameMode mode) {
    return mode == MODE_TEAMMATCH || mode == MODE_CTF || mode == MODE_INF || mode == MODE_HTF;
}

static Team balanced_team(const Game *game, int index) {
    int count[5] = {0};
    for (int i = 0; i < ACTOR_COUNT; ++i)
        if (i != index && game->actors[i].life != INACTIVE) ++count[game->actors[i].team];
    Team team = TEAM_NONE;
    int smallest = ACTOR_COUNT + 1;
    int teams = game->mode == MODE_TEAMMATCH ? 4 : 2;
    for (int candidate = TEAM_ALPHA; candidate <= teams; ++candidate)
        if (count[candidate] < smallest) {
            team = (Team)candidate;
            smallest = count[candidate];
        }
    assert(team != TEAM_NONE);
    return team;
}

void game_select_team(Game *game, int index, Team team) {
    Actor *actor = &game->actors[index];
    if (team != TEAM_SPECTATOR) {
        if (!game_team_mode(game->mode)) team = TEAM_NONE;
        else if (team == TEAM_NONE) team = balanced_team(game, index);
        assert(team == TEAM_NONE || (team >= TEAM_ALPHA &&
            team <= (game->mode == MODE_TEAMMATCH ? TEAM_DELTA : TEAM_BRAVO)));
    }
    if (actor->team == team) return;
    if (actor->carried_flag != FLAG_NONE) objectives_return(game, actor->carried_flag, index, EVENT_FLAG_RETURN);
    if (actor->life != INACTIVE) pickups_drop_weapon(game, index);
    actor->team = team;
    if (team == TEAM_SPECTATOR) { actor->life = INACTIVE; actor->respawn_ticks = 0; }
    else if (game_respawn(game, index) == SPAWN_BLOCKED) actor->life = INACTIVE;
}

void game_set_mode(Game *game, GameMode mode) {
    assert(world_supports_mode(mode));
    const int limits[] = {SRC_DM_LIMIT, SRC_PM_LIMIT, SRC_TM_LIMIT, SRC_CTF_LIMIT,
        SRC_RM_LIMIT, SRC_INF_LIMIT, SRC_HTF_LIMIT};
    game->mode = mode;
    game->score_limit = limits[mode];
    game->time_limit_ticks = SRC_TIMELIMIT;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        Actor *actor = &game->actors[i];
        actor->carried_flag = FLAG_NONE;
        if (actor->life == INACTIVE) continue;
        actor->team = TEAM_NONE;
    }
    if (game_team_mode(mode))
        for (int i = 0; i < ACTOR_COUNT; ++i)
            if (game->actors[i].life != INACTIVE) game->actors[i].team = balanced_team(game, i);
    game_restart(game);
}

void game_restart(Game *game) {
    game->phase = MATCH_PLAYING;
    game->match_ticks = 0;
    game->wave_counter = 1;
    game->htf_interval = SRC_HTF_POINTSTIME * TICK_RATE;
    game->projectile_count = game->event_count = game->pickup_count = 0;
    for (int i = 0; i < 5; ++i) game->team_score[i] = 0;
    for (int i = 0; i < ACTOR_COUNT; ++i)
        if (game->actors[i].life != INACTIVE) game->actors[i].life = DEAD;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        Actor *actor = &game->actors[i];
        actor->carried_flag = FLAG_NONE;
        actor->kills = actor->deaths = actor->captures = 0;
        if (actor->life == INACTIVE) continue;
        if (game_team_mode(game->mode) && actor->team == TEAM_NONE)
            actor->team = balanced_team(game, i);
        game_respawn(game, i);
    }
    objectives_init(game);
    pickups_init(game);
}

void game_init(Game *game, uint32_t seed, GameMode mode) {
    *game = (Game){.random = seed, .max_grenades = 2};
    const WeaponId loadouts[] = {AK74, AK74, STEYRAUG, BARRETT, RUGER77, M249, SPAS12};
    const WeaponId secondaries[] = {COLT, LAW, COLT, COLT, LAW, COLT, COLT};
    const char *names[] = {"Soldier", "Boogie Man", "Danko", "Poncho", "Dutch", "Blain", "Billy"};
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        if (i < 7) snprintf(game->names[i], sizeof(game->names[i]), "%s", names[i]);
        else snprintf(game->names[i], sizeof(game->names[i]), "Soldier %d", i + 1);
        game->actors[i].loadout[0] = loadouts[i % (sizeof(loadouts) / sizeof(loadouts[0]))];
        game->actors[i].loadout[1] = secondaries[i % (sizeof(secondaries) / sizeof(secondaries[0]))];
        game->actors[i].life = i < 7 ? ALIVE : INACTIVE;
    }
    game_set_mode(game, mode);
}

void game_free(Game *game) {
    free(game->projectiles);
    free(game->events);
    free(game->pickups);
    free(game->history);
    *game = (Game){0};
}

void game_step(Game *game, const Input inputs[ACTOR_COUNT]) {
    game->event_count = 0;
    if (game->phase == MATCH_FINISHED) return;
    int players = 0;
    for (int i = 0; i < ACTOR_COUNT; ++i) players += game->actors[i].life != INACTIVE;
    int wave = (int)nearbyintf(players * SRC_WAVERESPAWN_TIME_MULITPLIER) * TICK_RATE;
    if (wave > SRC_RESPAWNTIME_MINWAVE) wave = SRC_RESPAWNTIME_MAXWAVE;
    wave -= SRC_RESPAWNTIME_MINWAVE;
    if (wave < 1) wave = 1;
    if (--game->wave_counter < 1) game->wave_counter = wave;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        Actor *actor = &game->actors[i];
        if (actor->life == INACTIVE) {
            if (actor->respawn_ticks > 0 && game_respawn(game, i) == SPAWN_READY) actor->motion_tick = game->tick + 1;
            continue;
        }
        actor->controls = inputs[i].held;
        if (actor->life == DEAD) {
            ragdoll_step(actor);
            if (--actor->respawn_ticks == 0) {
                if (game_respawn(game, i) == SPAWN_READY) actor->motion_tick = game->tick + 1;
            }
            continue;
        }
        if (actor->spawn_protection_ticks >= 0) --actor->spawn_protection_ticks;
        movement_step(actor, inputs[i], game->tick);
        actor->motion_tick = game->tick + 1;
        combat_environment(game, i, world_contact_type(actor));
        if (actor->hit_ticks > 0) --actor->hit_ticks;
        if (actor->life == ALIVE && actor->position.y < world_bounds.min.y - 2 * actor_height(STANDING)) {
            ragdoll_start(actor);
            objectives_kill(game, i, i, NOWEAPON);
            actor->vest = 0;
            actor->bonus = BONUS_NONE;
            actor->bonus_ticks = 0;
            actor->health = 0;
            actor->life = DEAD;
            pickups_drop_weapon(game, i);
            ++actor->deaths;
        }
    }
    combat_step(game, inputs);
    pickups_step(game);
    objectives_step(game, inputs);
    ++game->tick;
    combat_history_record(game);
}
