#include "bot.h"
#include "bot_navigation.h"
#include "objectives.h"
#include "pose.h"
#include "world.h"
#include "generated_rules.h"

static float random_unit(BotMemory *memory) {
    uint32_t x=memory->random;
    x^=x<<13;x^=x>>17;x^=x<<5;
    memory->random=x;
    return (float)(x>>8)/16777216.0f;
}

static int opponent(const Game *game,int self,int other) {
    return self!=other && game->actors[other].life==ALIVE &&
        (!game_team_mode(game->mode) || game->actors[self].team!=game->actors[other].team);
}

static Vec3 chest(const Actor *actor) {
    Vec3 bones[21];actor_pose(actor,bones);
    return scale(add(bones[10],bones[11]),.5f);
}

static int visible(const Actor *actor,Vec3 origin,Vec3 target) {
    return world_trace_for(origin,target,v3(0,0,0),
        (WorldQuery){WORLD_TRACE_BULLET,actor->team,WORLD_NO_FLAG}).box<0;
}

typedef struct {Vec3 target;float time;int reachable;} Aim;

static Aim intercept(const Actor *actor,Vec3 target,Vec3 velocity,float speed,float inherited,int lifetime) {
    Vec3 origin=actor_muzzle(actor);
    float decay=1,sum=0,previous=INFINITY;
    for(int tick=1;tick<=lifetime;++tick) {
        sum+=decay;decay*=SRC_BULLET_DAMPING;
        float drop=SRC_BULLET_GRAVITY*((float)tick-SRC_BULLET_DAMPING*sum)/(1-SRC_BULLET_DAMPING);
        Vec3 predicted=add(target,scale(velocity,(float)tick));
        predicted.y+=drop;
        predicted=sub(predicted,scale(actor->velocity,inherited*sum));
        float excess=length(sub(predicted,origin))-speed*sum;
        if(excess<=0) {
            float fraction=tick==1 ? 1 : previous/(previous-excess);
            float time=(float)(tick-1)+fraction;
            float flight=(1-powf(SRC_BULLET_DAMPING,time))/(1-SRC_BULLET_DAMPING);
            predicted=add(target,scale(velocity,time));
            predicted.y+=SRC_BULLET_GRAVITY*(time-SRC_BULLET_DAMPING*flight)/(1-SRC_BULLET_DAMPING);
            predicted=sub(predicted,scale(actor->velocity,inherited*flight));
            return (Aim){predicted,time,1};
        }
        previous=excess;
    }
    return (Aim){target,0,0};
}

static int blast_safe(const Game *game,int self,Vec3 point,float radius,float time) {
    for(int i=0;i<ACTOR_COUNT;++i) {
        const Actor *actor=&game->actors[i];
        if(actor->life!=ALIVE || (i!=self &&
            (!game_team_mode(game->mode) || actor->team!=game->actors[self].team)))continue;
        Vec3 predicted=add(chest(actor),scale(actor->velocity,time));
        if(length(sub(predicted,point))<radius+SRC_PART_RADIUS)return 0;
    }
    return 1;
}

static int trajectory_clear(const Game *game,int index,Vec3 aim,float speed,float inherited,float time,float radius) {
    const Actor *actor=&game->actors[index];
    Actor firing=*actor;
    firing.yaw=atan2f(aim.x,aim.z);firing.pitch=asinf(aim.y);
    Vec3 position=actor_muzzle(&firing),velocity=add(scale(aim,speed),scale(actor->velocity,inherited));
    Vec3 shoulder=chest(&firing);
    Vec3 impact=combat_aim_target(game,index,shoulder,position,NULL);
    if(length(sub(impact,position))>.001f)return blast_safe(game,index,impact,radius,0);
    for(int tick=0;tick<(int)ceilf(time);++tick) {
        Vec3 end=add(position,scale(velocity,fminf(1,time-(float)tick)));
        if(!visible(actor,position,end))return 0;
        impact=combat_aim_target(game,index,position,end,NULL);
        if(length(sub(impact,end))>.001f)return blast_safe(game,index,impact,radius,(float)tick);
        velocity.y-=SRC_BULLET_GRAVITY;
        position=add(position,velocity);
        velocity=scale(velocity,SRC_BULLET_DAMPING);
    }
    return 1;
}

