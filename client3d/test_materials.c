#include "game.h"
#include "world.h"
#include "world_layouts.h"
#include "pose.h"
#include "ragdoll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world_materials.inc"

static void check(int condition,const char *message) {
    if (condition) return;
    fprintf(stderr,"material: %s\n",message);
    exit(EXIT_FAILURE);
}

int main(void) {
    poses_init();ragdolls_init();
    world_load(world_map_index("htf_Nuclear"));
    Game game;game_init(&game,1234,MODE_HTF);
    Actor *actor=&game.actors[0];
    actor->position=actor->previous=sub(game.flags[2].base,v3(0,8,0));
    actor->spawn_protection_ticks=-1;
    check(world_contact_type(actor)==7,"reactor damages the actual yellow-flag floor");
    for (int tick=0;tick<60;++tick) combat_environment(&game,0,world_contact_type(actor));
    check(actor->health<150 && actor->life==ALIVE,"reactor contact uses gradual source damage");
    game_free(&game);

    world_load(world_map_index("ctf_Voland"));game_init(&game,1234,MODE_CTF);
    for (int i=0;i<2;++i) {
        actor=&game.actors[i];actor->health=100;
        actor->position=actor->previous=sub(game.flags[i].base,v3(0,8,0));
        check(world_contact_type(actor)==8,"both Voland bases retain their healing platform");
        game.tick=12;combat_environment(&game,i,world_contact_type(actor));
        check(actor->health==102,"base healing retains two health per twelve ticks");
    }
    game_free(&game);

    world_load(world_map_index("ctf_Crucifix"));game_init(&game,1234,MODE_CTF);
    actor=&game.actors[0];
    actor->position=actor->previous=add(sub(game.flags[0].base,v3(0,8,0)),v3(32,0,0));
    check(world_contact_type(actor)==20,"carrier hazard stays on base egress");
    for (int tick=0;tick<30;++tick) combat_environment(&game,0,20);
    check(actor->health==150,"carrier floor leaves unladen players unharmed");
    actor->carried_flag=FLAG_BRAVO;
    for (int tick=0;tick<30;++tick) combat_environment(&game,0,20);
    check(actor->health<150 && actor->life==ALIVE,"carrier floor damages only a flag carrier");
    game_free(&game);

    world_load(world_map_index("htf_Star"));
    unsigned coefficients=0;int launched=0;
    for (size_t i=0;i<world_solid_count;++i) {
        const WorldSolid *solid=&world_solids[i];
        if (solid->poly_type!=18) continue;
        if (fabsf(solid->bounciness-5.8f)<.00001f) coefficients|=1;
        if (fabsf(solid->bounciness-6.2f)<.00001f) coefficients|=2;
        if (fabsf(solid->bounciness-5.791f)<.00001f) coefficients|=4;
        if (launched) continue;
        Vec3 center=v3(0,0,0);
        for (unsigned v=0;v<solid->face_size[1];++v)
            center=add(center,scale(solid->vertices[solid->faces[1][v]],1.0f/(float)solid->face_size[1]));
        Actor falling={.pose=STANDING,.previous=add(center,v3(0,8,0)),
            .position=sub(center,v3(0,8,0)),.velocity={0,-16,0}};
        launched=world_move(&falling)==AIRBORNE && falling.velocity.y>60;
    }
    check(coefficients==7 && launched,"Star retains distinct source bounce strengths and physical launch");

    const char *maps[]={"ctf_Crucifix","ctf_Snakebite","inf_Warehouse","inf_Outpost","ctf_IceBeam"};
    unsigned conditional_edges=0,states=0;
    for (size_t map=0;map<sizeof(maps)/sizeof(*maps);++map) {
        world_load(world_map_index(maps[map]));
        for (Team team=TEAM_ALPHA;team<=TEAM_BRAVO;++team) {
            for (WorldFlagState flag=WORLD_NO_FLAG;flag<=WORLD_HAS_FLAG;++flag) {
                WorldQuery query={WORLD_TRACE_ACTOR,team,flag};
                unsigned char allowed[world_nav_node_count],reached[world_nav_node_count];
                memset(reached,0,sizeof(reached));size_t available=0,count=0;
                for (size_t node=0;node<world_nav_node_count;++node) {
                    allowed[node]=(unsigned char)world_nav_link_allows(
                        &(NavLink){.from=(int)node,.to=(int)node,.mode=NAV_WALK},query);
                    available+=allowed[node];
                    if (allowed[node] && !count) { reached[node]=1;count=1; }
                }
                check(available>0,"conditional gates retain accessible navigation nodes");
                for (;;) {
                    size_t before=count;
                    for (size_t i=0;i<world_nav_link_count;++i) {
                        const NavLink *edge=&world_nav_links[i];
                        if (!allowed[edge->from] || !allowed[edge->to]) continue;
                        int pass=world_nav_link_allows(edge,query);
                        conditional_edges+=!pass;
                        if (pass && reached[edge->from] && !reached[edge->to]) {
                            reached[edge->to]=1;++count;
                        }
                    }
                    if (count==before) break;
                }
                if (count!=available) fprintf(stderr,"%s team%d flag%d reaches%zu/%zu\n",maps[map],team,flag,count,available);
                check(count==available,"both teams and carry states retain connected physical routes");
                ++states;
            }
        }
    }
    check(conditional_edges>0,"authored gates affect real navigation edges");
    size_t pads=0;
    for (size_t map=0;map<world_map_count;++map) {
        world_load(map);
        size_t count=0;
        const LayoutMaterial *materials=layout_material_plan(world_map_names[map],&count);
        const Layout *layout=world_layout(world_map_names[map]);
        for (size_t i=0;i<count;++i) {
            const LayoutMaterial *material=&materials[i];
            if (material->height) continue;
            const LayoutRoom *room=&layout->rooms[material->room];
            Vec3 feet=v3(room->x+material->x,room->y+.05f,room->z+material->z);
            Vec3 from=add(feet,v3(0,16,0)),to=v3(feet.x,world_bounds.min.y-8,feet.z);
            WorldHit support=world_trace(from,to,v3(3,0,3));
            check(support.box>=0,"source pad has physical terrain support");
            feet=add(add(from,scale(sub(to,from),support.fraction)),v3(0,.05f,0));
            Actor standing={.position=feet,.pose=STANDING};
            unsigned type=world_contact_type(&standing);
            int clear=world_pose_clear(feet,STANDING);
            if (!clear || type!=material->type)
                fprintf(stderr,"%s material%zu type%u contact%u clear%d\n",world_map_names[map],i,material->type,type,clear);
            check(clear && type==material->type,"authored contact pad stays exposed above terrain and cover");
            ++pads;
        }
    }
    world_free();ragdolls_free();poses_free();
    printf("Material roles: reactor damage, paired healing, carrier damage, launch coefficients, %u gate states and %zu exposed pads passed\n",states,pads);
}
