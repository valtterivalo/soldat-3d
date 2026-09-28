#include "game.h"
#include "objectives.h"
#include "world.h"
#include "pose.h"
#include "ragdoll.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition,const char *message) {
    if (condition) return;
    fprintf(stderr,"objective: %s\n",message);
    exit(EXIT_FAILURE);
}

static void reset(Game *game,GameMode mode,const char *map,int players) {
    printf("%s objectives on %s\n",game_mode_names[mode],map);
    fflush(stdout);
    game_free(game);
    world_load(world_map_index(map));
    game_init(game,0x666c6167u,mode);
    for (int i=players;i<ACTOR_COUNT;++i) game->actors[i].life=INACTIVE;
    game_set_mode(game,mode);
    for (int i=0;i<players;++i) {
        game->actors[i].position=game->actors[i].previous=v3(i*50,10000,0);
        game->actors[i].spawn_protection_ticks=-1;
    }
}

static void place(Actor *actor,Vec3 flag) {
    actor->position=actor->previous=sub(flag,v3(0,8,0));
    actor->velocity=v3(0,0,0);
}

static void shoot(Game *game,int owner,int target) {
    Vec3 bones[21];actor_pose(&game->actors[target],bones);
    game->projectiles=realloc(game->projectiles,sizeof(*game->projectiles));
    if (!game->projectiles) abort();
    game->projectile_count=game->projectile_capacity=1;
    game->projectiles[0]=(Projectile){.weapon=AK74,.owner=owner,.position=bones[12],.previous=bones[12],
        .initial=bones[12],.velocity=v3(10,0,0),.ticks=weapons[AK74].timeout-21,.hit_multiply=weapons[AK74].hit_multiply};
    Input idle[ACTOR_COUNT]={0};combat_step(game,idle);
}

