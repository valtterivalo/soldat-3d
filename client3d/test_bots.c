#include "bot.h"
#include "pose.h"
#include "ragdoll.h"
#include "world.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int pass,const char *contract) {
    if(pass)return;
    fprintf(stderr,"Bot contract failed: %s\n",contract);
    exit(EXIT_FAILURE);
}

static Game encounter(WeaponId primary,WeaponId secondary,float minimum,float maximum,int count) {
    Game game;game_init(&game,0x626f7473u,MODE_DEATHMATCH);
    game.score_limit=game.time_limit_ticks=0;
    for(int i=0;i<ACTOR_COUNT;++i)game.actors[i].life=i<count ? ALIVE : INACTIVE;
    Vec3 from={0},to={0};int found=0;
    for(size_t a=0;a<world_nav_node_count && !found;++a)for(size_t b=0;b<world_nav_node_count && !found;++b) {
        Vec3 x=world_nav_nodes[a].position,y=world_nav_nodes[b].position;
        float distance=length(sub(x,y));
        if(distance<minimum || distance>maximum || fabsf(x.y-y.y)>1)continue;
        if(world_trace(add(x,v3(0,10,0)),add(y,v3(0,10,0)),v3(0,0,0)).box>=0)continue;
        Vec3 side=v3(-(y.z-x.z)/distance,0,(y.x-x.x)/distance);
        if(primary==BARRETT && (world_trace(add(y,v3(0,7,0)),add(add(y,v3(0,7,0)),scale(side,35)),v3(3,6.8f,3)).box>=0 ||
            world_trace(add(y,v3(0,7,0)),sub(add(y,v3(0,7,0)),scale(side,35)),v3(3,6.8f,3)).box>=0))continue;
        Vec3 extra=add(y,v3(0,0,20));
        if(count>2 && (!world_pose_clear(extra,STANDING) ||
            world_trace(add(extra,v3(0,1,0)),sub(extra,v3(0,2,0)),v3(0,0,0)).box<0))continue;
        from=x;to=y;found=1;
    }
    check(found,"arena supplies a clear encounter at the requested range");
    for(int i=0;i<count;++i) {
        Actor *actor=&game.actors[i];
        actor->position=actor->previous=i==0 ? from : add(to,v3(0,0,(float)(i-1)*20));
        actor->contact=GROUNDED;actor->velocity=actor->force=v3(0,0,0);
        actor->spawn_protection_ticks=-1;actor->grenades=0;
        actor->nav_edge=actor->nav_goal=-1;
        game_equip(actor,i==0 ? primary : AK74,i==0 ? secondary : COLT);
    }
    Vec3 target=sub(to,from);
    game.actors[0].yaw=atan2f(target.x,target.z)-.6f;
    return game;
}

