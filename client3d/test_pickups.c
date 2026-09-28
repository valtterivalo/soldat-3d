#include "game.h"
#include "pickups.h"
#include "pose.h"
#include "ragdoll.h"
#include "world.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int pass,const char *name) {
    if (!pass) {fprintf(stderr,"Pickup contract failed: %s\n",name);exit(EXIT_FAILURE);}
}

static void reset(Game *game) {
    game_free(game);
    game_init(game, 0x6974656du, MODE_DEATHMATCH);
    for (int i=0;i<ACTOR_COUNT;++i) game->actors[i].spawn_protection_ticks=-1;
    game->pickup_count=0;
    for (int i=1;i<ACTOR_COUNT;++i) game->actors[i].life=INACTIVE;
    Vec3 air=scale(add(world_bounds.min,world_bounds.max),.5f);
    air.y=world_bounds.max.y-100;
    game->actors[0].position=game->actors[0].previous=air;
}

static size_t nearby(Game *game,PickupKind kind,WeaponId weapon,int ammo) {
    return pickups_spawn(game,kind,weapon,add(game->actors[0].position,v3(0,7,0)),v3(0,0,0),ammo,-1);
}

static void bullet_at(Game *game,WeaponId weapon,Vec3 position,Vec3 velocity) {
    game->projectiles=realloc(game->projectiles,sizeof(*game->projectiles));
    if (!game->projectiles) abort();
    game->projectile_count=game->projectile_capacity=1;
    game->projectiles[0]=(Projectile){.weapon=weapon,.owner=0,.position=position,.previous=position,
        .initial=position,.hit_spot=position,.velocity=velocity,.ticks=weapons[weapon].timeout-21,
        .hit_multiply=weapons[weapon].hit_multiply};
}

