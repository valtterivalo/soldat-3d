#include "pickups.h"
#include "world.h"
#include "pose.h"
#include "generated_rules.h"

#include <stdlib.h>

static const unsigned spawn_types[] = {
    [PICKUP_HEALTH]=8, [PICKUP_GRENADES]=7, [PICKUP_FLAMER]=11,
    [PICKUP_PREDATOR]=13, [PICKUP_VEST]=10, [PICKUP_BERSERKER]=12,
    [PICKUP_CLUSTER]=9, [PICKUP_BOW]=15
};

typedef struct {float gravity,damping;} PickupPhysics;
#define THING(name) {SRC_THING_##name##_GRAVITY,SRC_THING_##name##_VDAMPING}
static const PickupPhysics gun_physics[]={
    THING(DESERT_EAGLE),THING(HK_MP5),THING(AK74),THING(STEYR_AUG),THING(SPAS12),THING(RUGER77),
    THING(M79),THING(BARRET_M82A1),THING(MINIMI),THING(MINIGUN),THING(USSOCOM),THING(COMBAT_KNIFE),
    THING(CHAINSAW),THING(LAW)
};
static const PickupPhysics kit_physics[]={
    [PICKUP_HEALTH]=THING(MEDICAL_KIT),[PICKUP_GRENADES]=THING(GRENADE_KIT),
    [PICKUP_FLAMER]=THING(FLAMER_KIT),[PICKUP_PREDATOR]=THING(PREDATOR_KIT),
    [PICKUP_VEST]=THING(VEST_KIT),[PICKUP_BERSERKER]=THING(BERSERK_KIT),
    [PICKUP_CLUSTER]=THING(CLUSTER_KIT),[PICKUP_BOW]=THING(RAMBO_BOW)
};
#undef THING

static int spawn_point(Game *game, PickupKind kind, int previous) {
    size_t count=0;
    int last=-1;
    for (size_t i=0;i<world_source_spawn_count;++i) {
        if (world_source_spawns[i].type!=spawn_types[kind]) continue;
        last=(int)i;
        if ((int)i!=previous) ++count;
    }
    if (last<0 && kind==PICKUP_BOW) return (int)(game_random(game)*(float)world_source_spawn_count);
    if (count==0) return last;
    size_t chosen=(size_t)(game_random(game)*(float)count);
    for (size_t i=0;i<world_source_spawn_count;++i)
        if (world_source_spawns[i].type==spawn_types[kind] && (int)i!=previous && chosen--==0)
            return (int)i;
    abort();
}

void pickups_weapon(Actor *actor, int slot, WeaponId id, int ammo) {
    const WeaponDef *weapon=&weapons[id];
    actor->slots[slot]=(WeaponState){.id=id,.ammo=ammo,.fire_count=weapon->fire_interval,
        .reload_count=weapon->reload_time,.startup_count=weapon->startup_time,.phase=WEAPON_READY};
    actor->burst_count=actor->melee_frame=0;
}

static Vec3 pickup_start(PickupKind kind, int spawn) {
    Vec3 position=world_source_spawns[spawn].position;
    unsigned type=world_source_spawns[spawn].type;
    if (kind==PICKUP_BOW && (type<=6 || type==14)) position.y+=.75f;
    return position;
}

size_t pickups_spawn(Game *game, PickupKind kind, WeaponId weapon, Vec3 position,
    Vec3 velocity, int ammo, int owner) {
    if (game->pickup_count==game->pickup_capacity) {
        size_t capacity=game->pickup_capacity?game->pickup_capacity*2:32;
        Pickup *items=realloc(game->pickups,capacity*sizeof(*items));
        if (!items) abort();
        game->pickups=items;
        game->pickup_capacity=capacity;
    }
    size_t index=game->pickup_count++;
    game->pickups[index]=(Pickup){.kind=kind,.weapon=weapon,.position=position,.previous=position,
        .velocity=velocity,.ammo=ammo,.owner=owner,.spawn_index=-1,
        .ticks=kind==PICKUP_WEAPON?SRC_GUNRESISTTIME:SRC_FLAG_TIMEOUT};
    return index;
}

void pickups_init(Game *game) {
    game->pickup_count=0;
    const PickupKind kinds[]={PICKUP_HEALTH,PICKUP_GRENADES,PICKUP_BOW};
    const int counts[]={world_medikits,game->max_grenades>0?world_grenades:0,game->mode==MODE_RAMBO};
    for (size_t kind=0;kind<3;++kind) {
        for (int n=0;n<counts[kind];++n) {
            int spawn=spawn_point(game,kinds[kind],-1);
            if (spawn<0) break;
            size_t index=pickups_spawn(game,kinds[kind],kinds[kind]==PICKUP_BOW?BOW:NOWEAPON,
                pickup_start(kinds[kind],spawn),v3(0,0,0),1,-1);
            game->pickups[index].spawn_index=spawn;
        }
    }
}

