#include "world.h"

#undef NDEBUG
#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Vec3 normal;float distance; } TestPlane;
typedef struct { TestPlane planes[sizeof(((WorldSolid *)0)->face_size)/sizeof(unsigned)+6+6*sizeof(((WorldSolid *)0)->faces)/sizeof(unsigned)];Vec3 low,high;unsigned count; } TestHull;

static WorldHit exhaustive(const TestHull *hulls,Vec3 start,Vec3 end,Vec3 extents,WorldQuery query)
{
    WorldHit hit={1,{0,0,0},-1};
    for(size_t i=0;i<world_solid_count;++i) {
        const WorldSolid *solid=&world_solids[i];const TestHull *hull=&hulls[i];unsigned type=solid->poly_type;
        if(query.kind==WORLD_TRACE_LIGHT) {if(solid->texture!=WORLD_TERRAIN)continue;}
        else {
            if(type==3 || type==23 || type==24 || type==25)continue;
            if((type==1 && query.kind!=WORLD_TRACE_BULLET && query.kind!=WORLD_TRACE_ENVIRONMENT) ||
                (type==2 && query.kind!=WORLD_TRACE_ACTOR && query.kind!=WORLD_TRACE_ENVIRONMENT))continue;
            if(type>=10 && type<=17 && (query.team!=1+(type-10)/2 ||
                (type%2==0 && query.kind!=WORLD_TRACE_BULLET) || (type%2==1 && query.kind==WORLD_TRACE_BULLET)))continue;
            if((type==21 || type==22) && query.kind!=WORLD_TRACE_ACTOR && query.kind!=WORLD_TRACE_ENVIRONMENT)continue;
            if((type==21 && query.flag==WORLD_NO_FLAG) || (type==22 && query.flag==WORLD_HAS_FLAG))continue;
        }
        if(fminf(start.x,end.x)>hull->high.x+extents.x || fmaxf(start.x,end.x)<hull->low.x-extents.x ||
            fminf(start.y,end.y)>hull->high.y+extents.y || fmaxf(start.y,end.y)<hull->low.y-extents.y ||
            fminf(start.z,end.z)>hull->high.z+extents.z || fmaxf(start.z,end.z)<hull->low.z-extents.z)continue;
        float enter=-FLT_MAX,leave=1,nearest=-FLT_MAX;int inside=1;Vec3 normal={0},nearest_normal={0};
        unsigned plane_count=extents.x==0 && extents.y==0 && extents.z==0 ? solid->face_count+6 : hull->count;
        for(unsigned p=0;p<plane_count;++p) {
            TestPlane plane=hull->planes[p];
            float distance=plane.distance+fabsf(plane.normal.x)*extents.x+fabsf(plane.normal.y)*extents.y+fabsf(plane.normal.z)*extents.z;
            float from=dot(plane.normal,start)-distance,to=dot(plane.normal,end)-distance;
            if(from>=0)inside=0;
            if(from>nearest){nearest=from;nearest_normal=plane.normal;}
            if(from>0 && to>0){leave=-1;break;}
            if((from<0 && to<=0) || from==to)continue;
            float fraction=from/(from-to);
            if(from>to && fraction>enter){enter=fraction;normal=plane.normal;}
            else if(to>from && fraction<leave)leave=fraction;
        }
        if(inside)return(WorldHit){0,nearest_normal,(int)i};
        if(enter>=0 && enter<leave && enter<hit.fraction)hit=(WorldHit){enter,normal,(int)i};
    }
    return hit;
}

static float random01(uint32_t *state)
{
    *state^=*state<<13;*state^=*state>>17;*state^=*state<<5;
    return(float)(*state>>8)/16777216.f;
}