int main(void) {
    world_init();poses_init();ragdolls_init();
    for(int sign=-1;sign<=1;sign+=2) {
        Game game=encounter(AK74,COLT,145,210,2);
        (void)bot_input(&game,0);
        game.actors[0].pitch=sign*(1.57079632679f-.01f);
        game.bots[0].pitch_speed=sign*.075f;
        for(int tick=0;tick<60;++tick) {
            Input input=bot_input(&game,0);
            check(input.pitch>=-1.57079632679f && input.pitch<=1.57079632679f,
                "smoothed aim cannot cross the physical pitch poles or generate rejected network inputs");
            check(fabsf(game.bots[0].pitch_speed-(input.pitch-game.actors[0].pitch))<.000001f,
                "pitch smoothing retains only the turn actually applied at a pole");
            game.actors[0].pitch=input.pitch;game.actors[0].yaw=input.yaw;++game.tick;
        }
        game_free(&game);
    }
    for(int distance=0;distance<=30;distance+=5) {
        Game game;game_init(&game,555,MODE_DEATHMATCH);
        for(int i=2;i<ACTOR_COUNT;++i)game.actors[i].life=INACTIVE;
        game.actors[1].position=add(game.actors[0].position,v3(0,0,(float)distance));
        float late_turning=0,previous=0;
        for(int tick=0;tick<180;++tick) {
            Input input=bot_input(&game,0);
            float delta=atan2f(sinf(input.yaw-game.actors[0].yaw),cosf(input.yaw-game.actors[0].yaw));
            check(fabsf(delta)<=.0751f && fabsf(delta-previous)<=.0081f,
                "close and colocated targets cannot create a heading flip");
            if(tick>=60)late_turning+=fabsf(delta);
            previous=delta;game.actors[0].yaw=input.yaw;game.actors[0].pitch=input.pitch;
            ++game.tick;
        }
        check(late_turning<1.5f,"aim settles after acquiring even a colocated target");
        game_free(&game);
    }
    Game retention=encounter(AK74,COLT,145,210,3);
    (void)bot_input(&retention,0);
    int retained=retention.bots[0].target,challenger=retained==1 ? 2 : 1;
    check(retained>=1,"perception acquires a visible opponent");
    Vec3 offset=sub(retention.actors[retained].position,retention.actors[0].position);
    Vec3 lateral=scale(v3(-offset.z,0,offset.x),20/length(offset));
    retention.actors[challenger].position=add(retention.actors[0].position,add(scale(offset,.9f),lateral));
    check(world_trace(add(retention.actors[0].position,v3(0,10,0)),
        add(retention.actors[challenger].position,v3(0,10,0)),v3(0,0,0)).box<0,
        "crossing opponent remains visible");
    retention.tick+=4;
    (void)bot_input(&retention,0);
    check(retention.bots[0].target==retained,"a slightly closer opponent cannot repeatedly steal attention");
    retention.actors[retained].life=INACTIVE;retention.tick+=4;
    Input acquisition=bot_input(&retention,0);
    check(retention.bots[0].target==challenger && !(acquisition.held&INPUT_FIRE),
        "target loss reacquires a visible opponent with a fresh reaction interval");
    game_free(&retention);
    const WeaponId primaries[]={AK74,M249,BARRETT,AK74,AK74,AK74,AK74};
    const WeaponId secondaries[]={COLT,COLT,COLT,LAW,COLT,COLT,COLT};
    for(int scenario=0;scenario<7;++scenario) {
        Game game=encounter(primaries[scenario],secondaries[scenario],scenario==2 ? 360 : 145,
            scenario==2 ? 600 : 210,scenario==2 ? 2 : 3);
        if(scenario==4)game.actors[0].grenades=2;
        if(scenario==5)game.actors[0].slots[0].ammo=1;
        if(scenario==6)game.actors[0].slots[0].ammo=game.actors[0].slots[1].ammo=0;
        unsigned shots=0,hits=0,kills=0,law=0,grenades=0,bursts=0,pistol_hits=0,moving_hits=0;
        unsigned grenade_hits=0,covered_reload_ticks=0;
        unsigned burst_shots=0,largest_burst=0;uint32_t victims=0;
        Vec3 target_start=game.actors[1].position;
        Vec3 toward=sub(game.actors[0].position,target_start);
        float target_yaw=atan2f(toward.x,toward.z);
        Vec3 side=v3(-cosf(target_yaw),0,sinf(target_yaw));
        float strafe=1,moved=0;
        float previous_turn=0,max_turn=0,max_acceleration=0;
        uint32_t previous=0;
        for(int tick=0;tick<1800;++tick) {
            Input inputs[ACTOR_COUNT]={0};
            uint32_t random=game.random;
            inputs[0]=bot_input(&game,0);
            if(scenario==2) {
                float displacement=dot(sub(game.actors[1].position,target_start),side);
                if(displacement>25)strafe=-1;
                if(displacement< -25)strafe=1;
                inputs[1]=(Input){.yaw=target_yaw,.right=strafe};
                moved=fmaxf(moved,fabsf(displacement));
            }
            check(game.random==random,"bot decisions do not consume combat randomness");
            float turn=atan2f(sinf(inputs[0].yaw-game.actors[0].yaw),cosf(inputs[0].yaw-game.actors[0].yaw));
            max_turn=fmaxf(max_turn,fabsf(turn));
            max_acceleration=fmaxf(max_acceleration,fabsf(turn-previous_turn));previous_turn=turn;
            if(tick<8)check(!(inputs[0].held&INPUT_FIRE),"new targets require a reaction interval");
            if((inputs[0].held&INPUT_FIRE) && !(previous&INPUT_FIRE))++bursts;
            if(!(inputs[0].held&INPUT_FIRE))burst_shots=0;
            previous=inputs[0].held;
            float target_speed=length(game.actors[1].velocity);
            game_step(&game,inputs);
            if(game.actors[0].slots[0].phase==WEAPON_RELOADING &&
                world_trace(add(game.actors[0].position,v3(0,10,0)),
                    add(game.actors[1].position,v3(0,10,0)),v3(0,0,0)).box>=0)++covered_reload_ticks;
            for(size_t i=0;i<game.event_count;++i) {
                const GameEvent *event=&game.events[i];
                if(event->actor!=0)continue;
                if(event->kind==EVENT_SHOT) {
                    ++shots;law+=event->weapon==LAW;grenades+=event->weapon==FRAGGRENADE;
                    if(event->weapon==primaries[scenario] && ++burst_shots>largest_burst)largest_burst=burst_shots;
                    if(event->weapon==LAW)check(game.actors[0].contact==GROUNDED &&
                        game.actors[0].pose!=STANDING,"LAW fires only after a grounded firing stance");
                    if(event->weapon==BARRETT)check(game.actors[0].contact==GROUNDED &&
                        game.actors[0].pose!=STANDING,"Barrett settles into a supported precision stance");
                }
                if(event->kind==EVENT_HIT && event->target!=0) {
                    ++hits;victims|=1u<<event->target;pistol_hits+=event->weapon==COLT;
                    moving_hits+=event->target==1 && target_speed>.5f;
                    grenade_hits+=event->weapon==FRAGGRENADE;
                }
                if(event->kind==EVENT_KILL && event->target!=0)++kills;
            }
            if(kills>=(scenario<2 ? 2u : 1u) && (scenario!=3 || law) && (scenario!=4 || grenades))break;
        }
        printf("weapon %s scenario%d shots%u hits%u kills%u LAW%u grenades%u bursts%u yaw%.5f acceleration%.5f health%.1f\n",
            weapons[primaries[scenario]].name,scenario,shots,hits,kills,law,grenades,bursts,max_turn,max_acceleration,game.actors[0].health);
        fflush(stdout);
        check(max_turn<=.0751f,"aim has a finite human turn speed");
        check(max_acceleration<=.0081f,"aim accelerates without snapping between targets");
        check(hits>0 && kills>0,"ordinary bot inputs produce actual projectile hits and kills");
        if(scenario<2)check((victims&(1u<<1)) && (victims&(1u<<2)) && largest_burst<=9 && bursts>1,
            "automatic bursts transfer damaging fire between opponents");
        if(scenario==2)check(moved>20 && moving_hits>0,"Barrett hits a target moving through real ground physics");
        if(scenario==3)check(law>0 && game.actors[0].health>0,"clustered targets justify a safe LAW shot");
        if(scenario==4)check(grenades>0 && grenade_hits>0 && game.actors[0].health>0,
            "charged grenade trajectory damages a target without self killing");
        if(scenario==5)check(pistol_hits>0,"empty primary prompts useful ordinary secondary fire");
        if(scenario==6)check(covered_reload_ticks>0,"reloading bot reaches real terrain cover before reengaging");
        game_free(&game);
    }
    ragdolls_free();poses_free();world_free();
    puts("bot combat contracts passed");
    return EXIT_SUCCESS;
}