int main(void) {
    world_init();poses_init();ragdolls_init();
    Game game={0};
    Input idle[ACTOR_COUNT]={0};
    reset(&game);
    Actor *actor=&game.actors[0];
    pickups_init(&game);
    check(game.pickup_count==(size_t)(world_medikits+world_grenades),"map header controls medical and grenade pack counts");
    size_t medical=0;
    while (medical<game.pickup_count && game.pickups[medical].kind!=PICKUP_HEALTH) ++medical;
    check(medical<game.pickup_count,"Arena2 exposes original medical spawns");
    int previous_spawn=game.pickups[medical].spawn_index;
    size_t medical_spawns=0;
    for (size_t i=0;i<world_source_spawn_count;++i) medical_spawns+=world_source_spawns[i].type==8;
    actor->position=actor->previous=sub(game.pickups[medical].position,v3(0,7,0));
    actor->health=10;
    pickups_step(&game);
    check(actor->health==SRC_DEFAULT_HEALTH && game.pickup_count==(size_t)(world_medikits+world_grenades),
        "medical pack relocates immediately without reducing map pack count");
    check(medical_spawns==1 || game.pickups[medical].spawn_index!=previous_spawn,
        "medical respawn avoids its previous source marker");
    reset(&game);
    nearby(&game,PICKUP_HEALTH,NOWEAPON,0);
    pickups_step(&game);
    check(game.pickup_count==1,"healthy player leaves medical kit");
    actor->health=10;
    pickups_step(&game);
    check(actor->health==SRC_DEFAULT_HEALTH && actor->health_cooldown==120,"medical kit fills health and uses source cooldown");
    actor->health=10;
    nearby(&game,PICKUP_HEALTH,NOWEAPON,0);
    pickups_step(&game);
    check(actor->health==10,"medical cooldown prevents consecutive healing");
    actor->health_cooldown=1;
    pickups_step(&game);
    check(actor->health==SRC_DEFAULT_HEALTH,"medical cooldown releases pickup");

    reset(&game);
    actor->grenades=0;
    nearby(&game,PICKUP_GRENADES,NOWEAPON,0);
    pickups_step(&game);
    check(actor->grenades==game.max_grenades && actor->grenade_weapon==FRAGGRENADE,"grenade kit uses configured source limit");
    nearby(&game,PICKUP_CLUSTER,NOWEAPON,0);
    pickups_step(&game);
    check(actor->grenades==SRC_CLUSTER_GRENADES && actor->grenade_weapon==CLUSTERGRENADE,"cluster kit replaces frag inventory");
    actor->grenades=1;
    nearby(&game,PICKUP_GRENADES,NOWEAPON,0);
    pickups_step(&game);
    check(actor->grenades==1 && actor->grenade_weapon==CLUSTERGRENADE,"frag kit cannot replace remaining clusters");
    actor->grenades=0;
    pickups_step(&game);
    check(actor->grenades==game.max_grenades && actor->grenade_weapon==FRAGGRENADE,"empty cluster inventory accepts frag kit");

    reset(&game);
    actor->slots[actor->active_slot].ammo=7;
    pickups_drop_weapon(&game,0);
    check(actor->slots[actor->active_slot].id==NOWEAPON && game.pickups[0].ammo==7,"drop preserves magazine and empties active hand");
    game.pickups[0].position=add(actor->position,v3(0,7,0));game.pickups[0].velocity=v3(0,0,0);
    game.pickups[0].age=30;
    pickups_step(&game);
    check(actor->slots[actor->active_slot].id==NOWEAPON,"drop requires more than thirty ticks");
    pickups_step(&game);
    check(actor->slots[actor->active_slot].id==AK74 && actor->slots[actor->active_slot].ammo==7 && game.pickup_count==0,
        "empty hands collect exact dropped ammunition");
    size_t item=pickups_spawn(&game,PICKUP_WEAPON,LAW,add(actor->position,v3(40,0,0)),v3(0,0,0),0,-1);
    check(game.pickups[item].ticks==1200,"dropped gun lifetime is twenty seconds");
    game.pickups[item].ticks=1;
    pickups_step(&game);
    check(game.pickup_count==0,"expired weapon is removed");

    reset(&game);
    Input gun_throw[ACTOR_COUNT]={0};
    gun_throw[0].held=gun_throw[0].pressed=INPUT_THROW;
    for (int tick=0;tick<18;++tick) {combat_step(&game,gun_throw);gun_throw[0].pressed=0;}
    check(game.pickup_count==0,"gun remains held until source throw frame nineteen");
    combat_step(&game,gun_throw);
    check(game.pickup_count==1 && actor->slots[0].id==NOWEAPON,"gun releases on source throw frame nineteen");

    reset(&game);
    pickups_weapon(actor,0,KNIFE,1);
    Input throwing[ACTOR_COUNT]={0};
    throwing[0].held=throwing[0].pressed=INPUT_THROW;
    combat_step(&game,throwing);
    check(game.projectile_count==0,"knife charge starts before release");
    throwing[0]=(Input){0};
    combat_step(&game,throwing);
    check(actor->slots[0].id==NOWEAPON && game.projectile_count==1 && game.projectiles[0].weapon==THROWNKNIFE,
        "released knife becomes a damaging projectile");
    check(fabsf(game.projectiles[0].velocity.z-weapons[THROWNKNIFE].speed*.75f*SRC_BULLET_DAMPING)<.001f,
        "quick knife throw uses source half-charge speed");
    bullet_at(&game,THROWNKNIFE,add(world_nav_nodes[0].position,v3(0,1,0)),v3(0,-2,0));
    combat_step(&game,idle);
    check(game.projectile_count==0 && game.pickup_count==1 && game.pickups[0].weapon==KNIFE,
        "knife hitting terrain becomes recoverable");

    reset(&game);
    bullet_at(&game,CLUSTERGRENADE,add(world_nav_nodes[0].position,v3(0,1,0)),v3(0,-2,0));
    combat_step(&game,idle);
    check(game.projectile_count==5,"cluster container releases five bomblets on terrain");
    for (size_t i=0;i<game.projectile_count;++i)
        check(game.projectiles[i].weapon==CLUSTER,"cluster burst contains bomblets only");

    reset(&game);
    nearby(&game,PICKUP_FLAMER,NOWEAPON,0);
    pickups_step(&game);
    check(actor->bonus==BONUS_FLAMEGOD && actor->bonus_ticks==600 && actor->slots[0].id==FLAMER && actor->slots[1].id==AK74,
        "flame god gives source weapon bonus and saves primary as secondary");
    idle[0].pressed=INPUT_SWITCH|INPUT_THROW;
    combat_step(&game,idle);
    check(actor->switch_ticks==0 && actor->throw_frame==0,"flame god prevents weapon switch and drop");
    idle[0]=(Input){0};
    actor->bonus_ticks=1;
    pickups_step(&game);
    check(actor->bonus==BONUS_NONE && actor->slots[0].id==FLAMER,"flame god immunity expires while weapon remains");
    nearby(&game,PICKUP_PREDATOR,NOWEAPON,0);pickups_step(&game);
    check(actor->bonus==BONUS_PREDATOR && actor->bonus_ticks==1500,"predator duration follows source");
    nearby(&game,PICKUP_BERSERKER,NOWEAPON,0);pickups_step(&game);
    check(actor->bonus==BONUS_PREDATOR,"timed bonuses do not stack");
    actor->bonus=BONUS_NONE;pickups_step(&game);
    check(actor->bonus==BONUS_BERSERKER && actor->bonus_ticks==900,"berserker duration follows source");
    item=nearby(&game,PICKUP_VEST,NOWEAPON,0);actor->vest=SRC_DEFAULTVEST;
    check(game.pickups[item].ticks==1500,"bonus pack lifetime is twenty-five seconds");
    game.pickups[item].ticks=1;pickups_step(&game);
    check(game.pickup_count==0,"unused bonus pack expires");

    reset(&game);
    game.mode=MODE_RAMBO;
    pickups_weapon(actor,0,NOWEAPON,weapons[NOWEAPON].ammo);
    item=nearby(&game,PICKUP_BOW,BOW,1);game.pickups[item].age=101;
    pickups_step(&game);
    check(actor->slots[0].id==BOW && actor->slots[1].id==BOW2,"bow equips normal and explosive arrows");
    actor->health=140;game.tick=3;pickups_step(&game);
    check(actor->health==141,"bow regenerates one health every third tick");

    reset(&game);
    Actor *target=&game.actors[1];
    target->life=ALIVE;target->position=target->previous=add(actor->position,v3(30,0,0));target->vest=100;
    Vec3 bones[21];actor_pose(target,bones);
    float amount=10*weapons[AK74].hit_multiply*weapons[AK74].modifier_head;
    bullet_at(&game,AK74,bones[12],v3(10,0,0));combat_step(&game,idle);
    check(target->health==SRC_DEFAULT_HEALTH-nearbyintf(.25f*amount) && target->vest==100-nearbyintf(.33f*amount),
        "vest uses source rounded health and armor damage");
    target->health=SRC_DEFAULT_HEALTH;target->vest=100;actor->bonus=BONUS_BERSERKER;
    bullet_at(&game,AK74,bones[12],v3(10,0,0));combat_step(&game,idle);
    check(fabsf(target->health-(SRC_DEFAULT_HEALTH-4*amount))<.001f,"berserker multiplies original damage despite armor");
    target->health=SRC_DEFAULT_HEALTH;target->bonus=BONUS_FLAMEGOD;
    bullet_at(&game,AK74,bones[12],v3(10,0,0));combat_step(&game,idle);
    check(target->health==SRC_DEFAULT_HEALTH,"flame god ignores health damage");

    reset(&game);
    target->life=ALIVE;target->position=target->previous=add(actor->position,v3(30,0,0));
    target->health=1;target->vest=100;target->bonus=BONUS_BERSERKER;target->bonus_ticks=900;
    actor_pose(target,bones);
    bullet_at(&game,AK74,bones[12],v3(10,0,0));combat_step(&game,idle);
    check(target->life==DEAD && target->vest==0 && target->bonus==BONUS_NONE && target->bonus_ticks==0,
        "death removes vest and timed bonus immediately");
    actor_pose(actor,bones);
    bullet_at(&game,AK74,bones[12],v3(10,0,0));game.projectiles[0].owner=1;
    combat_step(&game,idle);
    check(fabsf(actor->health-(SRC_DEFAULT_HEALTH-amount))<.001f,
        "dead berserker's in-flight bullet no longer gains bonus damage");

    game_free(&game);ragdolls_free();poses_free();world_free();
    puts("Pickup eligibility, magazines, timers, bonuses, knife recovery, clusters and armor pass");
    return EXIT_SUCCESS;
}
