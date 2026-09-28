#include "game.h"
#include "world.h"
#include "pose.h"
#include "ragdoll.h"
#include "snapshot.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(int argc,char **argv)
{
    unsigned ticks=7200;
    if(argc>1){char *end;unsigned long requested=strtoul(argv[1],&end,10);assert(*argv[1] && !*end && requested>0 && requested<=UINT32_MAX);ticks=(unsigned)requested;}
    const char *maps[]={"Arena2","Airpirates","ctf_Crucifix","inf_Fortress"};
    const GameMode modes[]={MODE_DEATHMATCH,MODE_DEATHMATCH,MODE_CTF,MODE_INF};
    poses_init();ragdolls_init();
    for(size_t m=0;m<sizeof(maps)/sizeof(maps[0]);++m) {
        clock_t load=clock();world_load(world_map_index(maps[m]));load=clock()-load;
        Game game;game_init(&game,0x61756469u,modes[m]);
        for(int actor=0;actor<ACTOR_COUNT;++actor)game.actors[actor].life=ALIVE;
        game_set_mode(&game,modes[m]);game.score_limit=0;game.time_limit_ticks=0;
        uint32_t previous[ACTOR_COUNT]={0};uint64_t events=14695981039346656037ull;
        clock_t bots=0,simulation=0,slowest=0;
        for(unsigned tick=0;tick<ticks;++tick) {
            clock_t begin=clock();Input inputs[ACTOR_COUNT];
            for(int actor=0;actor<ACTOR_COUNT;++actor) {
                inputs[actor]=bot_input(&game,actor);
                inputs[actor].pressed=inputs[actor].held&~previous[actor];previous[actor]=inputs[actor].held;
            }
            clock_t stepped=clock();bots+=stepped-begin;game_step(&game,inputs);
            clock_t done=clock();simulation+=done-stepped;
            if(done-begin>slowest)slowest=done-begin;
            for(size_t i=0;i<game.event_count;++i) {
                const GameEvent *event=&game.events[i];
                events=(events^(uint64_t)event->kind)*1099511628211ull;
                events=(events^(uint64_t)event->actor)*1099511628211ull;
                events=(events^(uint64_t)event->target)*1099511628211ull;
            }
        }
        Snapshot snapshot=snapshot_encode(&game);uint64_t state=14695981039346656037ull;
        for(size_t i=0;i<snapshot.size;++i)state=(state^snapshot.data[i])*1099511628211ull;
        free(snapshot.data);
        double scale=1000.0/CLOCKS_PER_SEC;
        printf("%s actors%d ticks%u solids%zu nodes%zu links%zu load_ms%.3f bot_ms%.6f step_ms%.6f max_ms%.3f state%016llx events%016llx\n",
            maps[m],ACTOR_COUNT,ticks,world_solid_count,world_nav_node_count,world_nav_link_count,(double)load*scale,
            (double)bots*scale/ticks,(double)simulation*scale/ticks,(double)slowest*scale,(unsigned long long)state,(unsigned long long)events);
        fflush(stdout);game_free(&game);
    }
    world_free();ragdolls_free();poses_free();return 0;
}
