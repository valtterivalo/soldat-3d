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
    poses_init();ragdolls_init();
    const struct { const char *map;Vec3 from,to; } hazards[]={
        {"Aero",{-278,140.05f,234},{-250.667f,140.05f,247.333f}},
        {"Jungle",{9,.05f,386},{-23.3333f,.05f,382.667f}}
    };
    for(unsigned map=0;map<sizeof(hazards)/sizeof(*hazards);++map) {
        world_load(world_map_index(hazards[map].map));
        assert(!layout_ground_route(hazards[map].from,hazards[map].to,(WorldQuery){WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG}));
        for(unsigned route=0;route<2;++route) {
            Game game;game_init(&game,1234,MODE_DEATHMATCH);
            for(int i=1;i<ACTOR_COUNT;++i){game.actors[i].life=INACTIVE;game.actors[i].respawn_ticks=0;}
            Actor *walker=&game.actors[0];
            walker->position=walker->previous=hazards[map].from;
            walker->velocity=walker->force=v3(0,0,0);walker->spawn_protection_ticks=-1;
            uint32_t previous=0;int arrived=0;
            for(unsigned tick=0;tick<4*TICK_RATE && walker->life==ALIVE;++tick) {
                Vec3 delta=sub(hazards[map].to,walker->position);
                Input inputs[ACTOR_COUNT]={0};inputs[0].yaw=atan2f(delta.x,delta.z);inputs[0].forward=1;
                if(route)bot_navigation(&game,0,hazards[map].to,BOT_TRAVERSE,&inputs[0]);
                inputs[0].pressed=inputs[0].held&~previous;previous=inputs[0].held;
                game_step(&game,inputs);
                if(route)assert(walker->life==ALIVE);
                if(length(sub(walker->position,hazards[map].to))<14 && fabsf(walker->position.y-hazards[map].to.y)<8 && walker->contact==GROUNDED) {
                    arrived=1;break;
                }
            }
            assert(route ? arrived : walker->life==DEAD);
            game_free(&game);
        }
    }
    world_free();ragdolls_free();poses_free();
    printf("Collision: slope, three-plane contact and lethal-route regressions, %zu sweeps match independent double SAT\n",compared);return 0;
}