int main(void)
{
    uint32_t seed=0x129bad9u;size_t compared=0,occlusion_queries=0;
    for(size_t map=0;map<world_map_count;++map) {
        world_load(map);TestHull *hulls=calloc(world_solid_count,sizeof(*hulls));assert(hulls);
        for(size_t i=0;i<world_solid_count;++i) {
            const WorldSolid *solid=&world_solids[i];TestHull *hull=&hulls[i];Vec3 center={0};
            hull->low=v3(FLT_MAX,FLT_MAX,FLT_MAX);hull->high=scale(hull->low,-1);
            for(unsigned v=0;v<solid->vertex_count;++v) {
                Vec3 point=solid->vertices[v];center=add(center,scale(point,1/(float)solid->vertex_count));
                hull->low.x=fminf(hull->low.x,point.x);hull->low.y=fminf(hull->low.y,point.y);hull->low.z=fminf(hull->low.z,point.z);
                hull->high.x=fmaxf(hull->high.x,point.x);hull->high.y=fmaxf(hull->high.y,point.y);hull->high.z=fmaxf(hull->high.z,point.z);
            }
            for(unsigned face=0;face<solid->face_count;++face) {
                Vec3 a=solid->vertices[solid->faces[face][0]],normal={0};
                for(unsigned v=1;v+1<solid->face_size[face];++v) {
                    Vec3 b=sub(solid->vertices[solid->faces[face][v]],a),c=sub(solid->vertices[solid->faces[face][v+1]],a);
                    Vec3 n=v3(b.y*c.z-b.z*c.y,b.z*c.x-b.x*c.z,b.x*c.y-b.y*c.x);
                    if(dot(n,n)>dot(normal,normal))normal=n;
                }
                normal=scale(normal,1/length(normal));if(dot(normal,sub(center,a))>0)normal=scale(normal,-1);
                hull->planes[hull->count++]=(TestPlane){normal,dot(normal,a)};
            }
            const TestPlane bounds[]={{{-1,0,0},-hull->low.x},{{1,0,0},hull->high.x},{{0,-1,0},-hull->low.y},{{0,1,0},hull->high.y},{{0,0,-1},-hull->low.z},{{0,0,1},hull->high.z}};
            memcpy(hull->planes+hull->count,bounds,sizeof(bounds));hull->count+=6;
            unsigned char edges[16][16]={0};
            for(unsigned face=0;face<solid->face_count;++face)for(unsigned edge=0;edge<solid->face_size[face];++edge) {
                unsigned a=solid->faces[face][edge],b=solid->faces[face][(edge+1)%solid->face_size[face]];
                if(edges[a][b])continue;
                edges[a][b]=edges[b][a]=1;
                Vec3 delta=sub(solid->vertices[b],solid->vertices[a]);
                Vec3 axes[]={v3(0,delta.z,-delta.y),v3(-delta.z,0,delta.x),v3(delta.y,-delta.x,0)};
                for(unsigned axis=0;axis<3;++axis) {
                    float magnitude=length(axes[axis]);if(magnitude==0)continue;
                    Vec3 normal=scale(axes[axis],1/magnitude);
                    for(unsigned sign=0;sign<2;++sign,normal=scale(normal,-1)) {
                        unsigned existing=0;
                        for(;existing<hull->count;++existing)if(normal.x==hull->planes[existing].normal.x && normal.y==hull->planes[existing].normal.y && normal.z==hull->planes[existing].normal.z)break;
                        if(existing<hull->count)continue;
                        float distance=-FLT_MAX;
                        for(unsigned v=0;v<solid->vertex_count;++v)distance=fmaxf(distance,dot(normal,solid->vertices[v]));
                        hull->planes[hull->count++]=(TestPlane){normal,distance};
                    }
                }
            }
        }
        size_t cases=world_solid_count*2+128;
        for(size_t n=0;n<cases;++n) {
            Vec3 span=sub(world_bounds.max,world_bounds.min);
            Vec3 a=add(world_bounds.min,v3(random01(&seed)*span.x,random01(&seed)*span.y,random01(&seed)*span.z));
            if(n<world_solid_count*2){const WorldSolid *solid=&world_solids[n/2];a=solid->vertices[n%solid->vertex_count];}
            Vec3 b=n%2 ? add(a,v3(random01(&seed)*14-7,random01(&seed)*14-7,random01(&seed)*14-7)) :
                add(world_bounds.min,v3(random01(&seed)*span.x,random01(&seed)*span.y,random01(&seed)*span.z));
            if(n%7==0)b=a;
            Vec3 extents=n%3 ? v3(3,7,3) : v3(0,0,0);
            WorldQuery query={(WorldTraceKind)(n%5),(unsigned)(n/5%6),(WorldFlagState)(n/30%2)};
            WorldHit expected=exhaustive(hulls,a,b,extents,query),actual=world_trace_for(a,b,extents,query);
            if(expected.box!=actual.box || memcmp(&expected.fraction,&actual.fraction,sizeof(float)) || memcmp(&expected.normal,&actual.normal,sizeof(Vec3))) {
                fprintf(stderr,"Broadphase mismatch %s case%zu expected hull%d fraction%a actual hull%d fraction%a\n",world_map_names[map],n,expected.box,expected.fraction,actual.box,actual.fraction);
                abort();
            }
            if(extents.x==0) {
                ++occlusion_queries;
                if(world_occluded_for(a,b,query)!=(expected.box>=0)) {
                    fprintf(stderr,"Occlusion mismatch %s case%zu expected hull%d\n",world_map_names[map],n,expected.box);
                    abort();
                }
            }
            ++compared;
        }
        free(hulls);
    }
    world_free();printf("Broadphase: %zu traces and %zu occlusion queries on99 maps match exhaustive hull traces\n",compared,occlusion_queries);return 0;
}
