#undef NDEBUG
#include "world.c"
#include "bot_navigation.h"
#include "pose.h"
#include "ragdoll.h"

typedef struct { double x,y,z; } DVec3;

static DVec3 difference(Vec3 a,Vec3 b)
{
    return(DVec3){(double)a.x-b.x,(double)a.y-b.y,(double)a.z-b.z};
}

static DVec3 cross(DVec3 a,DVec3 b)
{
    return(DVec3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}

static double projection(DVec3 axis,Vec3 point)
{
    return axis.x*point.x+axis.y*point.y+axis.z*point.z;
}

static double swept_sat(const WorldSolid *solid,Vec3 from,Vec3 to,Vec3 extent)
{
    DVec3 axes[3+sizeof(solid->face_size)/sizeof(solid->face_size[0])+3*sizeof(solid->vertices)/sizeof(solid->vertices[0])*sizeof(solid->vertices)/sizeof(solid->vertices[0])];
    axes[0]=(DVec3){1,0,0};axes[1]=(DVec3){0,1,0};axes[2]=(DVec3){0,0,1};
    unsigned count=3;
    for(unsigned face=0;face<solid->face_count;++face) {
        Vec3 a=solid->vertices[solid->faces[face][0]],b=solid->vertices[solid->faces[face][1]],c=solid->vertices[solid->faces[face][2]];
        axes[count++]=cross(difference(b,a),difference(c,a));
    }
    for(unsigned a=0;a<solid->vertex_count;++a)for(unsigned b=a+1;b<solid->vertex_count;++b)
        for(unsigned dimension=0;dimension<3;++dimension)axes[count++]=cross(difference(solid->vertices[b],solid->vertices[a]),axes[dimension]);
    double enter=0,leave=1;
    for(unsigned i=0;i<count;++i) {
        DVec3 axis=axes[i];if(axis.x==0 && axis.y==0 && axis.z==0)continue;
        double low=DBL_MAX,high=-DBL_MAX;
        for(unsigned v=0;v<solid->vertex_count;++v) {
            double projected=projection(axis,solid->vertices[v]);low=fmin(low,projected);high=fmax(high,projected);
        }
        double radius=fabs(axis.x)*extent.x+fabs(axis.y)*extent.y+fabs(axis.z)*extent.z;
        low-=radius;high+=radius;
        double origin=projection(axis,from),velocity=projection(axis,to)-origin;
        if(velocity==0){if(origin<low || origin>high)return -1;continue;}
        double a=(low-origin)/velocity,b=(high-origin)/velocity;
        enter=fmax(enter,fmin(a,b));leave=fmin(leave,fmax(a,b));
        if(enter>leave)return -1;
    }
    return enter;
}

static float random_coordinate(uint32_t *state,float low,float high)
{
    *state^=*state<<13;*state^=*state>>17;*state^=*state<<5;
    return low+(high-low)*(float)(*state>>8)/16777216.f;
}

int main(void)
{
    const struct { Vec3 top[3],from,to; } fixtures[]={
        {{{-0x1.a8p+7f,0x1.5cf5c2p+4f,-0x1.04p+7f},{-0x1.1ep+8f,0x1.8p+4f,-0x1.44p+7f},{-0x1.2cp+8f,0x1.2fae14p+4f,-0x1.d8p+6f}},
         {-0x1.ef2928p+7f,0x1.ea3456p+4f,-0x1.2e6e58p+7f},{-0x1.f27392p+7f,0x1.e47cap+4f,-0x1.231ba2p+7f}},
        {{{0x1p+8f,0x1.8p+4f,-0x1.8p+7f},{0x1.7p+7f,0,-0x1.18p+8f},{0x1.44p+7f,0,-0x1.dcp+7f}},
         {0x1.787998p+7f,0x1.e0e904p+3f,-0x1.b8853cp+7f},{0x1.6f782cp+7f,0x1.bc8e04p+3f,-0x1.bfe37cp+7f}}
    };
    uint32_t seed=0x52a795b1;size_t compared=0;
    for(unsigned fixture=0;fixture<sizeof(fixtures)/sizeof(fixtures[0]);++fixture) {
        world_solids=calloc(1,sizeof(*world_solids));assert(world_solids);world_solid_count=1;
        solid_polygon(world_solids,fixtures[fixture].top,3,-0x1.54b852p+5f,(const unsigned char[]){255,255,255,255});
        world_build_hulls();
        Vec3 extent=v3(3,6.99f,3);
        assert(swept_sat(world_solids,fixtures[fixture].from,fixtures[fixture].to,extent)<0);
        assert(world_trace(fixtures[fixture].from,fixtures[fixture].to,extent).box<0);
        for(unsigned sample=0;sample<20000;++sample) {
            Vec3 a=v3(random_coordinate(&seed,hulls[0].min.x-20,hulls[0].max.x+20),random_coordinate(&seed,hulls[0].min.y-20,hulls[0].max.y+20),random_coordinate(&seed,hulls[0].min.z-20,hulls[0].max.z+20));
            Vec3 b=v3(random_coordinate(&seed,hulls[0].min.x-20,hulls[0].max.x+20),random_coordinate(&seed,hulls[0].min.y-20,hulls[0].max.y+20),random_coordinate(&seed,hulls[0].min.z-20,hulls[0].max.z+20));
            if(sample%7==0)b=a;
            extent=sample%3==0 ? v3(0,0,0) : sample%3==1 ? v3(3,7,3) : v3(5.375f,4.3f,2.75f);
            double expected=swept_sat(world_solids,a,b,extent);WorldHit actual=world_trace(a,b,extent);
            if((expected<0)!=(actual.box<0) || (expected>=0 && fabs(expected-actual.fraction)>0.0001)) {
                fprintf(stderr,"SAT mismatch fixture%u sample%u expected%.12g actual hull%d fraction%.12g\n",fixture,sample,expected,actual.box,actual.fraction);abort();
            }
            ++compared;
        }
        world_free();
    }
    world_solids=calloc(1,sizeof(*world_solids));assert(world_solids);world_solid_count=1;
    const Vec3 launchpad[]={{-32,0,-32},{32,0,-32},{32,0,32},{-32,0,32}};
    solid_polygon(world_solids,launchpad,4,-8,(const unsigned char[]){255,255,255,255});
    world_solids[0].poly_type=18;world_solids[0].bounciness=2;
    world_build_hulls();
    Actor falling={.previous={0,8,0},.position={0,-8,0},.velocity={0,-16,0},.pose=STANDING,.carried_flag=FLAG_NONE};
    assert(world_move(&falling)==AIRBORNE);
    assert(falling.velocity.y>15 && falling.position.y>7);
    assert(world_pose_clear(falling.position,falling.pose));
    world_free();
    world_solids=calloc(3,sizeof(*world_solids));assert(world_solids);world_solid_count=1;
    const Vec3 corner[]={v3(412,71.0624466f,352),v3(384,28,324),v3(380,28,328)};
    const unsigned char color[]={255,255,255,255};
    solid_polygon(world_solids,corner,3,-125.695343f,color);
    size_t capacity=3;
    layout_block(&capacity,v3(350,-130,300),v3(450,200,323),color);
    layout_block(&capacity,v3(350,-130,300),v3(379,200,400),color);
    world_build_hulls();
    Actor actor={.previous={382,32.6638222f,326},.position={382,32.6038208f,326},.velocity={0,-.06f,0},.pose=STANDING};
    assert(world_pose_clear(actor.previous,actor.pose));
    assert(world_move(&actor)==GROUNDED);
    assert(world_pose_clear(actor.position,actor.pose));
    assert(length(actor.velocity)<.00001f);
    for(unsigned tick=0;tick<120;++tick) {
        actor.previous=actor.position;actor.position.y-=.06f;actor.velocity=v3(0,-.06f,0);
        assert(world_move(&actor)==GROUNDED);
        assert(world_pose_clear(actor.position,actor.pose));
    }
    world_free();
    capacity=0;
    layout_block(&capacity,v3(0,0,-12),v3(6,40,12),color);
    world_gates=malloc(sizeof(*world_gates));assert(world_gates);world_gates[0]=0;world_gate_count=1;
    world_nav_nodes=calloc(4,sizeof(*world_nav_nodes));assert(world_nav_nodes);world_nav_node_count=4;
    const Vec3 gate_positions[]={{-3,.05f,0},{-2.9f,.05f,0},{-6,.05f,0},{-3,.05f,6}};
    for(unsigned node=0;node<4;++node)world_nav_nodes[node].position=gate_positions[node];
    world_build_hulls();
    const NavLink gate_edges[]={
        {.from=0,.to=0,.mode=NAV_WALK},{.from=1,.to=1,.mode=NAV_WALK},
        {.from=0,.to=1,.mode=NAV_WALK},{.from=0,.to=2,.mode=NAV_WALK},{.from=0,.to=3,.mode=NAV_WALK}
    };
    const unsigned gate_types[]={11,21};
    for(unsigned type=0;type<2;++type)for(Team team=TEAM_NONE;team<=TEAM_DELTA;++team)for(WorldFlagState flag=WORLD_NO_FLAG;flag<=WORLD_HAS_FLAG;++flag) {
        world_solids[0].poly_type=gate_types[type];WorldQuery query={WORLD_TRACE_ACTOR,team,flag};
        for(unsigned edge=0;edge<sizeof(gate_edges)/sizeof(*gate_edges);++edge) {
            NavLink link=gate_edges[edge];Vec3 from=add(gate_positions[link.from],v3(0,7,0)),to=add(gate_positions[link.to],v3(0,7,0));
            int collision=world_trace_for(from,to,v3(3,7,3),query).box>=0;
            assert(world_nav_link_allows(&link,query)==!collision);
            int blocked=(type==0 ? team==TEAM_ALPHA : flag==WORLD_HAS_FLAG) && (edge==1 || edge==2);
            assert(collision==blocked);
        }
    }
    world_free();
    for(unsigned raised=0;raised<2;++raised) {
        capacity=0;world_bounds=(Box){v3(-64,-30,-20),v3(64,100,100),SURFACE_STONE};
        float gap=raised*.5f;
        const Vec3 floor[]={{-64,4.6f-gap,-20},{64,-21-gap,-20},{64,9-gap,100},{-64,34.6f-gap,100}};
        WorldSolid *ground=solid_append(&capacity);solid_polygon(ground,floor,4,-30,color);ground->visible_faces=2;
        layout_slab(&capacity,v3(0,0,0),v3(0,20,80),32,8,color);
        world_build_hulls();
        Vec3 from=v3(-30,14.2f-gap,40),to=v3(0,10.8f,40);
        assert(world_pose_clear(from,STANDING) && world_pose_clear(to,STANDING));
        assert(layout_ground_route(from,to,(WorldQuery){WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG})==!raised);
        world_free();
    }
    capacity=0;world_bounds=(Box){v3(-160,-30,-120),v3(160,100,120),SURFACE_STONE};
    const Vec3 approach_floor[]={{-160,0,-120},{160,0,-120},{160,0,120},{-160,0,120}};
    WorldSolid *ground=solid_append(&capacity);solid_polygon(ground,approach_floor,4,-30,color);ground->visible_faces=2;
    layout_slab(&capacity,v3(0,0,0),v3(0,20,80),32,8,color);
    layout_slab(&capacity,v3(64,32,-20),v3(128,48,-20),40,8,color);
    world_build_hulls();
    assert(layout_ground_route(v3(0,.05f,-20),v3(0,10.8f,40),(WorldQuery){WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG}));
    assert(layout_ground_route(v3(96,.05f,-50),v3(96,.05f,10),(WorldQuery){WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG}));
    world_free();
    poses_init();ragdolls_init();
    const LayoutTerrainVertex enclosure[]={{0,0,0,96},{-80,-80,32,32},{80,-80,32,32},{80,80,32,32},{-80,80,32,32}};
    for(unsigned closed=0;closed<2;++closed) {
        capacity=0;world_bounds=(Box){v3(-80,-30,-80),v3(80,116,80),SURFACE_STONE};
        for(unsigned side=0;side<4;++side) {
            unsigned indices[]={0,side+1,(side+1)%4+1};Vec3 floor[3],roof[3];
            for(unsigned v=0;v<3;++v) {
                LayoutTerrainVertex point=enclosure[indices[v]];
                floor[v]=v3(point.x,point.y,point.z);roof[v]=v3(point.x,point.crest+20,point.z);
            }
            solid_polygon(solid_append(&capacity),floor,3,-30,color);
            if(closed) {
                WorldSolid *sky=solid_append(&capacity);solid_polygon(sky,roof,3,0,color);
                for(unsigned v=0;v<3;++v)sky->vertices[v].y=roof[v].y-20;
            }
        }
        world_build_hulls();
        Actor escape={.position={0,1.25f,0},.previous={0,1.25f,0},.pose=STANDING,.life=ALIVE,
            .fuel=152,.fuel_capacity=152};
        assert(world_pose_clear(escape.position,STANDING));
        Input flight={.forward=1,.yaw=1.57079632679f,.held=INPUT_JETS};
        for(unsigned tick=0;tick<2*TICK_RATE;++tick) {
            movement_step(&escape,flight,tick);
            assert(world_pose_clear(escape.position,STANDING));
            if(closed)assert(escape.position.x<80);
        }
        assert(closed ? escape.position.x<80 : escape.position.x>80);
        world_free();
    }
    for(unsigned lethal=5;lethal<=6;++lethal) {
        capacity=0;world_bounds=(Box){v3(-100,-8,-80),v3(100,100,80),SURFACE_STONE};
        layout_block(&capacity,world_bounds.min,v3(-12,0,80),color);
        layout_block(&capacity,v3(12,-8,-80),v3(100,0,80),color);
        layout_block(&capacity,v3(-12,-8,-80),v3(12,0,-14),color);
        layout_block(&capacity,v3(-12,-8,14),v3(12,0,80),color);
        layout_block(&capacity,v3(-12,-8,-14),v3(12,0,14),color);
        world_solids[4].poly_type=lethal;world_build_hulls();
        const Vec3 path[]={{-50,.05f,0},{-50,.05f,40},{50,.05f,40},{50,.05f,0}};
        WorldQuery query={WORLD_TRACE_ACTOR,TEAM_NONE,WORLD_NO_FLAG};
        assert(world_pose_clear(path[0],STANDING) && world_pose_clear(path[3],STANDING));
        assert(!layout_ground_route(path[0],path[3],query));
        world_nav_nodes=calloc(4,sizeof(*world_nav_nodes));assert(world_nav_nodes);world_nav_node_count=4;
        for(unsigned node=0;node<4;++node)world_nav_nodes[node].position=path[node];
        for(unsigned node=0;node<3;++node) {
            assert(layout_ground_route(path[node],path[node+1],query));
            layout_walk_edge(node,node+1,query);
        }
        for(unsigned route=0;route<2;++route) {
            Game game={.random=1234,.mode=MODE_DEATHMATCH,.phase=MATCH_PLAYING};
            for(int i=0;i<ACTOR_COUNT;++i)game.actors[i].life=INACTIVE;
            Actor *walker=&game.actors[0];
            *walker=(Actor){.position=path[0],.previous=path[0],.life=ALIVE,.health=150,
                .pose=STANDING,.contact=GROUNDED,.spawn_protection_ticks=-1,.nav_edge=-1,.nav_goal=-1};
            game_equip(walker,AK74,COLT);
            uint32_t previous=0;int arrived=0;
            for(unsigned tick=0;tick<4*TICK_RATE && walker->life==ALIVE;++tick) {
                Vec3 delta=sub(path[3],walker->position);
                Input inputs[ACTOR_COUNT]={0};inputs[0].yaw=atan2f(delta.x,delta.z);inputs[0].forward=1;
                if(route)bot_navigation(&game,0,path[3],BOT_TRAVERSE,&inputs[0]);
                inputs[0].pressed=inputs[0].held&~previous;previous=inputs[0].held;
                game_step(&game,inputs);
                if(route)assert(walker->life==ALIVE);
                if(length(sub(walker->position,path[3]))<14 && walker->contact==GROUNDED) {arrived=1;break;}
            }
            if(!(route ? arrived : walker->life==DEAD))fprintf(stderr,"Hazard %u route%u life%d arrived%d at%g,%g,%g tick%llu\n",lethal,route,walker->life,arrived,walker->position.x,walker->position.y,walker->position.z,(unsigned long long)game.tick);
            assert(route ? arrived : walker->life==DEAD);
            game_free(&game);
        }
        world_free();
    }
    world_free();ragdolls_free();poses_free();
    printf("Collision: slope, three-plane contact, gate tangency, conforming-edge enclosure and lethal-route regressions, %zu sweeps match independent double SAT\n",compared);return 0;
}
