#include "game.h"
#include "world.h"
#include "world_layouts.h"
#include "pose.h"
#include "ragdoll.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int condition,const char *message) {
    if(condition)return;
    fprintf(stderr,"spawns %s: %s\n",world_map_names[world_map_current],message);
    exit(EXIT_FAILURE);
}

static void separated(const Game *game) {
    for(int i=0;i<ACTOR_COUNT;++i) {
        const Actor *actor=&game->actors[i];
        check(actor->life==ALIVE,"all32 requested players spawn");
        check(world_pose_clear_for(actor->position,STANDING,
            (WorldQuery){WORLD_TRACE_ACTOR,actor->team,WORLD_NO_FLAG}),"spawn body clears actual terrain and team gates");
        for(int j=0;j<i;++j) {
            Vec3 delta=sub(actor->position,game->actors[j].position);
            if(fabsf(delta.y)>=actor_height(STANDING))continue;
            check(delta.x*delta.x+delta.z*delta.z>=4*SRC_PART_RADIUS*SRC_PART_RADIUS-.001f,
                "living players never share a spawn or intersect another player's source hit radius");
        }
    }
}

int main(void) {
    world_init();poses_init();ragdolls_init();
    world_load(world_map_index("Bunker"));
    Game safety;game_init(&safety,1234,MODE_DEATHMATCH);
    for(int i=0;i<ACTOR_COUNT;++i)safety.actors[i].life=INACTIVE;
    Actor *enemy=&safety.actors[1];
    *enemy=(Actor){.position=world_nav_nodes[0].position,.pose=STANDING,.contact=GROUNDED,.life=ALIVE};
    float clearance=SRC_FRAGGRENADE_EXPLOSION_RADIUS+SRC_PART_RADIUS;
    Vec3 eye=add(enemy->position,v3(0,10,0));int sheltered=0;
    for(size_t i=0;i<world_team_spawn_count(TEAM_NONE);++i) {
        Vec3 position=world_team_spawn(TEAM_NONE,i);
        sheltered|=length(sub(position,enemy->position))>=clearance &&
            world_trace_for(eye,add(position,v3(0,10,0)),v3(0,0,0),(WorldQuery){WORLD_TRACE_BULLET,TEAM_NONE,WORLD_NO_FLAG}).box>=0;
    }
    check(sheltered,"exposure fixture offers a distant spawn behind real terrain");
    for(unsigned seed=1;seed<=32;++seed) {
        safety.random=seed*0x9e3779b9u;enemy->yaw=(float)(seed%4)*1.5707963f;
        check(game_respawn(&safety,0)==SPAWN_READY,"a safe candidate spawns immediately");
        const Actor *spawned=&safety.actors[0];
        check(length(sub(spawned->position,enemy->position))>=clearance,
            "respawns preserve grenade-scale enemy clearance regardless of enemy facing");
        check(world_trace_for(eye,add(spawned->position,v3(0,10,0)),v3(0,0,0),
            (WorldQuery){WORLD_TRACE_BULLET,TEAM_NONE,WORLD_NO_FLAG}).box>=0,
            "available distant terrain cover takes priority over exposed spawn sites");
        check(spawned->spawn_protection_ticks==SRC_DEFAULT_CEASEFIRE_TIME,"source spawn protection duration stays unchanged");
    }
    game_free(&safety);
    world_load(world_map_index("RatCave"));
    const LayoutRoom *room=&world_layout("RatCave")->rooms[0];int region=0;
    for(size_t i=0;i<world_team_spawn_count(TEAM_NONE);++i) {
        Vec3 point=world_team_spawn(TEAM_NONE,i);
        region|=fabsf(point.x-room->x)<room->width*.5f && fabsf(point.z-room->z)<room->depth*.5f;
    }
    check(region,"neutral spawns include connected authored rooms beyond the original marker clusters");
    size_t modes=0,spawns=0,min_pool=(size_t)-1;
    for(size_t map=0;map<world_map_count;++map) {
        world_load(map);
        for(unsigned team=TEAM_NONE;team<=TEAM_DELTA;++team) {
            size_t count=world_team_spawn_count(team);
            if(count<min_pool)min_pool=count;
            check(count>=ACTOR_COUNT,"each original team spawn region supports a full32-player cohort");
            for(size_t i=0;i<count;++i) {
                Actor actor={.position=world_team_spawn(team,i),.pose=STANDING,.team=(Team)team};
                unsigned type=world_contact_type(&actor);
                check(type!=5 && type!=6 && type!=7 && type!=9 && type!=18 && type!=19 && type!=20,
                    "players do not spawn on damage or launch surfaces");
            }
        }
        for(GameMode mode=MODE_DEATHMATCH;mode<=MODE_HTF;mode=(GameMode)(mode+1)) {
            if(!world_supports_mode(mode))continue;
            Game game;game_init(&game,1234,mode);
            for(int i=0;i<ACTOR_COUNT;++i)game.actors[i].life=ALIVE;
            game_set_mode(&game,mode);
            separated(&game);
            if(mode==MODE_POINTMATCH || mode==MODE_HTF) {
                int source_marker=0;
                for(size_t i=0;i<world_marker_spawn_count(14);++i)
                    source_marker|=length(sub(game.flags[2].base,add(world_marker_spawn(14,i),v3(0,8,0))))<.001f;
                check(source_marker,"yellow objectives retain original marker placement rather than player-grid sites");
            }
            for(int wave=0;wave<3;++wave) {
                for(int i=0;i<ACTOR_COUNT;++i)check(game_respawn(&game,i)==SPAWN_READY,
                    "every player in a respawn wave receives an unoccupied location");
                separated(&game);
                spawns+=ACTOR_COUNT;
            }
            ++modes;spawns+=ACTOR_COUNT;
            game_free(&game);
        }
        for(Team team=TEAM_ALPHA;team<=TEAM_DELTA;team=(Team)(team+1)) {
            Game game;game_init(&game,9876,MODE_TEAMMATCH);
            for(int i=0;i<ACTOR_COUNT;++i) {
                game.actors[i].life=ALIVE;
                game.actors[i].team=team;
            }
            game_restart(&game);separated(&game);spawns+=ACTOR_COUNT;
            game_free(&game);
        }
    }
    world_free();poses_free();ragdolls_free();
    printf("Spawns: %zu maps, %zu supported modes, %zu separated spawns, smallest team pool%zu passed\n",
        world_map_count,modes,spawns,min_pool);
}
