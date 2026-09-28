#include "world.h"
#include "world_layouts.h"
#include "generated_rules.h"

#include <assert.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Vec3 normal; float distance; } Plane;
typedef struct { Plane planes[10]; Vec3 min,max; } Hull;
typedef struct { Vec3 min,max; size_t solid,end; } WorldBranch;

WorldSolid *world_solids;
size_t world_solid_count;
int world_jet_fuel,world_medikits,world_grenades;
WorldSpawn *world_source_spawns;
size_t world_source_spawn_count,world_map_current;
char world_texture[64];
#define WORLD_MAP(name,texture) name,
const char *const world_map_names[]={
#include "world_maps.inc"
};
#undef WORLD_MAP
#define WORLD_MAP(name,texture) texture,
static const char *const map_textures[]={
#include "world_maps.inc"
};
#undef WORLD_MAP
const size_t world_map_count=sizeof(world_map_names)/sizeof(world_map_names[0]);
WorldProp *world_props;
size_t world_prop_count, world_scenery_count;
char (*world_scenery)[51];
unsigned char world_background[2][4];
Vec3 world_spawns[ACTOR_COUNT];
static Hull *hulls;
static WorldBranch *world_tree;
static size_t world_tree_count;
static size_t *world_gates;
static unsigned *spawn_rooms;
static struct { Vec3 *positions;size_t count; } player_spawns[TEAM_DELTA+1];
size_t world_gate_count;

Box world_bounds;
NavNode *world_nav_nodes;
NavLink *world_nav_links;
size_t world_nav_node_count,world_nav_link_count;

float actor_height(Pose pose)
{
    const float heights[] = {14, 10, 6};
    return heights[pose];
}

static void read_bytes(FILE *file, void *destination, size_t count)
{
    if (fread(destination, 1, count, file) != count) {
        fprintf(stderr,"Failed to read %s.pms\n",world_map_names[world_map_current]);
        exit(EXIT_FAILURE);
    }
}

