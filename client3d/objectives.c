#include "objectives.h"
#include "world.h"
#include "pose.h"
#include "generated_rules.h"

void objectives_return(Game *game, FlagId id, int actor, EventKind event) {
    Flag *flag=&game->flags[id-1];
    Vec3 capture=flag->position;
    if (flag->state==FLAG_CARRIED) game->actors[flag->carrier].carried_flag=FLAG_NONE;
    if (id==FLAG_YELLOW) {
        size_t count=world_marker_spawn_count(14);
        flag->base=add(world_marker_spawn(14,(size_t)(game_random(game)*(float)count)),v3(0,8,0));
    }
    flag->position=flag->previous=flag->base;
    flag->velocity=v3(0,0,0);
    flag->state=FLAG_BASE;
    flag->carrier=-1;
    flag->ticks=SRC_FLAG_TIMEOUT;
    combat_event(game,(GameEvent){event,event==EVENT_FLAG_CAPTURE?capture:flag->position,actor,id,NOWEAPON});
}

void objectives_init(Game *game) {
    for (int i=0;i<3;++i) game->flags[i]=(Flag){.carrier=-1};
    for (int i=0;i<ACTOR_COUNT;++i) game->actors[i].carried_flag=FLAG_NONE;
    if (game->mode==MODE_CTF || game->mode==MODE_INF) {
        for (int i=0;i<2;++i) {
            Vec3 base=add(world_objective_spawn((unsigned)i+5),v3(0,8,0));
            game->flags[i]=(Flag){.position=base,.previous=base,.base=base,
                .state=FLAG_BASE,.carrier=-1,.ticks=SRC_FLAG_TIMEOUT};
        }
    }
    if (game->mode==MODE_POINTMATCH || game->mode==MODE_HTF) {
        size_t count=world_marker_spawn_count(14);
        Vec3 base=add(world_marker_spawn(14,(size_t)(game_random(game)*(float)count)),v3(0,8,0));
        game->flags[2]=(Flag){.position=base,.previous=base,.base=base,
            .state=FLAG_BASE,.carrier=-1,.ticks=SRC_FLAG_TIMEOUT};
    }
}

void objectives_drop(Game *game,int index,int throw_flag) {
    Actor *actor=&game->actors[index];
    if (actor->carried_flag==FLAG_NONE) return;
    if (throw_flag && (actor->animation==MOVE_ROLL || actor->animation==MOVE_ROLLBACK)) return;
    FlagId id=actor->carried_flag;
    Flag *flag=&game->flags[id-1];
    Vec3 bones[21];actor_pose(actor,bones);
    Vec3 position=bones[8];
    Vec3 velocity=actor->velocity;
    if (throw_flag) {
        Vec3 impulse=scale(direction(actor->yaw,actor->pitch),SRC_FLAGTHROW_POWER);
        velocity=add(velocity,impulse);
        Vec3 future=add(position,add(scale(impulse,5),velocity));
        WorldHit hit=world_trace_for(position,future,v3(10,8,3),
            (WorldQuery){WORLD_TRACE_ITEM,actor->team,WORLD_HAS_FLAG});
        if (hit.box>=0) return;
        position=future;
        actor->flag_grab_cooldown=TICK_RATE/4;
    }
    flag->position=flag->previous=position;
    flag->velocity=velocity;
    flag->state=FLAG_DROPPED;
    flag->carrier=-1;
    flag->ticks=SRC_FLAG_TIMEOUT;
    actor->carried_flag=FLAG_NONE;
    combat_event(game,(GameEvent){EVENT_FLAG_DROP,position,index,id,NOWEAPON});
}

void objectives_kill(Game *game,int victim,int killer,WeaponId weapon) {
    Actor *dead=&game->actors[victim],*owner=&game->actors[killer];
    dead->respawn_ticks=game_team_mode(game->mode)?game->wave_counter+SRC_RESPAWNTIME_MINWAVE:SRC_RESPAWNTIME;
    if (victim!=killer && (!game_team_mode(game->mode) || dead->team!=owner->team)) {
        int points=1;
        if (game->mode==MODE_POINTMATCH) {
            if (owner->carried_flag==FLAG_YELLOW) points*=2;
            if (owner->multikill_ticks>0 && owner->multikills>=2)
                points*=1<<(owner->multikills>5?5:owner->multikills-1);
        }
        if (game->mode==MODE_RAMBO) {
            WeaponId held=dead->slots[dead->active_slot].id;
            points=weapon==BOW || weapon==BOW2 || held==BOW || held==BOW2;
        }
        owner->kills+=points;
        if (points) {
            owner->multikill_ticks=SRC_MULTIKILLINTERVAL;
            ++owner->multikills;
        }
        if (game->mode==MODE_TEAMMATCH) ++game->team_score[owner->team];
    }
    objectives_drop(game,victim,0);
}

