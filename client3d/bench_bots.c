#include "game.h"
#include "world.h"
#include "pose.h"
#include "ragdoll.h"
#include "snapshot.h"
#include "showcase.h"

#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum { LOG_EVENTS, LOG_SUMMARY } Output;
typedef struct {
    uint64_t shots[WEAPON_COUNT], hits[WEAPON_COUNT], kills[WEAPON_COUNT];
    uint64_t alive, airborne, jets, jumps, rolls, switches, respawns, moving_hits, airborne_hits, self_hits, self_kills;
    uint64_t flight_start, flight_ticks;
    Vec3 launch;
    float flight_peak, travel, max_speed, min_y, max_y, longest_hit, longest_kill, max_ascent;
    unsigned fuel_spent, flight_fuel, same_tick_multikills;
} Metrics;

static const char *const weapon_ids[WEAPON_COUNT]={
    "EAGLE","MP5","AK74","STEYRAUG","SPAS12","RUGER77","M79","BARRETT",
    "M249","MINIGUN","COLT","KNIFE","CHAINSAW","LAW","BOW2","BOW",
    "FLAMER","M2","NOWEAPON","FRAGGRENADE","CLUSTERGRENADE","CLUSTER","THROWNKNIFE"
};
static const char *const event_ids[]={"shot","impact","explosion","hit","kill","pickup","drop",
    "flag_grab","flag_return","flag_drop","flag_capture"};

static uint64_t number(const char *text,uint64_t limit)
{
    char *end;
    errno=0;
    unsigned long long value=strtoull(text,&end,0);
    assert(*text && *text!='-' && !*end && !errno && value<=limit);
    return (uint64_t)value;
}

static void vector(Vec3 value)
{
    printf("[%.6f,%.6f,%.6f]",value.x,value.y,value.z);
}

static void state(const Game *game,int actor,const char *kind)
{
    const Actor *a=&game->actors[actor];
    printf("{\"kind\":\"%s\",\"tick\":%" PRIu64 ",\"actor\":%d,\"spawn\":%u,\"team\":%d,\"life\":%d,\"position\":",
        kind,game->tick,actor,a->spawn_id,a->team,a->life);
    vector(a->position);
    printf(",\"velocity\":");vector(a->velocity);
    printf(",\"yaw\":%.6f,\"pitch\":%.6f,\"health\":%.6f,\"fuel\":%d,\"grenades\":%d,\"pose\":%d,\"animation\":%d,\"grounded\":%s,\"weapon\":\"%s\",\"primary\":\"%s\",\"secondary\":\"%s\",\"controls\":%u}\n",
        a->yaw,a->pitch,a->health,a->fuel,a->grenades,a->pose,a->animation,a->contact==GROUNDED?"true":"false",
        weapon_ids[a->slots[a->active_slot].id],weapon_ids[a->loadout[0]],weapon_ids[a->loadout[1]],a->controls);
}