static uint32_t read_u32(FILE *file)
{
    unsigned char bytes[4];
    read_bytes(file, bytes, sizeof(bytes));
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static float read_float(FILE *file)
{
    uint32_t bits = read_u32(file);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void skip_bytes(FILE *file, size_t count)
{
    if (fseek(file, (long)count, SEEK_CUR) != 0) {
        perror(world_map_names[world_map_current]);
        exit(EXIT_FAILURE);
    }
}

typedef struct {
    unsigned char color[3][4];
    unsigned type;
    float bounciness;
} SourcePolygon;
typedef struct {
    SourcePolygon *polygons;
    WorldProp *props;
    size_t polygon_count, prop_count;
} SourceMap;

#include "world_materials.inc"

static size_t nav_node(Vec3 position, float separation)
{
    for (size_t i=0;i<world_nav_node_count;++i)
        if (length(sub(position,world_nav_nodes[i].position))<separation) return i;
    NavNode *nodes=realloc(world_nav_nodes,(world_nav_node_count+1)*sizeof(*nodes));
    if (!nodes) abort();
    world_nav_nodes=nodes;
    world_nav_nodes[world_nav_node_count]=(NavNode){position};
    return world_nav_node_count++;
}


static void solid_polygon(WorldSolid *solid, const Vec3 *top, unsigned count, float bottom,
    const unsigned char color[4])
{
    solid->vertex_count=2*count;solid->face_count=count+2;
    solid->face_size[0]=solid->face_size[1]=count;
    for (unsigned i=0;i<count;++i) {
        solid->vertices[i]=top[i];solid->vertices[i].y=bottom;
        solid->vertices[i+count]=top[i];
        for (unsigned v=i;v<=i+count;v+=count) {
            memcpy(solid->color[v],color,4);
            solid->uv[v][0]=top[i].x/80;solid->uv[v][1]=top[i].z/80;
        }
        solid->faces[0][i]=count-1-i;solid->faces[1][i]=count+i;
        unsigned next=(i+1)%count;
        solid->face_size[i+2]=4;
        solid->faces[i+2][0]=i;solid->faces[i+2][1]=next;
        solid->faces[i+2][2]=next+count;solid->faces[i+2][3]=i+count;
    }
}


static WorldSolid *solid_append(size_t *capacity)
{
    if (world_solid_count==*capacity) {
        *capacity=*capacity ? *capacity*2 : 256;
        WorldSolid *solids=realloc(world_solids,*capacity*sizeof(*solids));
        if (!solids) abort();world_solids=solids;
    }
    WorldSolid *solid=&world_solids[world_solid_count++];*solid=(WorldSolid){0};return solid;
}

static int hull_x(const void *left,const void *right)
{
    size_t a=*(const size_t *)left,b=*(const size_t *)right;
    float x=hulls[a].min.x+hulls[a].max.x,y=hulls[b].min.x+hulls[b].max.x;
    return x<y ? -1 : x>y ? 1 : a<b ? -1 : a>b;
}

static int hull_y(const void *left,const void *right)
{
    size_t a=*(const size_t *)left,b=*(const size_t *)right;
    float x=hulls[a].min.y+hulls[a].max.y,y=hulls[b].min.y+hulls[b].max.y;
    return x<y ? -1 : x>y ? 1 : a<b ? -1 : a>b;
}

static int hull_z(const void *left,const void *right)
{
    size_t a=*(const size_t *)left,b=*(const size_t *)right;
    float x=hulls[a].min.z+hulls[a].max.z,y=hulls[b].min.z+hulls[b].max.z;
    return x<y ? -1 : x>y ? 1 : a<b ? -1 : a>b;
}

static void world_branch(size_t *indices,size_t count)
{
    WorldBranch *branch=&world_tree[world_tree_count++];
    branch->min=hulls[indices[0]].min;branch->max=hulls[indices[0]].max;
    for(size_t i=1;i<count;++i) {
        const Hull *hull=&hulls[indices[i]];
        branch->min.x=fminf(branch->min.x,hull->min.x);branch->min.y=fminf(branch->min.y,hull->min.y);branch->min.z=fminf(branch->min.z,hull->min.z);
        branch->max.x=fmaxf(branch->max.x,hull->max.x);branch->max.y=fmaxf(branch->max.y,hull->max.y);branch->max.z=fmaxf(branch->max.z,hull->max.z);
    }
    branch->solid=SIZE_MAX;
    if(count==1)branch->solid=indices[0];
    else {
        Vec3 span=sub(branch->max,branch->min);
        int (*compare)(const void *,const void *)=span.x>span.y && span.x>span.z ? hull_x : span.y>span.z ? hull_y : hull_z;
        qsort(indices,count,sizeof(*indices),compare);
        world_branch(indices,count/2);world_branch(indices+count/2,count-count/2);
    }
    branch->end=world_tree_count;
}

static void world_build_hulls(void)
{
    free(hulls);
    hulls=calloc(world_solid_count,sizeof(*hulls));if (!hulls) abort();
    for (size_t i=0;i<world_solid_count;++i) {
        const WorldSolid *solid=&world_solids[i];
        Vec3 center=v3(0,0,0);
        hulls[i].min=v3(FLT_MAX,FLT_MAX,FLT_MAX);hulls[i].max=scale(hulls[i].min,-1);
        for (unsigned v=0;v<solid->vertex_count;++v) {
            Vec3 p=solid->vertices[v];center=add(center,scale(p,1/(float)solid->vertex_count));
            hulls[i].min.x=fminf(hulls[i].min.x,p.x);hulls[i].min.y=fminf(hulls[i].min.y,p.y);hulls[i].min.z=fminf(hulls[i].min.z,p.z);
            hulls[i].max.x=fmaxf(hulls[i].max.x,p.x);hulls[i].max.y=fmaxf(hulls[i].max.y,p.y);hulls[i].max.z=fmaxf(hulls[i].max.z,p.z);
        }
        for (unsigned face=0;face<solid->face_count;++face) {
            Vec3 a=solid->vertices[solid->faces[face][0]],normal=v3(0,0,0);
            for (unsigned j=1;j+1<solid->face_size[face];++j) {
                Vec3 b=sub(solid->vertices[solid->faces[face][j]],a),c=sub(solid->vertices[solid->faces[face][j+1]],a);
                Vec3 cross=v3(b.y*c.z-b.z*c.y,b.z*c.x-b.x*c.z,b.x*c.y-b.y*c.x);
                if (dot(cross,cross)>dot(normal,normal)) normal=cross;
            }
            normal=scale(normal,1/length(normal));if (dot(normal,sub(center,a))>0) normal=scale(normal,-1);
            hulls[i].planes[face]=(Plane){normal,dot(normal,a)};
        }
    }
    free(world_tree);world_tree_count=0;
    world_tree=malloc((2*world_solid_count-1)*sizeof(*world_tree));
    size_t *indices=malloc(world_solid_count*sizeof(*indices));if(!world_tree || !indices)abort();
    for(size_t i=0;i<world_solid_count;++i)indices[i]=i;
    world_branch(indices,world_solid_count);free(indices);
}

static size_t layout_block(size_t *capacity,Vec3 low,Vec3 high,const unsigned char color[4])
{
    Vec3 top[]={v3(low.x,high.y,low.z),v3(high.x,high.y,low.z),v3(high.x,high.y,high.z),v3(low.x,high.y,high.z)};
    size_t index=world_solid_count;
    solid_polygon(solid_append(capacity),top,4,low.y,color);
    return index;
}

static size_t layout_slab(size_t *capacity,Vec3 start,Vec3 end,float width,float thickness,const unsigned char color[4])
{
    Vec3 delta=sub(end,start);delta.y=0;
    Vec3 across=scale(v3(-delta.z,0,delta.x),width*.5f/length(delta));
    Vec3 top[]={sub(start,across),add(start,across),add(end,across),sub(end,across)};
    size_t index=world_solid_count;
    WorldSolid *solid=solid_append(capacity);solid_polygon(solid,top,4,0,color);
    for(unsigned i=0;i<4;++i)solid->vertices[i].y=top[i].y-thickness;
    return index;
}

static Vec3 layout_portal(const LayoutRoom *room,const LayoutRoom *target)
{
    float x=target->x-room->x,z=target->z-room->z;
    float along_x=x==0 ? FLT_MAX : room->width*.5f/fabsf(x);
    float along_z=z==0 ? FLT_MAX : room->depth*.5f/fabsf(z);
    float fraction=fminf(along_x,along_z);
    if(room->shape==ROCK)fraction=fminf(fraction,(room->width*.5f+room->depth*.5f-fminf(room->width,room->depth)*.16f)/(fabsf(x)+fabsf(z)));
    return v3(room->x+x*fraction,room->y,room->z+z*fraction);
}

static void layout_edge(size_t from,size_t to,NavTravel travel,int fuel,float apex)
{
    if(from==to)return;
    for(size_t i=0;i<world_nav_link_count;++i)
        if(world_nav_links[i].from==(int)from && world_nav_links[i].to==(int)to)return;
    NavLink *links=realloc(world_nav_links,(world_nav_link_count+1)*sizeof(*links));if(!links)abort();world_nav_links=links;
    float distance=length(sub(world_nav_nodes[to].position,world_nav_nodes[from].position));
    world_nav_links[world_nav_link_count++]=(NavLink){(int)from,(int)to,travel,distance/2.6f+(float)fuel*.4f,fuel,apex};
}

typedef struct { Vec3 *vertices; size_t count; } LayoutPolygon;
static LayoutPolygon layout_clip(const LayoutPolygon *polygon,Vec3 normal,float distance)
{
    LayoutPolygon result={malloc((polygon->count+1)*sizeof(Vec3)),0};if(!result.vertices)abort();
    for(size_t i=0;i<polygon->count;++i) {
        Vec3 a=polygon->vertices[i],b=polygon->vertices[(i+1)%polygon->count];
        float da=dot(normal,a)-distance,db=dot(normal,b)-distance;
        if(da<=0)result.vertices[result.count++]=a;
        if((da<0 && db>0)||(da>0 && db<0))result.vertices[result.count++]=add(a,scale(sub(b,a),da/(da-db)));
    }
    return result;
}

static void layout_foundation(size_t *capacity,const Layout *layout,size_t room_index,float bottom,const unsigned char base[4],const unsigned char crest[4])
{
    const LayoutRoom *room=&layout->rooms[room_index];
    if(room->y==bottom)return;
    float x0=room->x-room->width*.5f,x1=room->x+room->width*.5f,z0=room->z-room->depth*.5f,z1=room->z+room->depth*.5f;
    Vec3 footprint[8];unsigned count=room->shape==ROCK ? 8 : 4;
    if(count==8) {
        float b=fminf(room->width,room->depth)*.16f;
        Vec3 points[]={v3(x0+b,room->y,z0),v3(x1-b,room->y,z0),v3(x1,room->y,z0+b),v3(x1,room->y,z1-b),v3(x1-b,room->y,z1),v3(x0+b,room->y,z1),v3(x0,room->y,z1-b),v3(x0,room->y,z0+b)};
        memcpy(footprint,points,sizeof(points));
    } else {Vec3 points[]={v3(x0,room->y,z0),v3(x1,room->y,z0),v3(x1,room->y,z1),v3(x0,room->y,z1)};memcpy(footprint,points,sizeof(points));}
    WorldSolid *deck=solid_append(capacity);solid_polygon(deck,footprint,count,room->y-12,crest);
    for(unsigned i=0;i<count;++i)memcpy(deck->color[i],base,4);
    if(room->shape==BRIDGE || room->y-bottom<12)return;
    LayoutPolygon *pieces=malloc(sizeof(*pieces));if(!pieces)abort();size_t piece_count=1;
    pieces[0]=(LayoutPolygon){malloc(count*sizeof(Vec3)),count};if(!pieces[0].vertices)abort();memcpy(pieces[0].vertices,footprint,count*sizeof(Vec3));
    for(size_t l=0;l<layout->link_count;++l) {
        const LayoutLink *link=&layout->links[l];
        if(link->travel!=NAV_WALK || link->from==room_index || link->to==room_index)continue;
        const LayoutRoom *from=&layout->rooms[link->from],*to=&layout->rooms[link->to];
        if(fmaxf(from->y,to->y)+28>room->y)continue;
        Vec3 a=layout_portal(from,to),b=layout_portal(to,from),delta=sub(b,a);delta.y=0;
        float span=length(delta);Vec3 direction=scale(delta,1/span),normal=v3(-direction.z,0,direction.x);
        float projection=dot(sub(v3(room->x,0,room->z),v3(a.x,0,a.z)),direction),radius=sqrtf(room->width*room->width+room->depth*room->depth)*.5f;
        if(projection+radius<0 || projection-radius>span)continue;
        float center=dot(normal,a),half=link->width*.5f+6;
        LayoutPolygon *split=malloc(piece_count*2*sizeof(*split));if(!split && piece_count)abort();size_t split_count=0;
        for(size_t i=0;i<piece_count;++i) {
            LayoutPolygon left=layout_clip(&pieces[i],normal,center-half),right=layout_clip(&pieces[i],scale(normal,-1),-center-half);
            if(left.count>=3)split[split_count++]=left;else free(left.vertices);
            if(right.count>=3)split[split_count++]=right;else free(right.vertices);
            free(pieces[i].vertices);
        }
        free(pieces);pieces=split;piece_count=split_count;
    }
    for(size_t i=0;i<piece_count;++i) {
        LayoutPolygon polygon=pieces[i];
        for(size_t v=1;v+1<polygon.count;++v) {
            Vec3 top[]={polygon.vertices[0],polygon.vertices[v],polygon.vertices[v+1]};
            for(unsigned k=0;k<3;++k)top[k].y=room->y-12;
            solid_polygon(solid_append(capacity),top,3,bottom-12,base);
        }
        free(polygon.vertices);
    }
    free(pieces);
}

static void layout_geometry(const SourceMap *source,const Layout *layout)
{
    if(!layout || !layout->room_count)abort();
    size_t material_count=0;const LayoutMaterial *materials=layout_material_plan(world_map_names[world_map_current],&material_count);
    unsigned char color[4]={0,0,0,255};uint64_t channels[3]={0},weight=0;
    for(size_t i=0;i<source->polygon_count;++i)for(unsigned v=0;v<3;++v) {
        const unsigned char *sample=source->polygons[i].color[v];
        weight+=sample[3];
        for(unsigned c=0;c<3;++c)channels[c]+=(uint64_t)sample[c]*sample[3];
    }
    if(!weight)abort();
    for(unsigned c=0;c<3;++c)color[c]=(unsigned char)(channels[c]/weight);
    unsigned char crest[4],stone[4],earth[4];
    memcpy(crest,color,4);memcpy(stone,color,4);memcpy(earth,color,4);
    uint64_t green[3]={0},bright[3]={0},warm[3]={0},green_weight=0,bright_weight=0,warm_weight=0;
    for(size_t i=0;i<source->polygon_count;++i)for(unsigned v=0;v<3;++v) {
        const unsigned char *sample=source->polygons[i].color[v];
        if(sample[1]*10>sample[0]*11 && sample[1]*10>sample[2]*11) {
            green_weight+=sample[3];
            for(unsigned c=0;c<3;++c)green[c]+=(uint64_t)sample[c]*sample[3];
        }
        if((sample[0]+sample[1]+sample[2])*2>(color[0]+color[1]+color[2])*3) {
            bright_weight+=sample[3];
            for(unsigned c=0;c<3;++c)bright[c]+=(uint64_t)sample[c]*sample[3];
        }
        if(sample[0]>sample[1] && sample[1]>=sample[2] && sample[2]*5>sample[0]) {
            warm_weight+=sample[3];
            for(unsigned c=0;c<3;++c)warm[c]+=(uint64_t)sample[c]*sample[3];
        }
    }
    for(unsigned c=0;c<3;++c) {
        if(green_weight)crest[c]=(unsigned char)(green[c]/green_weight);
        if(bright_weight)stone[c]=(unsigned char)(bright[c]/bright_weight);
        if(green_weight && warm_weight)earth[c]=(unsigned char)(warm[c]/warm_weight);
    }
    float left=FLT_MAX,right=-FLT_MAX,back=FLT_MAX,front=-FLT_MAX,bottom=FLT_MAX,highest=-FLT_MAX;
    for(size_t i=0;i<layout->room_count;++i) {
        const LayoutRoom *room=&layout->rooms[i];
        left=fminf(left,room->x-room->width*.5f);right=fmaxf(right,room->x+room->width*.5f);
        back=fminf(back,room->z-room->depth*.5f);front=fmaxf(front,room->z+room->depth*.5f);
        bottom=fminf(bottom,room->y);highest=fmaxf(highest,room->y+room->roof);
    }
    Vec3 midpoint=v3((left+right)*.5f,0,(back+front)*.5f);
    float ceiling=highest+180;
    Vec3 normals[16],boundary[16];float distances[16];
    for(unsigned i=0;i<16;++i) {
        float angle=(float)i*.39269908f;normals[i]=v3(cosf(angle),0,sinf(angle));distances[i]=-FLT_MAX;
        for(size_t r=0;r<layout->room_count;++r) {
            const LayoutRoom *room=&layout->rooms[r];
            float support=room->x*normals[i].x+room->z*normals[i].z+fabsf(normals[i].x)*room->width*.5f+fabsf(normals[i].z)*room->depth*.5f;
            distances[i]=fmaxf(distances[i],support+70);
        }
    }
    left=back=FLT_MAX;right=front=-FLT_MAX;
    for(unsigned i=0;i<16;++i) {
        unsigned next=(i+1)%16;Vec3 a=normals[i],b=normals[next];
        float determinant=a.x*b.z-a.z*b.x;
        boundary[i]=v3((distances[i]*b.z-a.z*distances[next])/determinant,bottom,(a.x*distances[next]-distances[i]*b.x)/determinant);
        left=fminf(left,boundary[i].x);right=fmaxf(right,boundary[i].x);back=fminf(back,boundary[i].z);front=fmaxf(front,boundary[i].z);
    }
    world_bounds=(Box){v3(left,bottom,back),v3(right,ceiling,front),SURFACE_STONE};
    size_t capacity=0;
    for(unsigned i=0;i<16;++i) {
        unsigned next=(i+1)%16;Vec3 a=boundary[i],b=boundary[next],middle=midpoint;middle.y=bottom;
        Vec3 floor[]={middle,a,b};solid_polygon(solid_append(&capacity),floor,3,bottom-30,crest);
        Vec3 miter_a=scale(add(normals[i],normals[next]),1/(1+dot(normals[i],normals[next])));
        unsigned after=(next+1)%16;
        Vec3 miter_b=scale(add(normals[next],normals[after]),1/(1+dot(normals[next],normals[after])));
        float ay=highest+90+24*sinf((float)i*1.7f),by=highest+90+24*sinf((float)next*1.7f);
        Vec3 top[]={add(a,scale(miter_a,-18)),add(a,scale(miter_a,35)),add(b,scale(miter_b,35)),add(b,scale(miter_b,-18))};
        top[0].y=top[1].y=ay;top[2].y=top[3].y=by;
        for(unsigned triangle=0;triangle<2;++triangle) {
            Vec3 face[]={top[0],top[triangle+1],top[triangle+2]};
            WorldSolid *wall=solid_append(&capacity);solid_polygon(wall,face,3,bottom-30,crest);
            for(unsigned v=0;v<3;++v)memcpy(wall->color[v],stone,4);
        }
        Vec3 cover[]={v3(midpoint.x,ceiling,midpoint.z),v3(a.x,ay+20,a.z),v3(b.x,by+20,b.z)};
        WorldSolid *sky=solid_append(&capacity);solid_polygon(sky,cover,3,0,world_background[0]);sky->texture=WORLD_SKY;
        for(unsigned v=0;v<3;++v)sky->vertices[v].y=cover[v].y-20;
    }
    for(size_t index=0;index<layout->room_count;++index) {
        const LayoutRoom *room=&layout->rooms[index];
        float x0=room->x-room->width*.5f,x1=room->x+room->width*.5f;
        float z0=room->z-room->depth*.5f,z1=room->z+room->depth*.5f;
        layout_foundation(&capacity,layout,index,bottom,room->shape==HALL ? color : earth,room->shape==HALL ? color : crest);
        if(room->shape==BRIDGE && room->y-bottom>=28) {
            const float offsets[4][2]={{-.32f,-.32f},{.32f,-.32f},{.32f,.32f},{-.32f,.32f}};
            for(unsigned c=0;c<4;++c) {
                float x=room->x+offsets[c][0]*room->width,z=room->z+offsets[c][1]*room->depth;
                layout_block(&capacity,v3(x-8,bottom,z-8),v3(x+8,room->y-8,z+8),color);
            }
        }
        float wall_height=room->roof>0 ? room->roof : room->shape==PIT ? 58 : 38;
        if(room->shape==ROCK || room->shape==BRIDGE)wall_height=0;
        for(unsigned side=0;side<4 && wall_height>0;++side) {
            float extent=side<2 ? room->depth*.5f : room->width*.5f;
            float openings[layout->link_count+1][2];size_t opening_count=0;
            for(size_t l=0;l<=layout->link_count;++l) {
                Vec3 portal;float width;
                if(l==layout->link_count) {
                    if(room->y!=bottom)continue;
                    LayoutRoom target=*room;target.x=room->x+(room->x-midpoint.x);target.z=room->z+(room->z-midpoint.z)-.01f;
                    portal=layout_portal(room,&target);width=72;
                } else {
                    const LayoutLink *link=&layout->links[l];
                    const LayoutRoom *a=&layout->rooms[link->from],*b=&layout->rooms[link->to];
                    float delta=side<2 ? b->x-a->x : b->z-a->z;
                    if(delta==0)continue;
                    float wall=side==0 ? x0 : side==1 ? x1 : side==2 ? z0 : z1;
                    float fraction=(wall-(side<2 ? a->x : a->z))/delta;
                    if(fraction<0 || fraction>1)continue;
                    portal=v3(a->x+(b->x-a->x)*fraction,room->y,a->z+(b->z-a->z)*fraction);
                    float crossing=side<2 ? portal.z-room->z : portal.x-room->x;
                    width=link->width*sqrtf((b->x-a->x)*(b->x-a->x)+(b->z-a->z)*(b->z-a->z))/fabsf(delta);
                    if(fabsf(crossing)>extent+width*.5f+12)continue;
                }
                unsigned face=fabsf(portal.x-x0)<.05f ? 0 : fabsf(portal.x-x1)<.05f ? 1 : fabsf(portal.z-z0)<.05f ? 2 : 3;
                if(l==layout->link_count && face!=side)continue;
                float p=side<2 ? portal.z-room->z : portal.x-room->x;
                openings[opening_count][0]=fmaxf(-extent,p-width*.5f-12);
                openings[opening_count++][1]=fminf(extent,p+width*.5f+12);
            }
            for(size_t i=1;i<opening_count;++i)for(size_t j=i;j>0 && openings[j][0]<openings[j-1][0];--j) {
                float lo=openings[j][0],hi=openings[j][1];openings[j][0]=openings[j-1][0];openings[j][1]=openings[j-1][1];openings[j-1][0]=lo;openings[j-1][1]=hi;
            }
            float cursor=-extent;
            for(size_t gap=0;gap<=opening_count;++gap) {
                float end=gap==opening_count ? extent : openings[gap][0];
                if(end>cursor+.01f) {
                    Vec3 low,high;
                    if(side<2) {float x=side==0 ? x0 : x1;low=v3(x-6,room->y,room->z+cursor);high=v3(x+6,room->y+wall_height,room->z+end);}
                    else {float z=side==2 ? z0 : z1;low=v3(room->x+cursor,room->y,z-6);high=v3(room->x+end,room->y+wall_height,z+6);}
                    layout_block(&capacity,low,high,color);
                }
                if(gap<opening_count)cursor=fmaxf(cursor,openings[gap][1]);
            }
        }
        if(room->roof>0)layout_block(&capacity,v3(x0,room->y+room->roof,z0),v3(x1,room->y+room->roof+10,z1),color);
        if(room->shape==ROCK || room->shape==PIT || room->shape==COURTYARD) {
            float radius=fminf(room->shape==ROCK ? 19 : 13,fminf(room->width,room->depth)*.1f),height=room->shape==ROCK ? 32 : 22;
            float x=room->x,z=room->z,clearance=0;
            for(unsigned corner=0;corner<4;++corner) {
                Vec3 candidate=v3(room->x+((corner&1)?1:-1)*room->width*.3f,0,room->z+((corner&2)?1:-1)*room->depth*.3f);
                int occupied=0;
                for(size_t m=0;m<material_count;++m) {
                    const LayoutMaterial *material=&materials[m];
                    if(material->room==index && fabsf(candidate.x-room->x-material->x)<radius*1.35f+material->width*.5f+3 &&
                        fabsf(candidate.z-room->z-material->z)<radius*1.35f+material->depth*.5f+3)occupied=1;
                }
                if(occupied)continue;
                float nearest=FLT_MAX;
                for(size_t l=0;l<layout->link_count;++l) {
                    const LayoutRoom *a=&layout->rooms[layout->links[l].from],*b=&layout->rooms[layout->links[l].to];
                    Vec3 start=v3(a->x,0,a->z),delta=v3(b->x-a->x,0,b->z-a->z);
                    float t=fmaxf(0,fminf(1,dot(sub(candidate,start),delta)/dot(delta,delta)));
                    nearest=fminf(nearest,length(sub(candidate,add(start,scale(delta,t)))));
                }
                if(nearest>clearance){clearance=nearest;x=candidate.x;z=candidate.z;}
            }
            if(clearance<radius*1.35f+7)continue;
            Vec3 top[8];for(unsigned v=0;v<8;++v){float angle=(float)v*.78539816f;top[v]=v3(x+cosf(angle)*radius,room->y+height,z+sinf(angle)*radius);}
            WorldSolid *cover=solid_append(&capacity);solid_polygon(cover,top,8,room->y,color);
            for(unsigned v=0;v<8;++v){cover->vertices[v].x=x+(top[v].x-x)*1.35f;cover->vertices[v].z=z+(top[v].z-z)*1.35f;}
        }
    }
    for(size_t i=0;i<layout->link_count;++i) {
        const LayoutLink *link=&layout->links[i];const LayoutRoom *a=&layout->rooms[link->from],*b=&layout->rooms[link->to];
        if(link->travel!=NAV_WALK)continue;
        Vec3 start=layout_portal(a,b),end=layout_portal(b,a);
        if(a->y!=bottom || b->y!=bottom) {
            size_t ramp_index=layout_slab(&capacity,start,end,link->width,8,color);
            WorldSolid *ramp=&world_solids[ramp_index];
            if(a->shape!=HALL && a->shape!=BRIDGE && b->shape!=HALL && b->shape!=BRIDGE)
                for(unsigned v=0;v<4;++v) {
                    memcpy(ramp->color[v],earth,4);
                    memcpy(ramp->color[v+4],crest,4);
                }
        }
        if(a->roof>0 && b->roof>0)layout_slab(&capacity,add(start,v3(0,fminf(a->roof,b->roof)+8,0)),add(end,v3(0,fminf(a->roof,b->roof)+8,0)),link->width,8,color);
    }
    world_prop_count=source->prop_count;world_props=calloc(world_prop_count,sizeof(*world_props));if(!world_props && world_prop_count)abort();
    Vec3 source_min=v3(FLT_MAX,0,FLT_MAX),source_max=v3(-FLT_MAX,0,-FLT_MAX);
    for(size_t i=0;i<world_source_spawn_count;++i){Vec3 p=world_source_spawns[i].position;source_min.x=fminf(source_min.x,p.x);source_min.z=fminf(source_min.z,p.z);source_max.x=fmaxf(source_max.x,p.x);source_max.z=fmaxf(source_max.z,p.z);}
    spawn_rooms=malloc(world_source_spawn_count*sizeof(*spawn_rooms));
    if(!spawn_rooms)abort();
    for(size_t i=0;i<world_source_spawn_count;++i) {
        WorldSpawn *spawn=&world_source_spawns[i];unsigned room_index;
        if(spawn->type==1 || spawn->type==5)room_index=layout->alpha_room;
        else if(spawn->type==2 || spawn->type==6)room_index=layout->bravo_room;
        else if(spawn->type==14)room_index=layout->neutral_room;
        else {
            float x=(spawn->position.x-source_min.x)/fmaxf(1,source_max.x-source_min.x);
            float y=1-(spawn->position.z-source_min.z)/fmaxf(1,source_max.z-source_min.z);
            float nearest=FLT_MAX;room_index=0;
            for(size_t r=0;r<layout->room_count;++r) {
                const LayoutRoom *room=&layout->rooms[r];
                float dx=x-(room->x-left)/(right-left),dy=y-(room->y-bottom)/fmaxf(1,highest-bottom);
                float value=dx*dx+dy*dy*.4f;
                if(value<nearest){nearest=value;room_index=(unsigned)r;}
            }
        }
        const LayoutRoom *room=&layout->rooms[room_index];
        spawn_rooms[i]=room_index;
        float offset=spawn->type==5 || spawn->type==6 || spawn->type==14 ? 0 : (float)((int)(i%3)-1)*12;
        spawn->position=v3(room->x+offset,room->y+.05f,room->z);
        if(spawn->type>=7 && spawn->type!=14)spawn->position.y+=6;
    }
    for(size_t i=0;i<world_prop_count;++i) {
        WorldProp prop=source->props[i];const LayoutRoom *room=&layout->rooms[i%layout->room_count];
        float side=i%2 ? 1 : -1;
        prop.position=v3(room->x+side*room->width*.35f,room->y,room->z+((i/2)%2 ? 1 : -1)*room->depth*.35f);
        prop.yaw+=(float)(i%4)*1.5707963f;
        float fit=fminf(1,fminf(room->width,room->depth)*.35f/fmaxf(1,(float)prop.width*fabsf(prop.scale_x)));
        prop.scale_x*=fit;prop.scale_y*=fit;world_props[i]=prop;
    }
}

static void layout_materials(const SourceMap *source,const Layout *layout)
{
    size_t count=0;
    const LayoutMaterial *materials=layout_material_plan(world_map_names[world_map_current],&count);
    for(size_t m=0;m<count;++m) {
        const LayoutMaterial *material=&materials[m];
        const LayoutRoom *room=&layout->rooms[material->room];
        Vec3 chosen=v3(room->x+material->x,room->y,room->z+material->z);
        uint64_t channels[3]={0},weight=0;size_t source_count=0;int source_bounce=material->type!=18;
        for(size_t i=0;i<source->polygon_count;++i)if(source->polygons[i].type==material->type) {
            ++source_count;
            if(fabsf(source->polygons[i].bounciness-material->bounciness)<.0001f)source_bounce=1;
            for(unsigned v=0;v<3;++v) {
                const unsigned char *sample=source->polygons[i].color[v];weight+=sample[3];
                for(unsigned c=0;c<3;++c)channels[c]+=(uint64_t)sample[c]*sample[3];
            }
        }
        if(!source_count || !source_bounce){fprintf(stderr,"Material plan disagrees with source: %s type%u bounce%g\n",world_map_names[world_map_current],material->type,material->bounciness);abort();}
        if(!weight)for(size_t i=0;i<source->polygon_count;++i)for(unsigned v=0;v<3;++v) {
            const unsigned char *sample=source->polygons[i].color[v];weight+=sample[3];
            for(unsigned c=0;c<3;++c)channels[c]+=(uint64_t)sample[c]*sample[3];
        }
        unsigned char paint[4]={0,0,0,255};for(unsigned c=0;c<3;++c)paint[c]=(unsigned char)(channels[c]/weight);
        size_t original_count=world_solid_count,capacity=world_solid_count;
        if(material->height>0) {
            Vec3 half=v3(material->width*.5f,0,material->depth*.5f);
            layout_block(&capacity,sub(chosen,half),add(chosen,add(half,v3(0,material->height,0))),paint);
            world_solids[world_solid_count-1].poly_type=material->type;
            world_solids[world_solid_count-1].bounciness=material->bounciness;
            for(size_t i=0;i<world_gate_count;++i)if(!memcmp(world_solids[world_gates[i]].vertices,world_solids[world_solid_count-1].vertices,8*sizeof(Vec3)))
                world_solids[world_solid_count-1].texture=WORLD_HIDDEN;
            size_t *gates=realloc(world_gates,(world_gate_count+1)*sizeof(*gates));if(!gates)abort();
            world_gates=gates;world_gates[world_gate_count++]=world_solid_count-1;
            world_build_hulls();continue;
        }
        int painted=0;
        float half_x=material->width*.5f,half_z=material->depth*.5f;
        for(size_t i=0;i<original_count;++i) {
            WorldSolid original=world_solids[i];
            if(original.poly_type!=0)continue;
            unsigned vertices=original.face_size[1];int level=1;
            for(unsigned v=0;v<vertices;++v)if(fabsf(original.vertices[original.faces[1][v]].y-chosen.y)>.01f)level=0;
            if(!level || hulls[i].max.x<chosen.x-half_x || hulls[i].min.x>chosen.x+half_x || hulls[i].max.z<chosen.z-half_z || hulls[i].min.z>chosen.z+half_z)continue;
            LayoutPolygon inside={malloc(vertices*sizeof(Vec3)),vertices};if(!inside.vertices)abort();
            for(unsigned v=0;v<vertices;++v)inside.vertices[v]=original.vertices[original.faces[1][v]];
            Vec3 normals[]={v3(1,0,0),v3(-1,0,0),v3(0,0,1),v3(0,0,-1)};
            float limits[]={chosen.x+half_x,-chosen.x+half_x,chosen.z+half_z,-chosen.z+half_z};
            LayoutPolygon pieces[5];size_t piece_count=0;
            for(unsigned clip=0;clip<4;++clip) {
                pieces[piece_count++]=layout_clip(&inside,scale(normals[clip],-1),-limits[clip]);
                LayoutPolygon next=layout_clip(&inside,normals[clip],limits[clip]);free(inside.vertices);inside=next;
            }
            pieces[piece_count++]=inside;int replaced=0;
            for(size_t part=0;part<piece_count;++part) {
                LayoutPolygon polygon=pieces[part];int special=part==piece_count-1;
                for(size_t v=1;v+1<polygon.count;++v) {
                    Vec3 top[]={polygon.vertices[0],polygon.vertices[v],polygon.vertices[v+1]};
                    Vec3 a=sub(top[1],top[0]),b=sub(top[2],top[0]);if(a.x*b.z-a.z*b.x==0)continue;
                    WorldSolid replacement={0};solid_polygon(&replacement,top,3,hulls[i].min.y,special ? paint : original.color[original.faces[1][0]]);
                    for(unsigned k=0;k<3;++k)memcpy(replacement.color[k],original.color[original.faces[0][0]],4);
                    if(special){replacement.poly_type=material->type;replacement.bounciness=material->bounciness;painted=1;}
                    if(!replaced){world_solids[i]=replacement;replaced=1;}else *solid_append(&capacity)=replacement;
                }
                free(polygon.vertices);
            }
            if(!replaced){fprintf(stderr,"Empty floor partition: %s material%zu hull%zu\n",world_map_names[world_map_current],m,i);abort();}
        }
        if(!painted){fprintf(stderr,"Material has no floor: %s material%zu room%u at%g,%g,%g\n",world_map_names[world_map_current],m,material->room,chosen.x,chosen.y,chosen.z);abort();}
        world_build_hulls();
    }
}

typedef struct { size_t from,to; float distance; } LayoutPair;
static int layout_pair_compare(const void *left,const void *right)
{
    const LayoutPair *a=left,*b=right;return a->distance<b->distance ? -1 : a->distance>b->distance;
}

static int layout_ground_route(Vec3 start,Vec3 end,WorldQuery query)
{
    Vec3 delta=sub(end,start);float horizontal=sqrtf(delta.x*delta.x+delta.z*delta.z);
    unsigned steps=(unsigned)ceilf(horizontal/6);Vec3 previous=start;
    for(unsigned i=1;i<=steps;++i) {
        Vec3 next=add(start,scale(delta,(float)i/(float)steps));
        Vec3 from=v3(next.x,previous.y+8,next.z),to=v3(next.x,world_bounds.min.y-8,next.z);
        WorldHit floor=world_trace_for(from,to,v3(3,0,3),query);
        if(floor.box<0 || floor.normal.y<.5f)return 0;
        next.y=from.y+(to.y-from.y)*floor.fraction+.05f;
        if(!world_pose_clear_for(next,STANDING,query))return 0;
        WorldHit path=world_trace_for(add(previous,v3(0,7,0)),add(next,v3(0,7,0)),v3(3,6.99f,3),query);
        if(path.box>=0 && path.normal.y<.5f)return 0;
        previous=next;
    }
    return fabsf(previous.y-end.y)<7;
}

static int layout_jump_route(Vec3 start,Vec3 end)
{
    Actor actor={.position=start,.previous=start,.pose=STANDING,.contact=GROUNDED,.animation=MOVE_IDLE,.animation_tick=1};
    int descending=0;unsigned impulse=15;
    for(uint64_t tick=0;;++tick) {
        Input input={0};
        if(tick<impulse)input.held=INPUT_JUMP;
        if(tick>=impulse) {
            Vec3 travel=sub(end,actor.position);travel.y=0;float distance=length(travel);
            Vec3 desired=distance>1 ? scale(travel,fminf(2.8f,distance*.14f)/distance) : v3(0,0,0);
            Vec3 move=sub(desired,v3(actor.velocity.x,0,actor.velocity.z));float magnitude=length(move);
            if(magnitude>1)move=scale(move,1/magnitude);
            input.forward=move.z;input.right=-move.x;
        }
        movement_step(&actor,input,tick);
        if(tick>=impulse && actor.velocity.y<0)descending=1;
        if(tick>=impulse && (actor.contact==GROUNDED || (descending && actor.velocity.y>=0)))
            return actor.contact==GROUNDED && length(sub(actor.position,end))<12 && fabsf(actor.position.y-end.y)<7;
    }
}

static void layout_navigation(const Layout *layout)
{
    WorldQuery query={WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG};
    for(size_t i=0;i<layout->room_count;++i){const LayoutRoom *room=&layout->rooms[i];nav_node(v3(room->x,room->y+.05f,room->z),0);}
    for(size_t i=0;i<layout->link_count;++i) {
        const LayoutLink *link=&layout->links[i];const LayoutRoom *a=&layout->rooms[link->from],*b=&layout->rooms[link->to];
        Vec3 start=layout_portal(a,b),end=layout_portal(b,a);
        Vec3 inward_a=sub(v3(a->x,a->y,a->z),start),inward_b=sub(v3(b->x,b->y,b->z),end);
        start=add(start,scale(inward_a,12/length(inward_a)));end=add(end,scale(inward_b,12/length(inward_b)));
        start.y+=.05f;end.y+=.05f;
        Vec3 centers[]={v3(a->x,a->y+.05f,a->z),v3(b->x,b->y+.05f,b->z)};
        Vec3 *portals[]={&start,&end};
        for(unsigned side=0;side<2;++side) {
            WorldHit approach=world_trace_for(add(centers[side],v3(0,7,0)),add(*portals[side],v3(0,7,0)),v3(3,7,3),query);
            if(approach.box>=0)
                *portals[side]=add(centers[side],scale(sub(*portals[side],centers[side]),fmaxf(0,approach.fraction-.001f)));
        }
        Vec3 delta=sub(end,start);float horizontal=sqrtf(delta.x*delta.x+delta.z*delta.z);
        size_t from=nav_node(start,1),to=nav_node(end,1);
        layout_edge(link->from,from,NAV_WALK,0,0);layout_edge(from,link->from,NAV_WALK,0,0);
        layout_edge(link->to,to,NAV_WALK,0,0);layout_edge(to,link->to,NAV_WALK,0,0);
        NavTravel travel=link->travel;int fuel=travel==NAV_JET ? (int)ceilf(10+fabsf(delta.y)*.7f+horizontal*.18f) : 0;
        if(fuel>world_jet_fuel) {fprintf(stderr,"Jet route exceeds source fuel: %s link%zu needs%d has%d\n",world_map_names[world_map_current],i,fuel,world_jet_fuel);abort();}
        float apex=fmaxf(start.y,end.y)+(travel==NAV_JUMP ? 32 : 24);
        layout_edge(from,to,travel==NAV_JET && delta.y<-8 ? NAV_DROP : travel,travel==NAV_JET && delta.y<-8 ? 0 : fuel,apex);
        layout_edge(to,from,travel==NAV_JET && delta.y>8 ? NAV_DROP : travel,travel==NAV_JET && delta.y>8 ? 0 : fuel,apex);
    }
    for(size_t i=0;i<layout->room_count;++i) {
        const LayoutRoom *room=&layout->rooms[i];
        for(unsigned corner=0;corner<4;++corner) {
            Vec3 p=v3(room->x+((corner&1)?1:-1)*(room->width*.5f+14),world_bounds.min.y+.05f,room->z+((corner&2)?1:-1)*(room->depth*.5f+14));
            if(world_pose_clear_for(p,STANDING,query))nav_node(p,8);
        }
        if(room->shape==BRIDGE && room->y-world_bounds.min.y>=28)nav_node(v3(room->x,world_bounds.min.y+.05f,room->z),8);
        if(room->y==world_bounds.min.y) {
            LayoutRoom outside=*room;
            outside.x=room->x+room->x-(world_bounds.min.x+world_bounds.max.x)*.5f;
            outside.z=room->z+room->z-(world_bounds.min.z+world_bounds.max.z)*.5f-.01f;
            Vec3 portal=layout_portal(room,&outside),outward=sub(portal,v3(room->x,room->y,room->z));
            Vec3 p=add(portal,scale(outward,16/length(outward)));p.y+=.05f;
            if(world_pose_clear_for(p,STANDING,query))nav_node(p,8);
        }
    }
    for(size_t solid=0;solid<world_solid_count;++solid) {
        if(hulls[solid].max.y<=world_bounds.min.y+.1f || hulls[solid].min.y>=world_bounds.min.y+14)continue;
        const WorldSolid *shape=&world_solids[solid];Vec3 center=scale(add(hulls[solid].min,hulls[solid].max),.5f);
        for(unsigned face=0;face<shape->face_count;++face)for(unsigned edge=0;edge<shape->face_size[face];++edge) {
            Vec3 a=shape->vertices[shape->faces[face][edge]],b=shape->vertices[shape->faces[face][(edge+1)%shape->face_size[face]]];
            float plane=world_bounds.min.y+14;
            if(a.y>plane && b.y>plane)continue;
            if(a.y>plane)a=add(a,scale(sub(b,a),(plane-a.y)/(b.y-a.y)));
            a.x+=a.x<center.x ? -8 : 8;a.z+=a.z<center.z ? -8 : 8;a.y=world_bounds.min.y+.05f;
            if(!world_pose_clear_for(a,STANDING,query))continue;
            WorldHit ground=world_trace_for(add(a,v3(0,7,0)),add(a,v3(0,6.9f,0)),v3(3,7,3),query);
            if(ground.box>=0 && ground.normal.y>.5f)nav_node(a,8);
        }
    }
    size_t count=world_nav_node_count,*component=malloc(count*sizeof(*component));unsigned *degree=calloc(count,sizeof(*degree));
    LayoutPair *pairs=malloc(count*(count-1)/2*sizeof(*pairs));if(!component || !degree || !pairs)abort();
    for(size_t i=0;i<count;++i)component[i]=i;
    for(size_t i=0;i<world_nav_link_count;++i){NavLink edge=world_nav_links[i];size_t a=(size_t)edge.from,b=(size_t)edge.to;while(component[a]!=a)a=component[a];while(component[b]!=b)b=component[b];component[a]=b;++degree[edge.from];}
    size_t pair_count=0;
    for(size_t i=0;i<count;++i)for(size_t j=0;j<i;++j) {
        Vec3 a=world_nav_nodes[i].position,b=world_nav_nodes[j].position;
        if(fabsf(a.y-b.y)>.1f)continue;
        pairs[pair_count++]=(LayoutPair){i,j,length(sub(a,b))};
    }
    qsort(pairs,pair_count,sizeof(*pairs),layout_pair_compare);
    for(size_t i=0;i<pair_count;++i) {
        LayoutPair pair=pairs[i];size_t a=pair.from,b=pair.to;while(component[a]!=a)a=component[a];while(component[b]!=b)b=component[b];
        if(a==b && degree[pair.from]>=3 && degree[pair.to]>=3)continue;
        Vec3 start=world_nav_nodes[pair.from].position,end=world_nav_nodes[pair.to].position;
        WorldHit path=world_trace_for(add(start,v3(0,7,0)),add(end,v3(0,7,0)),v3(3,6.8f,3),query);
        if(path.box>=0 && (path.normal.y<.5f || !layout_ground_route(start,end,query) || !layout_ground_route(end,start,query)))continue;
        if(start.y>world_bounds.min.y+1)continue;
        layout_edge(pair.from,pair.to,NAV_WALK,0,0);layout_edge(pair.to,pair.from,NAV_WALK,0,0);
        component[a]=b;++degree[pair.from];++degree[pair.to];
    }
    pair_count=0;
    for(size_t i=0;i<count;++i)for(size_t j=0;j<i;++j) {
        Vec3 delta=sub(world_nav_nodes[i].position,world_nav_nodes[j].position);
        if(fabsf(delta.y)>80 || delta.x*delta.x+delta.z*delta.z>120*120)continue;
        pairs[pair_count++]=(LayoutPair){i,j,length(delta)};
    }
    qsort(pairs,pair_count,sizeof(*pairs),layout_pair_compare);
    for(size_t i=0;i<pair_count;++i) {
        LayoutPair pair=pairs[i];size_t a=pair.from,b=pair.to;while(component[a]!=a)a=component[a];while(component[b]!=b)b=component[b];
        if(a==b)continue;
        Vec3 start=world_nav_nodes[pair.from].position,end=world_nav_nodes[pair.to].position;
        if(!layout_jump_route(start,end) || !layout_jump_route(end,start))continue;
        float apex=fmaxf(start.y,end.y)+24;
        layout_edge(pair.from,pair.to,NAV_JUMP,0,apex);layout_edge(pair.to,pair.from,NAV_JUMP,0,apex);component[a]=b;
    }
    size_t root=0;while(component[root]!=root)root=component[root];
    for(size_t i=0;i<count;++i){size_t other=i;while(component[other]!=other)other=component[other];if(other!=root){fprintf(stderr,"Disconnected room floor %s node%zu at%g,%g,%g\n",world_map_names[world_map_current],i,world_nav_nodes[i].position.x,world_nav_nodes[i].position.y,world_nav_nodes[i].position.z);abort();}}
    free(pairs);free(component);free(degree);
}

size_t world_map_index(const char *name)
{
    for (size_t i=0;i<world_map_count;++i) if (!strcmp(name,world_map_names[i])) return i;
    return SIZE_MAX;
}

size_t world_team_spawn_count(unsigned team)
{
    assert(team<=TEAM_DELTA);
    return player_spawns[team].count;
}

Vec3 world_team_spawn(unsigned team, size_t index)
{
    assert(team<=TEAM_DELTA && index<player_spawns[team].count);
    return player_spawns[team].positions[index];
}

size_t world_marker_spawn_count(unsigned type)
{
    size_t count=0;
    for(size_t i=0;i<world_source_spawn_count;++i)count+=world_source_spawns[i].type==type;
    return count ? count : world_source_spawn_count;
}

Vec3 world_marker_spawn(unsigned type,size_t index)
{
    size_t matching=0;
    for(size_t i=0;i<world_source_spawn_count;++i)matching+=world_source_spawns[i].type==type;
    for(size_t i=0;i<world_source_spawn_count;++i) {
        if(matching && world_source_spawns[i].type!=type)continue;
        if(index--==0) {
            WorldSpawn spawn=world_source_spawns[i];
            if(spawn.type>=7 && spawn.type!=14)spawn.position.y-=6;
            return spawn.position;
        }
    }
    abort();
}

static void layout_player_spawns(const Layout *layout)
{
    const float spacing=2*SRC_PART_RADIUS;
    for(unsigned team=TEAM_NONE;team<=TEAM_DELTA;++team) {
        size_t matching=0,capacity=0;
        for(size_t i=0;i<world_source_spawn_count;++i)matching+=world_source_spawns[i].type==team;
        unsigned char rooms[layout->room_count];memset(rooms,0,sizeof(rooms));
        for(size_t i=0;i<world_source_spawn_count;++i)
            if(!matching || world_source_spawns[i].type==team)rooms[spawn_rooms[i]]=1;
        WorldQuery query={WORLD_TRACE_ACTOR,team,WORLD_NO_FLAG};
        for(size_t r=0;r<layout->room_count;++r)if(rooms[r]) {
            const LayoutRoom *room=&layout->rooms[r];
            int columns=(int)floorf((room->width-spacing)/spacing)+1;
            int rows=(int)floorf((room->depth-spacing)/spacing)+1;
            for(int z=0;z<rows;++z)for(int x=0;x<columns;++x) {
                Vec3 position=v3(room->x+((float)x-(float)(columns-1)*.5f)*spacing,room->y+.05f,
                    room->z+((float)z-(float)(rows-1)*.5f)*spacing);
                if(!world_pose_clear_for(position,STANDING,query))continue;
                WorldHit support=world_trace_for(add(position,v3(0,1,0)),sub(position,v3(0,.2f,0)),v3(3,0,3),query);
                if(support.box<0 || support.normal.y<.5f)continue;
                unsigned type=world_solids[support.box].poly_type;
                if(type==5 || type==6 || type==7 || type==9 || type==18 || type==19 || type==20)continue;
                if(!layout_ground_route(position,v3(room->x,room->y+.05f,room->z),query))continue;
                int duplicate=0;
                for(size_t i=0;i<player_spawns[team].count;++i)
                    if(length(sub(position,player_spawns[team].positions[i]))<.01f){duplicate=1;break;}
                if(duplicate)continue;
                if(player_spawns[team].count==capacity) {
                    capacity=capacity ? capacity*2 : ACTOR_COUNT;
                    Vec3 *positions=realloc(player_spawns[team].positions,capacity*sizeof(*positions));
                    if(!positions)abort();
                    player_spawns[team].positions=positions;
                }
                player_spawns[team].positions[player_spawns[team].count++]=position;
            }
        }
        if(!player_spawns[team].count){fprintf(stderr,"No legal player spawn sites on %s for team%u\n",world_map_names[world_map_current],team);abort();}
    }
    for(size_t i=0;i<ACTOR_COUNT;++i)world_spawns[i]=world_team_spawn(TEAM_NONE,i%world_team_spawn_count(TEAM_NONE));
}

Vec3 world_objective_spawn(unsigned type)
{
    for (size_t i=0;i<world_source_spawn_count;++i)
        if (world_source_spawns[i].type==type) return world_source_spawns[i].position;
    abort();
}

int world_supports_mode(GameMode mode)
{
    unsigned flags=0;
    for (size_t i=0;i<world_source_spawn_count;++i) {
        if (world_source_spawns[i].type==5) flags|=1;
        if (world_source_spawns[i].type==6) flags|=2;
    }
    if (mode==MODE_CTF || mode==MODE_INF) return flags==3;
    return world_source_spawn_count>0;
}

void world_init(void)
{
    world_load(0);
}

void world_load(size_t index)
{
    if (index>=world_map_count) abort();
    world_free();world_map_current=index;
    snprintf(world_texture,sizeof(world_texture),"%s",map_textures[index]);
    char path[512];snprintf(path,sizeof(path),"%s/maps/%s.pms",SOLDAT_ASSET_DIR,world_map_names[index]);
    FILE *file=fopen(path,"rb");
    if (!file) {perror(path);exit(EXIT_FAILURE);}
    if (read_u32(file)!=11) abort();
    skip_bytes(file,39+25);
    for (int i=0;i<2;++i) {
        unsigned char bgra[4];read_bytes(file,bgra,4);
        world_background[i][0]=bgra[2];world_background[i][1]=bgra[1];
        world_background[i][2]=bgra[0];world_background[i][3]=255;
    }
    world_jet_fuel=119*(int)read_u32(file)/100;
    unsigned char settings[4];read_bytes(file,settings,4);skip_bytes(file,4);
    world_grenades=settings[0];world_medikits=settings[1];
    SourceMap source={0};
    size_t polygons=read_u32(file);
    source.polygons=calloc(polygons,sizeof(*source.polygons));
    if (!source.polygons) abort();
    for (size_t i=0;i<polygons;++i) {
        SourcePolygon polygon={0};Vec3 vertices[3]={0};
        for (unsigned j=0;j<3;++j) {
            vertices[j].x=read_float(file);vertices[j].z=read_float(file);skip_bytes(file,8);
            unsigned char bgra[4];read_bytes(file,bgra,4);
            polygon.color[j][0]=bgra[2];polygon.color[j][1]=bgra[1];polygon.color[j][2]=bgra[0];polygon.color[j][3]=bgra[3];
            skip_bytes(file,8);
        }
        for (unsigned j=0;j<3;++j) {
            float nx=read_float(file),ny=read_float(file);read_float(file);
            if (j==2) polygon.bounciness=sqrtf(nx*nx+ny*ny);
        }
        unsigned char type;read_bytes(file,&type,1);polygon.type=type;
        Vec3 a=sub(vertices[1],vertices[0]),b=sub(vertices[2],vertices[0]);
        if (a.x*b.z-a.z*b.x!=0) source.polygons[source.polygon_count++]=polygon;
    }
    read_u32(file);uint32_t sectors=2*read_u32(file)+1;
    for (uint32_t i=0;i<sectors*sectors;++i) {
        unsigned char bytes[2];read_bytes(file,bytes,2);
        skip_bytes(file,2*((unsigned)bytes[0]|(unsigned)bytes[1]<<8));
    }
    source.prop_count=read_u32(file);source.props=calloc(source.prop_count,sizeof(*source.props));
    if (!source.props && source.prop_count) abort();
    for (size_t i=0;i<source.prop_count;++i) {
        WorldProp *prop=&source.props[i];uint32_t flags=read_u32(file);
        prop->active=flags&255;prop->style=flags>>16;
        prop->width=(int32_t)read_u32(file);prop->height=(int32_t)read_u32(file);
        prop->position.x=read_float(file);prop->position.z=read_float(file);
        prop->yaw=read_float(file);prop->scale_x=read_float(file);prop->scale_y=read_float(file);
        unsigned alpha=read_u32(file)&255;unsigned char bgra[4];read_bytes(file,bgra,4);
        prop->color[0]=bgra[2];prop->color[1]=bgra[1];prop->color[2]=bgra[0];prop->color[3]=(unsigned char)alpha;
        skip_bytes(file,4);
        prop->active=prop->active && alpha && prop->width>0 && prop->height>0 && prop->scale_x!=0 && prop->scale_y!=0;
    }
    world_scenery_count=read_u32(file);world_scenery=calloc(world_scenery_count,sizeof(*world_scenery));
    if (!world_scenery && world_scenery_count) abort();
    for (size_t i=0;i<world_scenery_count;++i) {
        unsigned char count;read_bytes(file,&count,1);read_bytes(file,world_scenery[i],50);
        world_scenery[i][count]='\0';skip_bytes(file,4);
        for (unsigned c=0;c<count;++c) if (world_scenery[i][c]>='A' && world_scenery[i][c]<='Z') world_scenery[i][c]+='a'-'A';
    }
    size_t colliders=read_u32(file);skip_bytes(file,16*colliders);
    size_t spawns=read_u32(file);world_source_spawns=calloc(spawns,sizeof(*world_source_spawns));
    if (!world_source_spawns) abort();
    for (size_t i=0;i<spawns;++i) {
        unsigned active=read_u32(file);
        float x=(float)(int32_t)read_u32(file),z=(float)(int32_t)read_u32(file);
        unsigned type=read_u32(file);
        if (active) world_source_spawns[world_source_spawn_count++]=(WorldSpawn){v3(x,0,z),type};
    }
    size_t waypoint_count=read_u32(file);skip_bytes(file,112*waypoint_count);
    if (fclose(file)!=0) {perror(path);exit(EXIT_FAILURE);}
    layout_geometry(&source,world_layout(world_map_names[index]));
    world_build_hulls();
    layout_materials(&source,world_layout(world_map_names[index]));
    layout_navigation(world_layout(world_map_names[index]));
    layout_player_spawns(world_layout(world_map_names[index]));
    unsigned *styles=calloc(world_scenery_count,sizeof(*styles));
    char (*scenery)[51]=calloc(world_scenery_count,sizeof(*scenery));size_t used=0;
    if ((!styles || !scenery) && world_scenery_count) abort();
    for (size_t i=0;i<world_prop_count;++i) {
        WorldProp *prop=&world_props[i];if (!prop->active) continue;
        unsigned original=prop->style-1;
        if (styles[original]==0) {styles[original]=(unsigned)++used;memcpy(scenery[used-1],world_scenery[original],51);}
        prop->style=styles[original];

    }
    free(styles);free(world_scenery);world_scenery=scenery;world_scenery_count=used;
    free(source.polygons);free(source.props);
}

void world_free(void)
{
    free(world_solids);free(hulls);free(world_tree);free(world_props);free(world_scenery);free(world_source_spawns);
    free(world_nav_nodes);free(world_nav_links);free(world_gates);
    free(spawn_rooms);spawn_rooms=NULL;
    for(unsigned team=TEAM_NONE;team<=TEAM_DELTA;++team) {
        free(player_spawns[team].positions);player_spawns[team].positions=NULL;player_spawns[team].count=0;
    }
    world_solids=NULL;hulls=NULL;world_tree=NULL;world_tree_count=0;world_props=NULL;world_scenery=NULL;world_source_spawns=NULL;
    world_nav_nodes=NULL;world_nav_links=NULL;world_gates=NULL;world_gate_count=0;
    world_solid_count=0;world_prop_count=0;world_scenery_count=0;world_source_spawn_count=0;
    world_nav_node_count=0;world_nav_link_count=0;
}

static int world_type_collides(unsigned type,WorldQuery query)
{
    if(type==3 || type==23 || type==24 || type==25)return 0;
    if((type==1 && query.kind!=WORLD_TRACE_BULLET && query.kind!=WORLD_TRACE_ENVIRONMENT) ||
        (type==2 && query.kind!=WORLD_TRACE_ACTOR && query.kind!=WORLD_TRACE_ENVIRONMENT))return 0;
    if(type>=10 && type<=17) {
        if(query.team!=1+(type-10)/2)return 0;
        if((type%2==0 && query.kind!=WORLD_TRACE_BULLET) || (type%2==1 && query.kind==WORLD_TRACE_BULLET))return 0;
    }
    if((type==21 || type==22) && query.kind!=WORLD_TRACE_ACTOR && query.kind!=WORLD_TRACE_ENVIRONMENT)return 0;
    if((type==21 && query.flag==WORLD_NO_FLAG) || (type==22 && query.flag==WORLD_HAS_FLAG))return 0;
    return 1;
}

int world_nav_link_allows(const NavLink *link,WorldQuery query)
{
    Vec3 from=world_nav_nodes[link->from].position,to=world_nav_nodes[link->to].position;
    Vec3 points[]={add(from,v3(0,7,0)),v3(from.x,link->apex+7,from.z),v3(to.x,link->apex+7,to.z),add(to,v3(0,7,0))};
    if(link->mode==NAV_WALK)points[1]=points[3];
    for(size_t i=0;i<world_gate_count;++i) {
        size_t index=world_gates[i];if(!world_type_collides(world_solids[index].poly_type,query))continue;
        const Hull *hull=&hulls[index];
        for(unsigned part=0;part<(link->mode==NAV_WALK ? 1u : 3u);++part) {
            Vec3 start=points[part],end=points[part+1];float enter=0,leave=1;
            for(unsigned face=0;face<world_solids[index].face_count;++face) {
                Plane plane=hull->planes[face];float limit=plane.distance+3*fabsf(plane.normal.x)+7*fabsf(plane.normal.y)+3*fabsf(plane.normal.z);
                float a=dot(plane.normal,start)-limit,b=dot(plane.normal,end)-limit;
                if(a>0 && b>0){leave=-1;break;}
                if(a==b)continue;
                float fraction=a/(a-b);
                if(a>b)enter=fmaxf(enter,fraction);else leave=fminf(leave,fraction);
            }
            if(enter<leave)return 0;
        }
    }
    return 1;
}

WorldHit world_trace_for(Vec3 start, Vec3 end, Vec3 extents, WorldQuery query)
{
    WorldHit hit = {1, {0, 0, 0}, -1},interior={0,{0,0,0},-1};
    Vec3 low=v3(fminf(start.x,end.x),fminf(start.y,end.y),fminf(start.z,end.z));
    Vec3 high=v3(fmaxf(start.x,end.x),fmaxf(start.y,end.y),fmaxf(start.z,end.z));
    for(size_t node=0;node<world_tree_count;) {
        const WorldBranch *branch=&world_tree[node];
        if(low.x>branch->max.x+extents.x || high.x<branch->min.x-extents.x ||
            low.y>branch->max.y+extents.y || high.y<branch->min.y-extents.y ||
            low.z>branch->max.z+extents.z || high.z<branch->min.z-extents.z) {node=branch->end;continue;}
        ++node;if(branch->solid==SIZE_MAX)continue;
        size_t i=branch->solid;const Hull *hull=&hulls[i];
        unsigned type=world_solids[i].poly_type;
        if(query.kind==WORLD_TRACE_LIGHT) {
            if(world_solids[i].texture!=WORLD_TERRAIN)continue;
        } else if(!world_type_collides(type,query))continue;

        float enter = -FLT_MAX;
        float leave = 1;
        float nearest = -FLT_MAX;
        int inside = 1;
        Vec3 normal = {0, 0, 0};
        Vec3 nearest_normal = {0, 0, 0};
        for (size_t j=0;j<world_solids[i].face_count+6u;++j) {
            Plane bounds[]={
                {{-1,0,0},-hull->min.x},{{1,0,0},hull->max.x},
                {{0,-1,0},-hull->min.y},{{0,1,0},hull->max.y},
                {{0,0,-1},-hull->min.z},{{0,0,1},hull->max.z}
            };
            Plane plane=j<world_solids[i].face_count ? hull->planes[j] : bounds[j-world_solids[i].face_count];
            float distance = plane.distance + fabsf(plane.normal.x) * extents.x +
                fabsf(plane.normal.y) * extents.y + fabsf(plane.normal.z) * extents.z;
            float from = dot(plane.normal, start) - distance;
            float to = dot(plane.normal, end) - distance;
            if (from >= 0)
                inside = 0;
            if (from > nearest) {
                nearest = from;
                nearest_normal = plane.normal;
            }
            if (from > 0 && to > 0) {
                leave = -1;
                break;
            }
            if (from < 0 && to <= 0)
                continue;
            if (from == to)
                continue;
            float fraction = from / (from - to);
            if (from > to && fraction > enter) {
                enter = fraction;
                normal = plane.normal;
            } else if (to > from && fraction < leave) {
                leave = fraction;
            }
        }
        if(inside && (interior.box<0 || (int)i<interior.box))interior=(WorldHit){0,nearest_normal,(int)i};
        if(enter>=0 && enter<leave && (enter<hit.fraction || (enter==hit.fraction && (int)i<hit.box)))
            hit=(WorldHit){enter,normal,(int)i};
    }
    return interior.box>=0 ? interior : hit;
}

WorldHit world_trace(Vec3 start, Vec3 end, Vec3 extents)
{
    return world_trace_for(start,end,extents,(WorldQuery){WORLD_TRACE_ENVIRONMENT,0,WORLD_NO_FLAG});
}

unsigned world_contact_type(const Actor *actor)
{
    float half=actor_height(actor->pose)*.5f;
    Vec3 center=add(actor->position,v3(0,half,0));
    WorldHit hit=world_trace_for(center,sub(center,v3(0,.1f,0)),v3(3,half,3),
        (WorldQuery){WORLD_TRACE_ACTOR,actor->team,actor->carried_flag!=FLAG_NONE ? WORLD_HAS_FLAG : WORLD_NO_FLAG});
    return hit.box<0 ? 0 : world_solids[hit.box].poly_type;
}

int world_pose_clear_for(Vec3 feet, Pose pose, WorldQuery query)
{
    float half_height = actor_height(pose) * 0.5f;
    Vec3 center = add(feet, v3(0, half_height, 0));
    return world_trace_for(center, center, v3(3, half_height, 3),query).box < 0;
}

int world_pose_clear(Vec3 feet, Pose pose)
{
    return world_pose_clear_for(feet,pose,(WorldQuery){WORLD_TRACE_ACTOR,0,WORLD_NO_FLAG});
}

Contact world_move(Actor *actor)
{
    WorldQuery query={WORLD_TRACE_ACTOR,actor->team,actor->carried_flag!=FLAG_NONE ? WORLD_HAS_FLAG : WORLD_NO_FLAG};
    Vec3 offset = v3(0, actor_height(actor->pose) * 0.5f, 0);
    Vec3 extents = v3(3, offset.y, 3);
    Vec3 position = add(actor->previous, offset);
    Vec3 remaining = sub(actor->position, actor->previous);
    Vec3 planes[2];
    int plane_count = 0;
    Contact contact = AIRBORNE;
    while (dot(remaining, remaining) > 0) {
        WorldHit hit = world_trace_for(position, add(position, remaining), extents,query);
        position = add(position, scale(remaining, hit.fraction));
        if (hit.box < 0)
            break;
        position = add(position, scale(hit.normal, 0.001f));
        if (hit.normal.y > 0.5f && world_solids[hit.box].poly_type!=18)
            contact = GROUNDED;
        remaining = scale(remaining, 1 - hit.fraction);
        Vec3 clipped = sub(remaining, scale(hit.normal, dot(remaining, hit.normal)));
        Vec3 velocity = sub(actor->velocity, scale(hit.normal, dot(actor->velocity, hit.normal)));
        for (int i = 0; i < plane_count; ++i) {
            if (dot(clipped, planes[i]) >= -0.00001f)
                continue;
            Vec3 crease = v3(planes[i].y * hit.normal.z - planes[i].z * hit.normal.y,
                planes[i].z * hit.normal.x - planes[i].x * hit.normal.z,
                planes[i].x * hit.normal.y - planes[i].y * hit.normal.x);
            float crease_length = length(crease);
            if (crease_length < 0.00001f) {
                clipped = v3(0, 0, 0);
                velocity = v3(0, 0, 0);
                break;
            }
            crease = scale(crease, 1 / crease_length);
            clipped = scale(crease, dot(remaining, crease));
            velocity = scale(crease, dot(actor->velocity, crease));
            for (int j = 0; j < plane_count; ++j) {
                if (dot(clipped, planes[j]) < -0.00001f) {
                    clipped = v3(0, 0, 0);
                    velocity = v3(0, 0, 0);
                    break;
                }
            }
            break;
        }
        if (world_solids[hit.box].poly_type==18) {
            float bounce=world_solids[hit.box].bounciness;
            clipped=add(remaining,scale(hit.normal,length(remaining)*bounce));
            velocity=add(actor->velocity,scale(hit.normal,length(actor->velocity)*bounce));
        }
        actor->velocity = velocity;
        remaining = clipped;
        for (int i = 0; i < plane_count;) {
            if (dot(clipped, planes[i]) > 0.00001f)
                planes[i] = planes[--plane_count];
            else
                ++i;
        }
        int independent = 1;
        for (int i = 0; i < plane_count; ++i)
            if (fabsf(dot(hit.normal, planes[i])) > 0.99999f)
                independent = 0;
        if (independent && plane_count < 2) {
            planes[plane_count++] = hit.normal;
        } else if (independent) {
            Vec3 crease = v3(planes[0].y * planes[1].z - planes[0].z * planes[1].y,
                planes[0].z * planes[1].x - planes[0].x * planes[1].z,
                planes[0].x * planes[1].y - planes[0].y * planes[1].x);
            if (fabsf(dot(crease, hit.normal)) > 0.00001f) {
                remaining = v3(0, 0, 0);
                actor->velocity = v3(0, 0, 0);
            }
        }
    }
    WorldHit support = world_trace_for(position, sub(position, v3(0, 0.05f, 0)), extents,query);
    if (support.box >= 0 && support.normal.y > 0.5f && actor->velocity.y <= 0)
        contact = GROUNDED;
    actor->position = sub(position, offset);
    actor->contact = contact;
    return contact;
}