void objectives_step(Game *game,const Input inputs[ACTOR_COUNT]) {
    int teams[5]={0};
    for (int i=0;i<ACTOR_COUNT;++i) {
        Actor *actor=&game->actors[i];
        if (actor->life==INACTIVE) continue;
        ++teams[actor->team];
        if (actor->multikill_ticks>0 && --actor->multikill_ticks==0) actor->multikills=0;
        if (actor->flag_grab_cooldown>0) --actor->flag_grab_cooldown;
        if (actor->life==ALIVE && (inputs[i].pressed&INPUT_FLAG_THROW)) objectives_drop(game,i,1);
    }
    for (int i=0;i<3;++i) {
        Flag *flag=&game->flags[i];
        FlagId id=(FlagId)(i+1);
        if (flag->state==FLAG_ABSENT) continue;
        flag->previous=flag->position;
        if (flag->state==FLAG_CARRIED) {
            Actor *actor=&game->actors[flag->carrier];
            Vec3 bones[21];actor_pose(actor,bones);
            flag->position=bones[8];
            flag->ticks=SRC_FLAG_TIMEOUT;
            continue;
        }
        {
            flag->velocity=scale(flag->velocity,SRC_FLAG_DAMPING);
            flag->velocity.y-=SRC_GRAV;
            Vec3 end=add(flag->position,flag->velocity);
            WorldHit hit=world_trace_for(flag->position,end,v3(2,8,2),
                (WorldQuery){WORLD_TRACE_ITEM,TEAM_NONE,WORLD_HAS_FLAG});
            flag->position=add(flag->position,scale(sub(end,flag->position),hit.fraction));
            if (hit.box>=0) {
                flag->position=add(flag->position,scale(hit.normal,.01f));
                flag->velocity=sub(flag->velocity,scale(hit.normal,dot(flag->velocity,hit.normal)));
                if (length(flag->velocity)<SRC_MINMOVEDELTA) flag->velocity=v3(0,0,0);
            }
            if (--flag->ticks==0 || flag->position.y<world_bounds.min.y-2*actor_height(STANDING)) {
                objectives_return(game,id,-1,EVENT_FLAG_RETURN);
                continue;
            }
            if (id!=FLAG_YELLOW && length(sub(flag->position,flag->base))<SRC_BASE_RADIUS) {
                flag->state=FLAG_BASE;
                flag->ticks=SRC_FLAG_TIMEOUT;
            } else flag->state=FLAG_DROPPED;
        }
        if (game->mode==MODE_INF && id==FLAG_ALPHA) continue;
        int nearest=-1;
        float distance=SRC_FLAG_RADIUS;
        for (int actor_index=0;actor_index<ACTOR_COUNT;++actor_index) {
            Actor *actor=&game->actors[actor_index];
            if (actor->life!=ALIVE || actor->flag_grab_cooldown>0 || actor->spawn_protection_ticks>0) continue;
            if (id!=FLAG_YELLOW && (unsigned)actor->team==(unsigned)id && flag->state==FLAG_BASE) continue;
            if (actor->carried_flag!=FLAG_NONE && (unsigned)actor->team!=(unsigned)id) continue;
            float candidate=length(sub(add(actor->position,v3(0,8,0)),flag->position));
            if (candidate<distance) {distance=candidate;nearest=actor_index;}
        }
        if (nearest<0) continue;
        Actor *actor=&game->actors[nearest];
        if (id!=FLAG_YELLOW && (unsigned)actor->team==(unsigned)id) {
            objectives_return(game,id,nearest,EVENT_FLAG_RETURN);
            continue;
        }
        flag->state=FLAG_CARRIED;
        flag->carrier=nearest;
        actor->carried_flag=id;
        combat_event(game,(GameEvent){EVENT_FLAG_GRAB,flag->position,nearest,id,NOWEAPON});
    }
    if (game->mode==MODE_CTF || game->mode==MODE_INF) {
        for (int i=0;i<2;++i) {
            Flag *flag=&game->flags[i];
            if (flag->state!=FLAG_CARRIED) continue;
            Actor *actor=&game->actors[flag->carrier];
            Flag *home=&game->flags[actor->team-1];
            if (home->state!=FLAG_BASE || length(sub(flag->position,home->position))>=SRC_TOUCHDOWN_RADIUS) continue;
            int score=game->mode==MODE_INF?SRC_INF_REDAWARD:1;
            if (game->mode==MODE_INF && teams[TEAM_ALPHA]>teams[TEAM_BRAVO])
                score-=5*(teams[TEAM_ALPHA]-teams[TEAM_BRAVO]);
            game->team_score[actor->team]+=score;
            if (game->team_score[actor->team]<0) game->team_score[actor->team]=0;
            ++actor->captures;
            objectives_return(game,(FlagId)(i+1),flag->carrier,EVENT_FLAG_CAPTURE);
        }
    }
    if (teams[TEAM_ALPHA]>0 && teams[TEAM_BRAVO]>0) {
        if (game->mode==MODE_INF) {
            int interval=SRC_INF_BLUELIMIT*TICK_RATE;
            if (teams[TEAM_BRAVO]>teams[TEAM_ALPHA]) interval+=2*TICK_RATE*(teams[TEAM_BRAVO]-teams[TEAM_ALPHA]);
            if (length(sub(game->flags[FLAG_BRAVO-1].position,game->flags[FLAG_BRAVO-1].base))<SRC_BASE_RADIUS &&
                game->match_ticks>0 && game->match_ticks%interval==0)
                ++game->team_score[TEAM_BRAVO];
        }
        if (game->mode==MODE_HTF) {
            if (teams[TEAM_ALPHA]==teams[TEAM_BRAVO]) game->htf_interval=SRC_HTF_POINTSTIME*TICK_RATE;
            if (game->flags[2].state==FLAG_CARRIED && game->match_ticks>0 && game->match_ticks%game->htf_interval==0) {
                Team team=game->actors[game->flags[2].carrier].team;
                Team other=team==TEAM_ALPHA?TEAM_BRAVO:TEAM_ALPHA;
                ++game->team_score[team];
                game->htf_interval=SRC_HTF_SEC_POINT+2*TICK_RATE*(teams[team]-teams[other]);
                if (game->htf_interval<SRC_HTF_SEC_POINT) game->htf_interval=SRC_HTF_SEC_POINT;
            }
        }
    }
    ++game->match_ticks;
    if (game->time_limit_ticks>0 && game->match_ticks>=game->time_limit_ticks) game->phase=MATCH_FINISHED;
    if (game->score_limit>0) {
        if (game_team_mode(game->mode)) {
            for (int i=1;i<=4;++i) if (game->team_score[i]>=game->score_limit) game->phase=MATCH_FINISHED;
        } else {
            for (int i=0;i<ACTOR_COUNT;++i)
                if (game->actors[i].life!=INACTIVE && game->actors[i].kills>=game->score_limit) game->phase=MATCH_FINISHED;
        }
    }
}

Vec3 objectives_target(const Game *game,int index,Vec3 combat_target) {
    const Actor *actor=&game->actors[index];
    if (game->mode==MODE_CTF || game->mode==MODE_INF) {
        const Flag *home=&game->flags[actor->team-1];
        const Flag *enemy=&game->flags[actor->team==TEAM_ALPHA?1:0];
        if (actor->carried_flag!=FLAG_NONE) return home->position;
        if (game->mode==MODE_INF && actor->team==TEAM_BRAVO) return home->position;
        if (home->state!=FLAG_BASE && index%3==0) return home->position;
        return enemy->position;
    }
    if (game->mode==MODE_POINTMATCH || game->mode==MODE_HTF) {
        const Flag *flag=&game->flags[2];
        if (actor->carried_flag==FLAG_YELLOW) {
            if (game->mode==MODE_HTF) return world_team_spawn(actor->team,0);
            return combat_target;
        }
        return flag->position;
    }
    return combat_target;
}