void pickups_drop_weapon(Game *game, int index) {
    Actor *actor=&game->actors[index];
    WeaponState weapon=actor->slots[actor->active_slot];
    Vec3 bones[21];
    actor_pose(actor,bones);
    int bow=weapon.id==BOW || weapon.id==BOW2;
    if (weapon.id<=LAW || (bow && game->mode==MODE_RAMBO)) {
        Vec3 velocity=add(actor->velocity,scale(direction(actor->yaw,actor->pitch),actor->life==DEAD?.33f:1.505f));
        size_t item=pickups_spawn(game,bow?PICKUP_BOW:PICKUP_WEAPON,bow?BOW:weapon.id,
            bones[16],velocity,weapon.ammo,index);
        game->pickups[item].yaw=actor->yaw;
        combat_event(game,(GameEvent){EVENT_DROP,bones[16],index,-1,weapon.id});
    }
    pickups_weapon(actor,actor->active_slot,NOWEAPON,weapons[NOWEAPON].ammo);
    if (bow) pickups_weapon(actor,1-actor->active_slot,NOWEAPON,weapons[NOWEAPON].ammo);
}

void pickups_step(Game *game) {
    for (int i=0;i<ACTOR_COUNT;++i) {
        Actor *actor=&game->actors[i];
        if (actor->life!=ALIVE) continue;
        if (actor->bonus_ticks>0 && --actor->bonus_ticks==0) actor->bonus=BONUS_NONE;
        if (actor->health_cooldown>0) --actor->health_cooldown;
        WeaponId id=actor->slots[actor->active_slot].id;
        if ((id==BOW || id==BOW2) && game->tick%3==0 && actor->health<SRC_DEFAULT_HEALTH)
            actor->health+=1;
    }
    size_t surviving=0;
    for (size_t i=0;i<game->pickup_count;++i) {
        Pickup item=game->pickups[i];
        PickupPhysics physics=item.kind==PICKUP_WEAPON?gun_physics[item.weapon]:kit_physics[item.kind];
        float radius=item.kind==PICKUP_WEAPON?SRC_GUN_RADIUS*(item.weapon==KNIFE?1.5f:1):
            item.kind==PICKUP_BOW?SRC_BOW_RADIUS:SRC_KIT_RADIUS;
        item.previous=item.position;
        item.velocity=scale(item.velocity,physics.damping);
        item.velocity.y-=SRC_GRAV*physics.gravity;
        Vec3 end=add(item.position,item.velocity);
        Vec3 extents=item.kind==PICKUP_WEAPON || item.kind==PICKUP_BOW?v3(.75f,.75f,.75f):
            v3(SRC_KIT_WIDTH*.5f,SRC_KIT_HEIGHT*.5f,2.75f);
        WorldHit wall=world_trace_for(item.position,end,extents,(WorldQuery){WORLD_TRACE_ITEM,0,WORLD_NO_FLAG});
        item.position=add(item.position,scale(item.velocity,wall.fraction));
        if (wall.box>=0) {
            item.position=add(item.position,scale(wall.normal,.01f));
            float penetration=-(1-wall.fraction)*dot(item.velocity,wall.normal);
            item.velocity=scale(wall.normal,penetration);
            if (length(item.velocity)<SRC_MINMOVEDELTA) item.velocity=v3(0,0,0);
        }
        int target=-1;
        float nearest=radius;
        for (int actor_index=0;actor_index<ACTOR_COUNT;++actor_index) {
            const Actor *actor=&game->actors[actor_index];
            if (actor->life!=ALIVE) continue;
            const WeaponState *weapon=&actor->slots[actor->active_slot];
            int eligible=0;
            switch (item.kind) {
                case PICKUP_WEAPON: eligible=weapon->id==NOWEAPON && actor->switch_ticks==0 && item.age>30;break;
                case PICKUP_BOW: eligible=weapon->id==NOWEAPON && actor->switch_ticks==0 && item.age>100;break;
                case PICKUP_HEALTH: eligible=actor->health<SRC_DEFAULT_HEALTH && actor->health_cooldown==0;break;
                case PICKUP_GRENADES: eligible=actor->grenades<game->max_grenades &&
                    (actor->grenade_weapon!=CLUSTERGRENADE || actor->grenades==0);break;
                case PICKUP_FLAMER: eligible=actor->bonus==BONUS_NONE && actor->spawn_protection_ticks<1 &&
                    weapon->id!=BOW && weapon->id!=BOW2;break;
                case PICKUP_PREDATOR: case PICKUP_BERSERKER:
                    eligible=actor->bonus==BONUS_NONE && actor->spawn_protection_ticks<1;break;
                case PICKUP_VEST: eligible=actor->vest<SRC_DEFAULTVEST;break;
                case PICKUP_CLUSTER: eligible=actor->grenade_weapon==FRAGGRENADE || actor->grenades==0;break;
            }
            Vec3 center=add(actor->position,v3(0,actor_height(actor->pose)*.5f,0));
            float distance=length(sub(item.position,center));
            if (eligible && distance<nearest && world_trace(center,item.position,v3(0,0,0)).box<0) {
                nearest=distance;target=actor_index;
            }
        }
        if (target>=0) {
            Actor *actor=&game->actors[target];
            switch (item.kind) {
                case PICKUP_WEAPON: pickups_weapon(actor,actor->active_slot,item.weapon,item.ammo);break;
                case PICKUP_BOW:
                    pickups_weapon(actor,actor->active_slot,BOW,1);
                    pickups_weapon(actor,1-actor->active_slot,BOW2,weapons[BOW2].ammo);break;
                case PICKUP_HEALTH:
                    actor->health=SRC_DEFAULT_HEALTH;
                    actor->health_cooldown=2*TICK_RATE-(int)(game->tick%(2*TICK_RATE));break;
                case PICKUP_GRENADES: actor->grenade_weapon=FRAGGRENADE;actor->grenades=game->max_grenades;break;
                case PICKUP_FLAMER:
                    pickups_weapon(actor,1-actor->active_slot,actor->slots[actor->active_slot].id,
                        weapons[actor->slots[actor->active_slot].id].ammo);
                    pickups_weapon(actor,actor->active_slot,FLAMER,weapons[FLAMER].ammo);
                    actor->bonus=BONUS_FLAMEGOD;actor->bonus_ticks=SRC_FLAMERBONUSTIME;actor->health=SRC_DEFAULT_HEALTH;break;
                case PICKUP_PREDATOR:
                    actor->bonus=BONUS_PREDATOR;actor->bonus_ticks=SRC_PREDATORBONUSTIME;actor->health=SRC_DEFAULT_HEALTH;break;
                case PICKUP_VEST: actor->vest=SRC_DEFAULTVEST;break;
                case PICKUP_BERSERKER:
                    actor->bonus=BONUS_BERSERKER;actor->bonus_ticks=SRC_BERSERKERBONUSTIME;actor->health=SRC_DEFAULT_HEALTH;break;
                case PICKUP_CLUSTER: actor->grenade_weapon=CLUSTERGRENADE;actor->grenades=SRC_CLUSTER_GRENADES;break;
            }
            combat_event(game,(GameEvent){EVENT_PICKUP,item.position,target,item.kind,item.weapon});
            if (item.kind!=PICKUP_HEALTH && item.kind!=PICKUP_GRENADES) continue;
            int spawn=spawn_point(game,item.kind,item.spawn_index);
            if (spawn<0) continue;
            item.position=item.previous=pickup_start(item.kind,spawn);
            item.velocity=v3(0,0,0);item.age=0;item.spawn_index=spawn;
        }
        ++item.age;
        if (item.kind!=PICKUP_HEALTH && item.kind!=PICKUP_GRENADES && --item.ticks==0) {
            if (item.kind!=PICKUP_BOW) continue;
            int spawn=spawn_point(game,PICKUP_BOW,-1);
            item.position=item.previous=pickup_start(PICKUP_BOW,spawn);
            item.velocity=v3(0,0,0);item.age=0;item.ticks=SRC_FLAG_TIMEOUT;item.spawn_index=spawn;
        }
        if (item.position.y<world_bounds.min.y-2*actor_height(STANDING)) continue;
        game->pickups[surviving++]=item;
    }
    game->pickup_count=surviving;
    if (game->mode==MODE_RAMBO && game->tick%TICK_RATE==0) {
        int present=0;
        for (size_t i=0;i<game->pickup_count;++i) present|=game->pickups[i].kind==PICKUP_BOW;
        for (int i=0;i<ACTOR_COUNT;++i) {
            const Actor *actor=&game->actors[i];
            WeaponId weapon=actor->slots[actor->active_slot].id;
            present|=actor->life!=INACTIVE && (weapon==BOW || weapon==BOW2);
        }
        if (!present) {
            int spawn=spawn_point(game,PICKUP_BOW,-1);
            size_t item=pickups_spawn(game,PICKUP_BOW,BOW,pickup_start(PICKUP_BOW,spawn),v3(0,0,0),1,-1);
            game->pickups[item].spawn_index=spawn;
        }
    }
    if (game->bonus_frequency==0 || game->tick==0) return;
    const int periods[]={0,7400,4300,2500,1600,800};
    int period=periods[game->bonus_frequency];
    const PickupKind kinds[]={PICKUP_BERSERKER,PICKUP_FLAMER,PICKUP_PREDATOR,PICKUP_VEST,PICKUP_CLUSTER};
    const int intervals[]={period,444,period,period/2,period/2};
    const int odds[]={SRC_BERSERKERBONUS_RANDOM,SRC_FLAMERBONUS_RANDOM,SRC_PREDATORBONUS_RANDOM,SRC_VESTBONUS_RANDOM,SRC_CLUSTERBONUS_RANDOM};
    for (size_t i=0;i<5;++i) {
        if (game->tick%(uint64_t)intervals[i]!=0 || (int)(game_random(game)*odds[i])!=0) continue;
        int spawn=spawn_point(game,kinds[i],-1);
        if (spawn>=0) {
            size_t item=pickups_spawn(game,kinds[i],NOWEAPON,world_source_spawns[spawn].position,v3(0,0,0),0,-1);
            game->pickups[item].spawn_index=spawn;
        }
    }
}