int main(int argc,char **argv)
{
    const char *map="Arena2",*preset="match";
    uint32_t seed=0x501da7;
    uint64_t ticks=7200,sample=30;
    int population=8;
    Output output=LOG_EVENTS;
    for(int arg=1;arg<argc;++arg) {
        if(!strcmp(argv[arg],"--summary")){output=LOG_SUMMARY;continue;}
        if(!strcmp(argv[arg],"--help")) {
            puts("bench_bots --map NAME --preset match|crossfire|rifles|marksmen|ascent --seed N --ticks N --population 2..32 --sample N --summary");
            return 0;
        }
        assert(arg+1<argc);
        const char *option=argv[arg],*value=argv[++arg];
        if(!strcmp(option,"--map"))map=value;
        else if(!strcmp(option,"--preset"))preset=value;
        else if(!strcmp(option,"--seed")){seed=(uint32_t)number(value,UINT32_MAX);assert(seed);}
        else if(!strcmp(option,"--ticks")){ticks=number(value,UINT64_MAX);assert(ticks);}
        else if(!strcmp(option,"--population")){population=(int)number(value,ACTOR_COUNT);assert(population>=2);}
        else if(!strcmp(option,"--sample")){sample=number(value,UINT64_MAX);assert(sample);}
        else {fprintf(stderr,"Unknown option %s\n",option);return EXIT_FAILURE;}
    }
    poses_init();ragdolls_init();world_load(world_map_index(map));
    Game game;showcase_init(&game,preset,seed,population);
    printf("{\"kind\":\"match\",\"map\":\"%s\",\"preset\":\"%s\",\"seed\":%u,\"ticks\":%" PRIu64 ",\"population\":%d,\"mode\":\"%s\",\"sample\":%" PRIu64 ",\"fuel_capacity\":%d}\n",
        world_map_names[world_map_current],preset,seed,ticks,population,game_mode_ids[game.mode],sample,world_jet_fuel);
    Metrics metrics[ACTOR_COUNT]={0};
    uint32_t held[ACTOR_COUNT]={0};
    uint64_t hash=14695981039346656037ull;
    for(int i=0;i<population;++i) {
        metrics[i].min_y=metrics[i].max_y=game.actors[i].position.y;
        state(&game,i,"initial");
    }
    clock_t begin=clock();
    for(uint64_t tick=0;tick<ticks;++tick) {
        Actor before[ACTOR_COUNT];memcpy(before,game.actors,sizeof(before));
        Input inputs[ACTOR_COUNT]={0};
        for(int actor=0;actor<population;++actor) {
            inputs[actor]=bot_input(&game,actor);
            inputs[actor].pressed=inputs[actor].held&~held[actor];held[actor]=inputs[actor].held;
        }
        game_step(&game,inputs);
        unsigned kills[ACTOR_COUNT][WEAPON_COUNT]={0};
        for(size_t index=0;index<game.event_count;++index) {
            const GameEvent *event=&game.events[index];
            hash=(hash^(uint64_t)event->kind)*1099511628211ull;
            hash=(hash^(uint64_t)(event->actor+1))*1099511628211ull;
            hash=(hash^(uint64_t)(event->target+1))*1099511628211ull;
            hash=(hash^(uint64_t)event->weapon)*1099511628211ull;
            float distance=0;
            if((event->kind==EVENT_HIT || event->kind==EVENT_KILL) && event->actor>=0) {
                distance=length(sub(before[event->target].position,before[event->actor].position));
                Metrics *m=&metrics[event->actor];
                if(event->kind==EVENT_HIT) {
                    if(event->actor==event->target)++m->self_hits;
                    else {
                        ++m->hits[event->weapon];m->longest_hit=fmaxf(m->longest_hit,distance);
                        m->moving_hits+=length(before[event->target].velocity)>.25f;
                        m->airborne_hits+=before[event->target].contact==AIRBORNE;
                    }
                } else if(event->actor==event->target)++m->self_kills;
                else {
                    ++m->kills[event->weapon];++kills[event->actor][event->weapon];
                    m->longest_kill=fmaxf(m->longest_kill,distance);
                }
            }
            if(event->kind==EVENT_SHOT)++metrics[event->actor].shots[event->weapon];
            if(output==LOG_SUMMARY)continue;
            printf("{\"kind\":\"%s\",\"tick\":%" PRIu64 ",\"seconds\":%.6f,\"actor\":%d,\"target\":%d,\"weapon\":\"%s\",\"position\":",
                event_ids[event->kind],game.tick,(double)game.tick/TICK_RATE,event->actor,event->target,weapon_ids[event->weapon]);
            vector(event->position);
            if(event->actor>=0) {
                printf(",\"actor_position\":");vector(before[event->actor].position);
                printf(",\"actor_velocity\":");vector(before[event->actor].velocity);
            }
            if(event->kind==EVENT_SHOT && game.bots[event->actor].target>=0) {
                int target=game.bots[event->actor].target;
                printf(",\"aim_target\":%d,\"aim_distance\":%.6f,\"aim_target_position\":",target,
                    length(sub(before[target].position,before[event->actor].position)));
                vector(before[target].position);
                printf(",\"aim_target_velocity\":");vector(before[target].velocity);
            }
            if(event->kind==EVENT_HIT || event->kind==EVENT_KILL) {
                printf(",\"distance\":%.6f,\"target_position\":",distance);vector(before[event->target].position);
                printf(",\"target_velocity\":");vector(before[event->target].velocity);
                printf(",\"target_airborne\":%s",before[event->target].contact==AIRBORNE?"true":"false");
            }
            puts("}");
        }
        for(int actor=0;actor<population;++actor) {
            const Actor *a=&game.actors[actor],*b=&before[actor];Metrics *m=&metrics[actor];
            assert(a->fuel>=0 && a->fuel<=a->fuel_capacity);
            if(a->life==ALIVE)assert(world_pose_clear_for(a->position,a->pose,
                (WorldQuery){WORLD_TRACE_ACTOR,a->team,a->carried_flag==FLAG_NONE?WORLD_NO_FLAG:WORLD_HAS_FLAG}));
            for(int weapon=0;weapon<WEAPON_COUNT;++weapon)if(kills[actor][weapon]>=2) {
                ++m->same_tick_multikills;
                if(output==LOG_EVENTS)printf("{\"kind\":\"simultaneous_kills\",\"tick\":%" PRIu64 ",\"actor\":%d,\"weapon\":\"%s\",\"count\":%u}\n",
                    game.tick,actor,weapon_ids[weapon],kills[actor][weapon]);
            }
            if(a->spawn_id!=b->spawn_id) {++m->respawns;m->flight_ticks=0;if(output==LOG_EVENTS)state(&game,actor,"spawn");}
            if(b->life==ALIVE) {
                ++m->alive;m->airborne+=b->contact==AIRBORNE;m->jets+=(inputs[actor].held&INPUT_JETS)!=0;
                m->jumps+=(inputs[actor].pressed&INPUT_JUMP)!=0;m->rolls+=(inputs[actor].pressed&INPUT_ROLL)!=0;
                if(a->spawn_id==b->spawn_id) {
                    m->travel+=length(sub(a->position,b->position));m->max_speed=fmaxf(m->max_speed,length(a->velocity));
                    if(b->fuel>a->fuel){m->fuel_spent+=(unsigned)(b->fuel-a->fuel);m->flight_fuel+=(unsigned)(b->fuel-a->fuel);}
                }
                m->min_y=fminf(m->min_y,a->position.y);m->max_y=fmaxf(m->max_y,a->position.y);
                if(b->active_slot!=a->active_slot) {++m->switches;if(output==LOG_EVENTS)state(&game,actor,"switch");}
                if(output==LOG_EVENTS && a->animation!=b->animation &&
                    (a->animation==MOVE_ROLL || a->animation==MOVE_ROLLBACK))state(&game,actor,"roll");
                if(b->contact==GROUNDED && a->contact==AIRBORNE) {
                    m->flight_start=game.tick;m->launch=b->position;m->flight_peak=a->position.y;m->flight_ticks=1;
                    m->flight_fuel=b->fuel>a->fuel?(unsigned)(b->fuel-a->fuel):0;
                    if(output==LOG_EVENTS)state(&game,actor,"takeoff");
                } else if(m->flight_ticks && a->contact==AIRBORNE) {++m->flight_ticks;m->flight_peak=fmaxf(m->flight_peak,a->position.y);}
                else if(m->flight_ticks && a->contact==GROUNDED) {
                    m->max_ascent=fmaxf(m->max_ascent,m->flight_peak-m->launch.y);
                    if(output==LOG_EVENTS) {
                        printf("{\"kind\":\"landing\",\"tick\":%" PRIu64 ",\"actor\":%d,\"launch_tick\":%" PRIu64 ",\"flight_ticks\":%" PRIu64 ",\"ascent\":%.6f,\"fuel\":%u,\"from\":",
                            game.tick,actor,m->flight_start,m->flight_ticks,m->flight_peak-m->launch.y,m->flight_fuel);
                        vector(m->launch);printf(",\"position\":");vector(a->position);puts("}");
                    }
                    m->flight_ticks=0;
                }
            }
            if(output==LOG_EVENTS && game.tick%sample==0 && a->life!=INACTIVE)state(&game,actor,"sample");
        }
    }
    Snapshot snapshot=snapshot_encode(&game);uint64_t state_hash=14695981039346656037ull;
    for(size_t i=0;i<snapshot.size;++i)state_hash=(state_hash^snapshot.data[i])*1099511628211ull;
    free(snapshot.data);
    for(int actor=0;actor<population;++actor) {
        const Actor *a=&game.actors[actor];const Metrics *m=&metrics[actor];
        printf("{\"kind\":\"actor_summary\",\"actor\":%d,\"kills\":%d,\"deaths\":%d,\"captures\":%d,\"respawns\":%" PRIu64 ",\"alive_ticks\":%" PRIu64 ",\"airborne_ticks\":%" PRIu64 ",\"jet_ticks\":%" PRIu64 ",\"jump_presses\":%" PRIu64 ",\"roll_presses\":%" PRIu64 ",\"switches\":%" PRIu64 ",\"moving_hits\":%" PRIu64 ",\"airborne_hits\":%" PRIu64 ",\"self_hits\":%" PRIu64 ",\"fuel_spent\":%u,\"travel\":%.6f,\"max_speed\":%.6f,\"min_y\":%.6f,\"max_y\":%.6f,\"max_ascent\":%.6f,\"longest_hit\":%.6f,\"longest_kill\":%.6f,\"simultaneous_multikills\":%u,\"weapons\":[",
            actor,a->kills,a->deaths,a->captures,m->respawns,m->alive,m->airborne,m->jets,m->jumps,m->rolls,m->switches,
            m->moving_hits,m->airborne_hits,m->self_hits,m->fuel_spent,m->travel,m->max_speed,m->min_y,m->max_y,
            m->max_ascent,m->longest_hit,m->longest_kill,m->same_tick_multikills);
        int separator=0;
        for(int weapon=0;weapon<WEAPON_COUNT;++weapon)if(m->shots[weapon] || m->hits[weapon] || m->kills[weapon]) {
            printf("%s{\"weapon\":\"%s\",\"shots\":%" PRIu64 ",\"hits\":%" PRIu64 ",\"kills\":%" PRIu64 "}",separator?",":"",
                weapon_ids[weapon],m->shots[weapon],m->hits[weapon],m->kills[weapon]);separator=1;
        }
        printf("],\"self_kills\":%" PRIu64 "}\n",m->self_kills);
    }
    printf("{\"kind\":\"summary\",\"tick\":%" PRIu64 ",\"event_hash\":\"%016" PRIx64 "\",\"state_hash\":\"%016" PRIx64 "\"}\n",game.tick,hash,state_hash);
    assert(!ferror(stdout));
    fprintf(stderr,"%s seed%u actors%d ticks%" PRIu64 " CPU %.3fs\n",map,seed,population,ticks,(double)(clock()-begin)/CLOCKS_PER_SEC);
    game_free(&game);world_free();ragdolls_free();poses_free();
    return 0;
}