int main(void) {
    world_init();
    poses_init();ragdolls_init();
    Game game={0};Input idle[ACTOR_COUNT]={0};
    reset(&game,MODE_CTF,"ctf_Ash",3);
    check(game.actors[0].team==TEAM_ALPHA && game.actors[1].team==TEAM_BRAVO && game.actors[2].team==TEAM_ALPHA,
        "automatic teams balance active players");
    game_respawn(&game,0);
    int source_spawn=0;
    for (size_t i=0;i<world_team_spawn_count(TEAM_ALPHA);++i)
        source_spawn|=length(sub(game.actors[0].position,world_team_spawn(TEAM_ALPHA,i)))==0;
    check(source_spawn,"team respawn chooses original team markers");
    place(&game.actors[0],game.flags[1].position);
    objectives_step(&game,idle);
    check(game.actors[0].carried_flag==FLAG_NONE,"spawn protection prevents immediate flag pickup");
    game.actors[0].spawn_protection_ticks=0;
    objectives_step(&game,idle);
    check(game.actors[0].carried_flag==FLAG_BRAVO && game.flags[1].carrier==0,"enemy flag attaches to carrier");
    game.actors[0].spawn_protection_ticks=-1;
    game.flags[0].state=FLAG_DROPPED;game.flags[0].position=add(game.flags[0].base,v3(200,100,0));
    place(&game.actors[0],game.flags[0].base);
    objectives_step(&game,idle);
    check(game.team_score[TEAM_ALPHA]==0 && game.actors[0].carried_flag==FLAG_BRAVO,
        "capture requires own flag at base");
    place(&game.actors[2],game.flags[0].position);
    objectives_step(&game,idle);
    check(game.flags[0].state==FLAG_BASE && game.actors[0].carried_flag==FLAG_NONE && game.team_score[TEAM_ALPHA]==1 &&
        game.actors[0].captures==1,"touch return unlocks capture and restores both flags");
    game.actors[2].position=v3(500,10000,0);
    place(&game.actors[0],game.flags[1].position);objectives_step(&game,idle);
    game.actors[0].position=v3(0,10000,0);
    game.actors[0].yaw=0;
    idle[0].pressed=INPUT_FLAG_THROW;objectives_step(&game,idle);idle[0].pressed=0;
    check(game.actors[0].carried_flag==FLAG_NONE && game.actors[0].flag_grab_cooldown==TICK_RATE/4 &&
        game.flags[1].state==FLAG_DROPPED && game.flags[1].velocity.z>4,
        "flag throw inherits aim velocity and blocks immediate re-grab");
    game.flags[1].ticks=1;objectives_step(&game,idle);
    check(game.flags[1].state==FLAG_BASE && game.flags[1].ticks==SRC_FLAG_TIMEOUT,"dropped flag returns after source timeout");
    place(&game.actors[0],game.flags[1].position);game.actors[0].flag_grab_cooldown=0;objectives_step(&game,idle);
    int respawn=game.wave_counter+SRC_RESPAWNTIME_MINWAVE;
    combat_environment(&game,0,5);
    check(game.actors[0].life==DEAD && game.actors[0].carried_flag==FLAG_NONE && game.flags[1].state==FLAG_DROPPED &&
        game.actors[0].respawn_ticks==respawn,"carrier death drops objective and joins source respawn wave");
    game.tick=700;game.score_limit=17;game.friendly_fire=1;
    strcpy(game.names[0],"Flagger");game.actors[0].loadout[0]=SPAS12;
    game_restart(&game);
    check(game.tick==700 && game.match_ticks==0 && game.score_limit==17 && game.friendly_fire==1 &&
        game.actors[0].team==TEAM_ALPHA && game.actors[0].slots[0].id==SPAS12 && !strcmp(game.names[0],"Flagger") &&
        game.team_score[TEAM_ALPHA]==0 && game.flags[1].state==FLAG_BASE,
        "restart preserves player identity and settings while resetting match state");
    game_select_team(&game,0,TEAM_SPECTATOR);
    check(game.actors[0].life==INACTIVE,"spectator selection removes actor from simulation");
    game_select_team(&game,0,TEAM_NONE);
    check(game.actors[0].life==ALIVE && game.actors[0].team!=TEAM_NONE,"automatic team selection rejoins play");
    game.flags[1].position=v3(0,10000,200);game.flags[1].state=FLAG_DROPPED;
    game.projectiles=realloc(game.projectiles,sizeof(*game.projectiles));
    if (!game.projectiles) abort();
    game.projectile_count=game.projectile_capacity=1;
    game.projectiles[0]=(Projectile){.weapon=AK74,.owner=0,.position=game.flags[1].position,
        .velocity=v3(10,0,0),.ticks=weapons[AK74].timeout-2,.hit_multiply=weapons[AK74].hit_multiply};
    combat_step(&game,idle);
    check(game.flags[1].velocity.x>0 && game.projectiles[0].flag_hit_ticks[1]==SRC_THING_COLLISION_COOLDOWN,
        "bullets push flags and record source per-flag push cooldown");

    reset(&game,MODE_TEAMMATCH,"ctf_Ash",2);
    game.actors[1].spawn_protection_ticks=0;game.actors[1].health=1;
    shoot(&game,0,1);
    check(game.actors[1].health==1,"last spawn-protected tick still blocks incoming bullets");
    game_select_team(&game,1,TEAM_ALPHA);
    game.actors[1].spawn_protection_ticks=-1;
    game.actors[0].position=v3(0,10000,0);game.actors[1].position=v3(50,10000,0);
    game.actors[1].health=1;shoot(&game,0,1);
    check(game.actors[1].health==1,"friendly fire disabled rejects teammate damage");
    game.friendly_fire=1;shoot(&game,0,1);
    check(game.actors[1].life==DEAD && game.actors[0].kills==0 && game.team_score[TEAM_ALPHA]==0,
        "friendly fire enabled permits death without awarding teamkill points");
    game_select_team(&game,1,TEAM_BRAVO);game.actors[1].position=v3(50,10000,0);game.actors[1].health=1;
    game.actors[1].spawn_protection_ticks=-1;
    shoot(&game,0,1);
    check(game.actors[0].kills==1 && game.team_score[TEAM_ALPHA]==1,"enemy kill increments personal and team battle score");
    game.score_limit=1;objectives_step(&game,idle);
    check(game.phase==MATCH_FINISHED,"team score limit finishes match");

    reset(&game,MODE_POINTMATCH,"Arena",2);
    game.actors[0].carried_flag=FLAG_YELLOW;
    game.flags[2].state=FLAG_CARRIED;game.flags[2].carrier=0;
    for (int i=0;i<3;++i) objectives_kill(&game,1,0,AK74);
    check(game.actors[0].kills==8,"pointmatch flag doubles score and third quick kill gains multikill multiplier");
    game.actors[0].multikill_ticks=1;objectives_step(&game,idle);
    check(game.actors[0].multikills==0,"source multikill window expires");
    objectives_kill(&game,1,0,AK74);
    check(game.actors[0].kills==10,"expired multikill does not amplify next flag kill");

    reset(&game,MODE_INF,"inf_Outpost",2);
    game.match_ticks=SRC_INF_BLUELIMIT*TICK_RATE;objectives_step(&game,idle);
    check(game.team_score[TEAM_BRAVO]==1,"infiltration defenders score while objective stays home");
    place(&game.actors[0],game.flags[1].position);objectives_step(&game,idle);
    game.match_ticks=SRC_INF_BLUELIMIT*TICK_RATE*2;objectives_step(&game,idle);
    check(game.team_score[TEAM_BRAVO]==2,"defenders still score while held objective remains inside source base radius");
    game.actors[0].position=add(game.actors[0].position,v3(100,0,0));
    game.match_ticks=SRC_INF_BLUELIMIT*TICK_RATE*3;objectives_step(&game,idle);
    check(game.team_score[TEAM_BRAVO]==2,"carried infiltration objective outside base pauses defender scoring");
    place(&game.actors[0],game.flags[0].position);objectives_step(&game,idle);
    check(game.team_score[TEAM_ALPHA]==SRC_INF_REDAWARD && game.flags[1].state==FLAG_BASE,
        "infiltration capture returns black objective and gives thirty points");
    place(&game.actors[1],game.flags[0].position);objectives_step(&game,idle);
    check(game.actors[1].carried_flag==FLAG_NONE,"white infiltration base cannot be carried");

    reset(&game,MODE_HTF,"htf_Nuclear",2);
    place(&game.actors[0],game.flags[2].position);objectives_step(&game,idle);
    game.match_ticks=SRC_HTF_POINTSTIME*TICK_RATE;objectives_step(&game,idle);
    check(game.team_score[TEAM_ALPHA]==1,"hold the flag awards team point at source interval");
    game.actors[2].team=TEAM_SPECTATOR;
    game_select_team(&game,2,TEAM_ALPHA);
    game.match_ticks=SRC_HTF_POINTSTIME*TICK_RATE*2;objectives_step(&game,idle);
    check(game.team_score[TEAM_ALPHA]==2 && game.htf_interval==SRC_HTF_SEC_POINT+2*TICK_RATE,
        "larger flag-holding team receives original scoring delay");
    game.score_limit=0;game.time_limit_ticks=game.match_ticks+1;objectives_step(&game,idle);
    check(game.phase==MATCH_FINISHED,"time limit finishes match independently of score");

    reset(&game,MODE_CTF,"ctf_Ash",1);
    game_restart(&game);
    game.score_limit=1;
    uint32_t previous=0;
    unsigned airborne=0,jet_ticks=0;
    while (game.phase==MATCH_PLAYING) {
        Input inputs[ACTOR_COUNT]={0};
        inputs[0]=bot_input(&game,0);
        inputs[0].pressed=inputs[0].held&~previous;
        previous=inputs[0].held;
        jet_ticks+=(inputs[0].held&INPUT_JETS)!=0;
        game_step(&game,inputs);
        airborne+=game.actors[0].contact==AIRBORNE;
        check(world_pose_clear(game.actors[0].position,game.actors[0].pose),
            "objective navigation stays outside solid terrain");
    }
    check(game.team_score[TEAM_ALPHA]==1 && game.actors[0].captures==1 && game.actors[0].deaths==0,
        "bot traverses the real map, grabs enemy flag and returns it before match time expires");
    printf("CTF bot capture: %d ticks, %u airborne ticks, %u jet ticks\n",game.match_ticks,airborne,jet_ticks);

    game_free(&game);world_free();ragdolls_free();poses_free();
    puts("Team scoring, friendly fire, flag capture/return/drop, Pointmatch, Infiltration, HTF and restart pass");
    return 0;
}