static float turn(float error,float *speed) {
    float requested=fmaxf(-.045f,fminf(.045f,error*.16f));
    *speed+=fmaxf(-.003f,fminf(.003f,requested-*speed));
    return *speed;
}

Input bot_input(Game *game,int index) {
    Actor *actor=&game->actors[index];
    BotMemory *memory=&game->bots[index];
    Input input={.yaw=actor->yaw,.pitch=actor->pitch};
    if(actor->life!=ALIVE || game->phase==MATCH_FINISHED)return input;
    int fresh=memory->spawn_id!=actor->spawn_id;
    if(fresh) {
        *memory=(BotMemory){.spawn_id=actor->spawn_id,.target=-1,.roam_node=-1,
            .random=(0x9e3779b9u^(uint32_t)(index+1)*0x85ebca6bu^actor->spawn_id*0xc2b2ae35u)|1u,
            .observed_ammo=actor->slots[actor->active_slot].ammo,
            .observed_weapon=actor->slots[actor->active_slot].id};
    }
    Vec3 eye=chest(actor);
    if(fresh || game->tick>=memory->perceive_tick) {
        memory->perceive_tick=game->tick+6;
        int selected=-1;float best=INFINITY;
        for(int i=0;i<ACTOR_COUNT;++i) {
            if(!opponent(game,index,i))continue;
            const Actor *other=&game->actors[i];
            Vec3 point=chest(other),delta=sub(point,eye);
            float distance=length(delta);
            int heard=other->controls&INPUT_FIRE;
            if(distance>70 && dot(delta,direction(actor->yaw,actor->pitch))<.15f*distance &&
                !(heard && distance<450))continue;
            if(!visible(actor,eye,point))continue;
            float score=distance*(i==memory->target && other->spawn_id==memory->target_spawn ? .62f : 1);
            if(other->carried_flag!=FLAG_NONE)score*=.65f;
            if(other->spawn_protection_ticks>=0)score*=2;
            if(score>=best)continue;
            best=score;selected=i;
        }
        if(selected>=0) {
            const Actor *other=&game->actors[selected];
            if(memory->target!=selected || memory->target_spawn!=other->spawn_id) {
                memory->reaction_ticks=14+(int)(random_unit(memory)*10);
                memory->burst_remaining=0;
                memory->target_position=chest(other);memory->target_tick=game->tick;
                memory->target_velocity=memory->observed_velocity=v3(0,0,0);
            } else {
                memory->target_position=memory->observed_position;memory->target_tick=memory->seen_tick;
                memory->target_velocity=memory->observed_velocity;
                memory->observed_velocity=scale(sub(chest(other),memory->observed_position),
                    1/(float)(game->tick-memory->seen_tick));
            }
            memory->target=selected;memory->target_spawn=other->spawn_id;
            memory->observed_position=chest(other);memory->seen_tick=game->tick;
        } else if(memory->target>=0 && (game->tick-memory->seen_tick>90 ||
            !opponent(game,index,memory->target) || game->actors[memory->target].spawn_id!=memory->target_spawn))
            memory->target=-1;
    }
    if(memory->reaction_ticks>0)--memory->reaction_ticks;
    if(memory->burst_pause>0)--memory->burst_pause;
    int target=memory->target;
    int seen=target>=0 && game->tick-memory->seen_tick<6 &&
        game->actors[target].spawn_id==memory->target_spawn && opponent(game,index,target);
    Vec3 target_point=target<0 ? eye : add(memory->target_position,
        scale(memory->target_velocity,(float)(game->tick-memory->target_tick)));
    float distance=length(sub(target_point,eye));
    int clustered=0;
    if(seen)for(int i=0;i<ACTOR_COUNT;++i)
        if(opponent(game,index,i) && length(sub(chest(&game->actors[i]),target_point))<SRC_M79GRENADE_EXPLOSION_RADIUS &&
            visible(actor,eye,chest(&game->actors[i])))++clustered;
    WeaponState *gun=&actor->slots[actor->active_slot];
    if(memory->observed_weapon!=gun->id) {
        memory->observed_weapon=gun->id;memory->observed_ammo=gun->ammo;
        memory->burst_remaining=0;memory->burst_pause=0;
    }
    int fired=memory->observed_ammo>gun->ammo ? memory->observed_ammo-gun->ammo : 0;
    memory->observed_ammo=gun->ammo;
    if(fired) {
        memory->burst_remaining-=fired;
        if(weapons[gun->id].fire_mode==2)memory->burst_pause=weapons[gun->id].fire_interval+8+(int)(random_unit(memory)*8);
        else if(memory->burst_remaining<=0)memory->burst_pause=18+(int)(random_unit(memory)*18);
    }
    int desired_slot=actor->active_slot;
    int law_slot=actor->slots[1].id==LAW ? 1 : actor->slots[0].id==LAW ? 0 : -1;
    int law_opportunity=seen && distance>110 && distance<550 && actor->contact==GROUNDED &&
        clustered>=2 && blast_safe(game,index,target_point,SRC_M79GRENADE_EXPLOSION_RADIUS,distance/weapons[LAW].speed);
    if(law_slot>=0 && actor->slots[law_slot].ammo>0 && law_opportunity)desired_slot=law_slot;
    else if(gun->id==LAW && (!seen || gun->ammo==0 || distance<100))desired_slot=1-actor->active_slot;
    else if(actor->active_slot==0 && gun->ammo==0 && seen && distance<280 &&
        actor->slots[1].ammo>0 && actor->slots[1].id==COLT)desired_slot=1;
    else if(actor->active_slot==1 && gun->id==COLT && (!seen || actor->slots[0].ammo>0))desired_slot=0;
    if(desired_slot!=actor->active_slot && actor->switch_ticks==0 && memory->grenade_hold==0)input.held|=INPUT_SWITCH;
    const WeaponDef *weapon=&weapons[gun->id];
    int marksman=gun->id==BARRETT || gun->id==RUGER77;
    Vec3 approach=target<0 ? actor->position : sub(target_point,v3(0,8,0));
    BotMoveIntent intent=BOT_TRAVERSE;
    int objective=game->mode==MODE_CTF || game->mode==MODE_INF || game->mode==MODE_POINTMATCH || game->mode==MODE_HTF;
    if(target<0 && !objective) {
        if(memory->roam_node<0 || length(sub(world_nav_nodes[memory->roam_node].position,actor->position))<20)
            memory->roam_node=(int)(random_unit(memory)*(float)world_nav_node_count);
        approach=world_nav_nodes[memory->roam_node].position;
    }
    approach=objectives_target(game,index,approach);
    int precision=seen && (gun->id==LAW || gun->id==BARRETT || gun->id==RUGER77) && distance>90;
    int reloading=gun->phase==WEAPON_RELOADING || gun->ammo==0;
    if(seen && !objective) {
        float range=gun->id==BARRETT ? 460 : gun->id==RUGER77 ? 320 :
            gun->id==LAW ? 200 : gun->id==SPAS12 || gun->id==FLAMER ? 85 :
            gun->id==CHAINSAW || gun->id==KNIFE || gun->id==NOWEAPON ? 16 : 210;
        Vec3 separation=sub(actor->position,sub(target_point,v3(0,8,0)));
        float horizontal=sqrtf(separation.x*separation.x+separation.z*separation.z);
        if(horizontal>1)approach=add(sub(target_point,v3(0,8,0)),
            scale(v3(separation.x,0,separation.z),range/horizontal));
        intent=precision ? BOT_HOLD : BOT_ENGAGE;
        if(precision)approach=actor->position;
    }
    if(precision && actor->carried_flag==FLAG_NONE) {intent=BOT_HOLD;approach=actor->position;}
    if(target>=0 && (reloading || (actor->health<55 && actor->hit_ticks>0))) {
        if(game->tick>=memory->cover_tick) {
            memory->cover_tick=game->tick+60;memory->cover=actor->position;
            float best=INFINITY;
            for(size_t i=0;i<world_nav_node_count;++i) {
                Vec3 point=world_nav_nodes[i].position;
                float travel=length(sub(point,actor->position));
                if(travel>170 || travel<12 || visible(actor,target_point,add(point,v3(0,8,0))))continue;
                float score=travel+.12f*length(sub(point,target_point));
                if(score<best){best=score;memory->cover=point;}
            }
        }
        approach=memory->cover;intent=BOT_RETREAT;
    }
    if(!seen && gun->phase==WEAPON_READY && gun->ammo<weapon->ammo/2)input.held|=INPUT_RELOAD;
    Vec3 lead=scale(memory->target_velocity,marksman ? .8f+.35f*memory->aim_error.z : 1);
    Aim solution=target>=0 ? intercept(actor,target_point,lead,weapon->speed,
        weapon->inherited_velocity,weapon->timeout) : (Aim){add(approach,v3(0,8,0)),0,0};
    int throwing=memory->grenade_hold>0;
    if(!throwing && seen && memory->reaction_ticks==0 && actor->spawn_protection_ticks<0 &&
        actor->grenades>0 && actor->grenade_charge==0 && actor->contact==GROUNDED &&
        actor->switch_ticks==0 && game->tick>=memory->grenade_tick && distance>90 && distance<260 &&
        (clustered>=2 || reloading) && gun->id!=LAW && gun->id!=BARRETT) {
        for(int charge=24;charge<=36;++charge) {
            float speed=(float)charge/weapons[actor->grenade_weapon].speed;
            Aim arc=intercept(actor,target_point,memory->target_velocity,speed,
                weapons[actor->grenade_weapon].inherited_velocity,SRC_GRENADE_TIMEOUT);
            Vec3 impact=add(target_point,scale(memory->target_velocity,arc.time));
            if(!arc.reachable || !blast_safe(game,index,impact,SRC_FRAGGRENADE_EXPLOSION_RADIUS,arc.time))continue;
            Vec3 aim=actor_aim_direction(actor,arc.target);
            if(!trajectory_clear(game,index,aim,speed,weapons[actor->grenade_weapon].inherited_velocity,
                arc.time,SRC_FRAGGRENADE_EXPLOSION_RADIUS))continue;
            memory->grenade_hold=charge;memory->grenade_target=impact;throwing=1;break;
        }
    }
    if(throwing) {
        float speed=(float)memory->grenade_hold/weapons[actor->grenade_weapon].speed;
        solution=intercept(actor,memory->grenade_target,v3(0,0,0),speed,
            weapons[actor->grenade_weapon].inherited_velocity,SRC_GRENADE_TIMEOUT);
        intent=BOT_HOLD;approach=actor->position;
    }
    if(game->tick>=memory->aim_tick) {
        memory->aim_tick=game->tick+30+(uint64_t)(random_unit(memory)*30);
        memory->aim_bias=v3(random_unit(memory)*2-1,random_unit(memory)*2-1,random_unit(memory)*2-1);
    }
    memory->aim_error=add(memory->aim_error,scale(sub(memory->aim_bias,memory->aim_error),.08f));
    float error=marksman ? fminf(distance*.04f,3+distance*.032f) :
        fminf(distance*.025f,precision ? .8f+distance*.006f : 1.5f+distance*.014f);
    if(throwing)error=0;
    Vec3 desired=actor_aim_direction(actor,add(solution.target,scale(memory->aim_error,error)));
    float yaw=atan2f(desired.x,desired.z),pitch=asinf(desired.y);
    if(throwing) {
        float low=-1.57079632679f,high=1.57079632679f;
        while(high-low>.00001f) {
            float angle=(low+high)*.5f,horizontal=cosf(angle),vertical=sinf(angle);
            float arc=.125f*(1-fabsf(vertical));
            float thrown=atan2f(vertical+sinf(horizontal*1.57079632679f)*arc,
                horizontal-sinf(vertical*1.57079632679f)*arc);
            if(thrown<pitch)low=angle;else high=angle;
        }
        pitch=(low+high)*.5f;
    }
    float yaw_error=atan2f(sinf(yaw-actor->yaw),cosf(yaw-actor->yaw));
    if(memory->reaction_ticks>0) {yaw_error=0;pitch=actor->pitch;}
    input.yaw=actor->yaw+turn(yaw_error,&memory->yaw_speed);
    input.pitch=fmaxf(-1.57079632679f,fminf(1.57079632679f,
        actor->pitch+turn(pitch-actor->pitch,&memory->pitch_speed)));
    memory->pitch_speed=input.pitch-actor->pitch;
    bot_navigation(game,index,approach,intent,&input);
    int settled=actor->contact==GROUNDED && actor->velocity.x*actor->velocity.x+actor->velocity.z*actor->velocity.z<.16f;
    if(precision && intent==BOT_HOLD && settled && !memory->nav_dodge_projectile && !memory->nav_neighbors) {
        input.forward=input.right=0;
        input.held&=~(INPUT_JUMP|INPUT_JETS);
        input.held|=INPUT_CROUCH;
        if(gun->id==BARRETT && distance>420 && actor->pose!=PRONE && actor->animation!=MOVE_GETUP)
            input.held|=INPUT_PRONE;
    } else if(actor->pose==PRONE && actor->animation!=MOVE_GETUP)input.held|=INPUT_PRONE;
    float alignment=dot(direction(input.yaw,input.pitch),direction(yaw,pitch));
    float tolerance=atan2f(7,distance)+.012f;
    if(weapon->startup_time>0 && gun->startup_count<weapon->startup_time)tolerance+=.05f;
    int clear=seen && visible(actor,actor_muzzle(actor),target_point);
    if(throwing) {
        float speed=(float)memory->grenade_hold/weapons[actor->grenade_weapon].speed;
        int safe=solution.reachable && trajectory_clear(game,index,desired,speed,
            weapons[actor->grenade_weapon].inherited_velocity,solution.time,SRC_FRAGGRENADE_EXPLOSION_RADIUS);
        if(!safe && actor->grenade_charge<15) {
            memory->grenade_hold=0;memory->grenade_tick=game->tick+TICK_RATE;
            input.pressed=input.held&~actor->controls;
            return input;
        }
        if(actor->grenade_charge==0 && alignment<cosf(.035f)) {
            input.pressed=input.held&~actor->controls;
            return input;
        }
        if(actor->grenade_charge<memory->grenade_hold)input.held|=INPUT_GRENADE;
        else {memory->grenade_hold=0;memory->grenade_tick=game->tick+4*TICK_RATE;}
    } else if(clear && solution.reachable && memory->reaction_ticks==0 &&
        game->actors[target].spawn_protection_ticks<0 && actor->spawn_protection_ticks<0 &&
        (gun->ammo>0 || gun->id==NOWEAPON) &&
        actor->switch_ticks==0 && memory->burst_pause==0 && alignment>=cosf(tolerance) &&
        (weapon->fire_mode!=2 || gun->fire_count<=1) &&
        (!precision || (settled && actor->bink_count<4)) &&
        (gun->id!=LAW || (blast_safe(game,index,add(target_point,scale(memory->target_velocity,solution.time)),
            SRC_M79GRENADE_EXPLOSION_RADIUS,solution.time) && trajectory_clear(game,index,
                direction(input.yaw,input.pitch),weapon->speed,weapon->inherited_velocity,solution.time,
                SRC_M79GRENADE_EXPLOSION_RADIUS)))) {
        if(memory->burst_remaining<=0)
            memory->burst_remaining=gun->id==M249 || gun->id==MINIGUN ? 5+(int)(random_unit(memory)*4) : 3+(int)(random_unit(memory)*3);
        input.held|=INPUT_FIRE;
    }
    input.pressed=input.held&~actor->controls;
    return input;
}
