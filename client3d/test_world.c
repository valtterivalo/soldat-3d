#include "world.h"
#include "pose.h"
#include "ragdoll.h"
#include "generated_rules.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    world_init();poses_init();ragdolls_init();
    assert(world_jet_fuel==152);
    assert(world_background[0][0]==36 && world_background[0][1]==112 && world_background[0][2]==187);
    Vec3 sample=v3(world_bounds.min.x+38,world_bounds.min.y,(world_bounds.min.z+world_bounds.max.z)*.5f+17);
    Vec3 from=add(sample,v3(0,8,0)),to=sub(sample,v3(0,8,0)),point=v3(0,0,0);
    WorldHit floor=world_trace(from,to,point);
    assert(floor.box>=0 && floor.normal.y>.99f);
    WorldQuery player={WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG};
    WorldQuery bullet={WORLD_TRACE_BULLET,0,WORLD_NO_FLAG};
    WorldQuery item={WORLD_TRACE_ITEM,0,WORLD_NO_FLAG};
    WorldQuery light={WORLD_TRACE_LIGHT,0,WORLD_NO_FLAG};
    assert(world_trace_for(from,v3(from.x,world_bounds.max.y+100,from.z),point,light).box<0);
    world_solids[floor.box].poly_type=3;
    assert(world_trace_for(from,to,point,light).box==floor.box);
    world_solids[floor.box].texture=WORLD_HIDDEN;
    assert(world_trace_for(from,to,point,light).box<0);
    world_solids[floor.box].texture=WORLD_TERRAIN;
    world_solids[floor.box].poly_type=1;
    assert(world_trace_for(from,to,point,player).box<0);
    assert(world_trace_for(from,to,point,bullet).box==floor.box);
    assert(world_trace_for(from,to,point,item).box<0);
    world_solids[floor.box].poly_type=2;
    assert(world_trace_for(from,to,point,player).box==floor.box);
    assert(world_trace_for(from,to,point,bullet).box<0);
    assert(world_trace_for(from,to,point,item).box<0);
    world_solids[floor.box].poly_type=21;
    assert(world_trace_for(from,to,point,player).box<0);
    player.flag=WORLD_HAS_FLAG;
    assert(world_trace_for(from,to,point,player).box==floor.box);
    assert(world_trace_for(from,to,point,bullet).box<0);
    world_solids[floor.box].poly_type=18;
    world_solids[floor.box].bounciness=2;
    Actor actor={.previous=from,.position=to,.velocity={0,-16,0},.pose=STANDING};
    assert(world_move(&actor)==AIRBORNE);
    assert(actor.velocity.y>15 && actor.position.y>sample.y+7);
    world_solids[floor.box].poly_type=0;
    int ramps=0;
    for(size_t i=0;i<world_nav_link_count && !ramps;++i) {
        const NavLink *edge=&world_nav_links[i];
        if(edge->mode!=NAV_WALK)continue;
        Vec3 a=world_nav_nodes[edge->from].position,b=world_nav_nodes[edge->to].position;
        if(fabsf(a.y-b.y)<16)continue;
        Vec3 middle=scale(add(a,b),.5f);
        WorldHit ramp=world_trace(add(middle,v3(0,20,0)),sub(middle,v3(0,20,0)),point);
        if(ramp.box<0 || ramp.normal.y>=.99f || ramp.normal.y<=.5f)continue;
        actor=(Actor){.previous=add(middle,v3(0,20,0)),.position=sub(middle,v3(0,20,0)),.velocity={0,-40,0},.pose=STANDING};
        assert(world_move(&actor)==GROUNDED);
        assert(fabsf(dot(actor.velocity,ramp.normal))<.001f);
        assert(world_pose_clear(actor.position,STANDING));
        ++ramps;
    }
    assert(ramps);
    assert(world_map_count==99);
    assert(world_map_index("Arena2")==0);
    assert(world_map_index("missing map")==SIZE_MAX);
    size_t mode_count=0,enclosure_rays=0;
    for(size_t map=0;map<world_map_count;++map) {
        world_load(map);
        assert(world_map_current==map && world_map_index(world_map_names[map])==map);
        assert(world_texture[0] && world_source_spawn_count && world_nav_node_count && world_nav_link_count);
        for(GameMode mode=MODE_DEATHMATCH;mode<=MODE_HTF;++mode) {
            if(!world_supports_mode(mode))continue;
            Game game;game_init(&game,1234,mode);
            assert(game.mode==mode && game.phase==MATCH_PLAYING);
            for(int i=0;i<ACTOR_COUNT;++i)if(game.actors[i].life==ALIVE) {
                const Actor *spawn=&game.actors[i];
                assert(world_pose_clear_for(spawn->position,STANDING,
                    (WorldQuery){WORLD_TRACE_ACTOR,spawn->team,WORLD_NO_FLAG}));
            }
            game_free(&game);++mode_count;
        }
        for(size_t other=map+1;other<world_map_count;++other)assert(strcmp(world_map_names[map],world_map_names[other]));
        for(int i=0;i<ACTOR_COUNT;++i)assert(world_pose_clear(world_spawns[i],STANDING));
        for(size_t i=0;i<world_nav_node_count;++i) {
            Vec3 node=world_nav_nodes[i].position;
            if(!world_pose_clear(node,STANDING))fprintf(stderr,"Blocked node %s %zu %.4f %.4f %.4f\n",world_map_names[map],i,node.x,node.y,node.z);
            assert(world_pose_clear(node,STANDING));
        }
        unsigned char reached[world_nav_node_count];memset(reached,0,sizeof(reached));reached[0]=1;
        size_t count=1;
        while(count<world_nav_node_count) {
            size_t before=count;
            for(size_t i=0;i<world_nav_link_count;++i) {
                const NavLink *edge=&world_nav_links[i];
                assert(edge->from>=0 && (size_t)edge->from<world_nav_node_count);
                assert(edge->to>=0 && (size_t)edge->to<world_nav_node_count);
                if(reached[edge->from] && !reached[edge->to] && edge->fuel<=world_jet_fuel) {reached[edge->to]=1;++count;}
            }
            assert(count>before);
        }
        Vec3 center=scale(add(world_bounds.min,world_bounds.max),.5f);
        center.y=world_bounds.max.y-140;
        float span=length(sub(world_bounds.max,world_bounds.min))*2;
        assert(world_pose_clear(center,STANDING));
        const Vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for(unsigned direction=0;direction<6;++direction) {
            WorldHit hit=world_trace(center,add(center,scale(directions[direction],span)),v3(3,7,3));
            if(hit.box<0)fprintf(stderr,"Open enclosure %s direction%u from %.2f,%.2f,%.2f\n",world_map_names[map],direction,center.x,center.y,center.z);
            assert(hit.box>=0);++enclosure_rays;
        }
        for(size_t i=0;i<world_solid_count;++i) {
            const WorldSolid *solid=&world_solids[i];
            Vec3 centroid=v3(0,0,0);float highest=-INFINITY;
            for(unsigned k=0;k<solid->vertex_count;++k) {
                centroid=add(centroid,scale(solid->vertices[k],1/(float)solid->vertex_count));
                highest=fmaxf(highest,solid->vertices[k].y);
            }
            if(solid->texture==WORLD_TERRAIN && highest>center.y)for(unsigned k=0;k<solid->vertex_count;++k) {
                Vec3 direction=sub(solid->vertices[k],center);direction.y=0;
                Vec3 outside=add(center,scale(direction,span/length(direction)));
                WorldHit wall=world_trace(center,outside,v3(3,7,3));
                assert(wall.box>=0 && world_solids[wall.box].texture==WORLD_TERRAIN);++enclosure_rays;
            }
            for(unsigned f=0;f<solid->face_count;++f) {
                Vec3 a=solid->vertices[solid->faces[f][0]],n=v3(0,0,0);
                for(unsigned k=1;k+1<solid->face_size[f];++k) {
                    Vec3 b=sub(solid->vertices[solid->faces[f][k]],a),c=sub(solid->vertices[solid->faces[f][k+1]],a);
                    n=add(n,v3(b.y*c.z-b.z*c.y,b.z*c.x-b.x*c.z,b.x*c.y-b.y*c.x));
                }
                assert(length(n)>0);n=scale(n,1/length(n));
                if(dot(n,sub(centroid,a))>0)n=scale(n,-1);
                for(unsigned k=3;k<solid->face_size[f];++k)assert(fabsf(dot(n,sub(solid->vertices[solid->faces[f][k]],a)))<.01f);
                for(unsigned k=0;k<solid->vertex_count;++k)assert(dot(n,sub(solid->vertices[k],a))<.01f);
            }
        }
        actor=(Actor){.previous=center,.position=add(center,v3(span,0,span)),.velocity={12,0,12},.pose=STANDING};
        world_move(&actor);
        assert(world_pose_clear(actor.position,STANDING));
        assert(actor.position.x<world_bounds.max.x && actor.position.z<world_bounds.max.z);
        actor.velocity=actor.force=v3(0,0,0);
        float fall_speed=SRC_GRAV*SRC_EDAMPING/(1-SRC_EDAMPING);
        unsigned fall_ticks=(unsigned)ceilf((actor.position.y-world_bounds.min.y)/fall_speed+1/(1-SRC_EDAMPING))+2;
        for(unsigned tick=0;tick<fall_ticks && actor.contact!=GROUNDED;++tick)movement_step(&actor,(Input){0},tick);
        assert(actor.contact==GROUNDED && actor.position.y>=world_bounds.min.y);
    }
    world_free();ragdolls_free();poses_free();
    printf("World: 99 enclosed maps, %zu mode constructors, %zu boundary rays, safe falls, connected navigation and convex collision passed\n",mode_count,enclosure_rays);
    return 0;
}
