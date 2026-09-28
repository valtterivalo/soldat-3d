#include "gostek.h"
#include "raylib.h"
#include "pose.h"
#include "ragdoll.h"
#include "generated_rules.h"
#include "raymath.h"
#include "rlgl.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Mesh mesh;
    Texture2D texture;
    BoundingBox bounds;
    size_t capacity;
} MeshPart;
typedef struct InflatedMesh {
    float thickness;
    MeshPart part;
    struct InflatedMesh *next;
} InflatedMesh;
typedef struct {
    Texture2D texture;
    int columns, vertical;
    float *top, *bottom;
    InflatedMesh *meshes;
} InflatedArt;
typedef struct {
    Texture2D side;
    Rectangle front, back;
    Color color;
    Color atlas_color;
} SurfaceArt;
typedef struct { float t, width, depth; } Ring;
typedef struct { Vector3 position; Vector2 uv; } Vertex;
static Texture2D turnaround, body_side[10];
static InflatedArt guns[WEAPON_COUNT], magazines[WEAPON_COUNT], grenade, bow_string, bow_arrow;
enum { PART_SIDE, PART_ATLAS, PART_CAP, PART_CROWN, PART_COUNT };
typedef struct LoftMesh {
    const Ring *rings;
    SurfaceArt art;
    MeshPart parts[PART_COUNT];
    struct LoftMesh *next;
} LoftMesh;
typedef struct { const MeshPart *part; Matrix transform; Color color; } MeshInstance;
static LoftMesh *lofts;
static MeshInstance *instances;
static size_t instance_count,instance_capacity;
static BoundingBox actor_bounds;
typedef struct { float transform[16],normal[9]; } InstanceData;
typedef struct {
    const MeshPart *part;
    Color color;
    float opacity,visibility;
    InstanceData *data;
    size_t count,capacity;
} InstanceGroup;
typedef enum { DRAW_IMMEDIATE, DRAW_COLLECTED, DRAW_LOADING } DrawPhase;
static DrawPhase draw_phase;
static InstanceGroup *groups;
static size_t group_count,group_capacity,merge_start,buffer_capacity;
static InstanceData *buffer_data;
static unsigned instance_buffer;
static int transform_location,normal_location;
static Shader opaque_shader;
static int alpha_location,visibility_location;
static uint64_t fire_until[ACTOR_COUNT];
static float image_scale;
static const char *body_files[] = {"klata","biodro","morda","udo","noga","ramie","reka","dlon","stopa","lecistopa"};
static const char *clip_files[WEAPON_COUNT] = {
    [EAGLE]="deserteagle-clip",[MP5]="mp5-clip",[AK74]="ak74-clip",[STEYRAUG]="steyraug-clip",
    [M79]="m79-clip",[BARRETT]="barretm82-clip",[M249]="m249-clip",[MINIGUN]="minigun-clip",[COLT]="colt1911-clip"
};
static Texture2D load_art(const char *directory, const char *name) {
    char path[sizeof(SOLDAT_ASSET_DIR) + 128];
    int written = snprintf(path, sizeof(path), SOLDAT_ASSET_DIR "/%s/%s.png", directory, name);
    if (written < 0 || (size_t)written >= sizeof(path)) abort();
    Texture2D texture = LoadTexture(path);
    if (texture.id == 0) { fprintf(stderr,"Cannot load Soldat texture %s\n",path); exit(EXIT_FAILURE); }
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    return texture;
}

static InflatedArt inflate_art(const char *name) {
    char path[sizeof(SOLDAT_ASSET_DIR) + 128];
    int written = snprintf(path,sizeof(path),SOLDAT_ASSET_DIR "/weapons-gfx/%s.png",name);
    if (written < 0 || (size_t)written >= sizeof(path)) abort();
    Image image = LoadImage(path);
    if (!image.data) { fprintf(stderr,"Cannot load Soldat geometry source %s\n",path); exit(EXIT_FAILURE); }
    Color *pixels = LoadImageColors(image);
    int vertical=strcmp(name,"bow")==0 || strcmp(name,"bow-s")==0;
    InflatedArt art = {.texture=LoadTextureFromImage(image),.columns=vertical?image.height:image.width,.vertical=vertical};
    int rows=vertical?image.width:image.height;
    art.top = malloc(sizeof(float) * (size_t)art.columns * 2);
    if (!art.top) abort();
    art.bottom = art.top + art.columns;
    for (int x=0;x<art.columns;x++) {
        art.top[x]=(float)rows;
        art.bottom[x]=0;
        for (int y=0;y<rows;y++) if (pixels[(vertical?x:y)*image.width+(vertical?y:x)].a>32) {
            art.top[x]=fminf(art.top[x],(float)y);
            art.bottom[x]=fmaxf(art.bottom[x],(float)y+1);
        }
    }
    SetTextureFilter(art.texture,TEXTURE_FILTER_POINT);
    UnloadImageColors(pixels);
    UnloadImage(image);
    return art;
}

void gostek_init(void) {
    image_scale=SRC_ART_SCALE;
    turnaround=load_art("generated","soldier-turnaround");
    for (int i=0;i<10;i++) body_side[i]=load_art("gostek-gfx",body_files[i]);
    for (int i=0;i<WEAPON_COUNT;i++) {
        if (weapon_visuals[i].file) guns[i]=inflate_art(weapon_visuals[i].file);
        if (clip_files[i]) magazines[i]=inflate_art(clip_files[i]);
    }
    grenade=inflate_art("frag-grenade");
    bow_string=inflate_art("bow-s");
    bow_arrow=inflate_art("bow-a");
    opaque_shader=LoadShaderFromMemory(
        "#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec3 vertexNormal;"
        "in mat4 instanceTransform; in mat3 instanceNormal;"
        "uniform mat4 mvp; uniform vec4 colDiffuse;"
        "out vec2 fragTexCoord; out vec4 fragColor;"
        "void main(){vec3 normal=normalize(instanceNormal*vertexNormal);"
        "float light=0.68+0.32*max(0.0,dot(normal,vec3(-0.36,0.8,0.48)));"
        "fragColor=vec4(floor(colDiffuse.rgb*255.0*light)/255.0,1.0);"
        "fragTexCoord=vertexTexCoord;gl_Position=mvp*instanceTransform*vec4(vertexPosition,1.0);}",
        "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; uniform sampler2D texture0; uniform float actorAlpha; uniform float cameraVisibility; out vec4 finalColor;"
        "void main(){int pattern[16]=int[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);"
        "ivec2 cell=ivec2(mod(floor(gl_FragCoord.xy),4.0));"
        "if(cameraVisibility<(float(pattern[cell.x+cell.y*4])+0.5)/16.0)discard;"
        "vec4 p=texture(texture0,fragTexCoord);"
        "finalColor=vec4(mix(vec3(0.72),p.rgb,p.a)*fragColor.rgb,actorAlpha);}");
    if (opaque_shader.id==0 || opaque_shader.id==rlGetShaderIdDefault()) {fprintf(stderr,"Cannot compile the Soldat mesh shader\n");abort();}
    alpha_location=GetShaderLocation(opaque_shader,"actorAlpha");
    visibility_location=GetShaderLocation(opaque_shader,"cameraVisibility");
    transform_location=GetShaderLocationAttrib(opaque_shader,"instanceTransform");
    normal_location=GetShaderLocationAttrib(opaque_shader,"instanceNormal");
    if (transform_location<0 || normal_location<0) abort();
    draw_phase=DRAW_LOADING;
    Actor preview={.life=ALIVE,.contact=GROUNDED,.spawn_protection_ticks=-1,.fuel=1,.grenades=1};
    gostek_fire(0,1);
    for (int i=0;i<WEAPON_COUNT;i++) if (weapon_visuals[i].file) {
        preview.slots[0]=preview.slots[1]=(WeaponState){.id=(WeaponId)i,.ammo=1};
        gostek_draw(&preview,0,1,1,0,1,NULL);
        gostek_draw(&preview,0,1,1,1,1,NULL);
        gostek_draw_weapon((WeaponId)i,(Vec3){0},0);
    }
    fire_until[0]=0;
    instance_count=0;
    draw_phase=DRAW_IMMEDIATE;
}

void gostek_fire(int id,uint64_t tick) { fire_until[id]=tick+2; }

static void triangle(MeshPart *part,Vertex a,Vertex b,Vertex c,Vector3 outward) {
    Vector3 normal=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b.position,a.position),Vector3Subtract(c.position,a.position)));
    if (Vector3DotProduct(normal,outward)<0) {
        Vertex swap=b;b=c;c=swap;
        normal=Vector3Negate(normal);
    }
    size_t count=(size_t)part->mesh.vertexCount;
    if (count+3>part->capacity) {
        part->capacity=part->capacity?part->capacity*2:96;
        part->mesh.vertices=realloc(part->mesh.vertices,part->capacity*3*sizeof(float));
        part->mesh.normals=realloc(part->mesh.normals,part->capacity*3*sizeof(float));
        part->mesh.texcoords=realloc(part->mesh.texcoords,part->capacity*2*sizeof(float));
        assert(part->mesh.vertices && part->mesh.normals && part->mesh.texcoords);
    }
    Vertex vertices[3]={a,c,b};
    for (int i=0;i<3;i++) {
        Vector3 position=vertices[i].position;
        size_t index=count+(size_t)i;
        part->mesh.vertices[index*3]=position.x;
        part->mesh.vertices[index*3+1]=position.y;
        part->mesh.vertices[index*3+2]=position.z;
        part->mesh.normals[index*3]=normal.x;
        part->mesh.normals[index*3+1]=normal.y;
        part->mesh.normals[index*3+2]=normal.z;
        part->mesh.texcoords[index*2]=vertices[i].uv.x;
        part->mesh.texcoords[index*2+1]=vertices[i].uv.y;
        if (index==0) part->bounds=(BoundingBox){position,position};
        else {
            part->bounds.min=Vector3Min(part->bounds.min,position);
            part->bounds.max=Vector3Max(part->bounds.max,position);
        }
    }
    part->mesh.vertexCount+=3;
    part->mesh.triangleCount++;
}

static Matrix basis(Vector3 origin,Vector3 x,Vector3 y,Vector3 z) {
    return (Matrix){x.x,y.x,z.x,origin.x,x.y,y.y,z.y,origin.y,x.z,y.z,z.z,origin.z,0,0,0,1};
}

static void queue_mesh(const MeshPart *part,Matrix transform,Color color) {
    if (instance_count==instance_capacity) {
        instance_capacity=instance_capacity?instance_capacity*2:64;
        instances=realloc(instances,instance_capacity*sizeof(*instances));
        assert(instances);
    }
    instances[instance_count++]=(MeshInstance){part,transform,color};
    Vector3 center=Vector3Transform(Vector3Scale(Vector3Add(part->bounds.min,part->bounds.max),.5f),transform);
    Vector3 radius=Vector3Scale(Vector3Subtract(part->bounds.max,part->bounds.min),.5f);
    Vector3 extent={fabsf(transform.m0)*radius.x+fabsf(transform.m4)*radius.y+fabsf(transform.m8)*radius.z,
        fabsf(transform.m1)*radius.x+fabsf(transform.m5)*radius.y+fabsf(transform.m9)*radius.z,
        fabsf(transform.m2)*radius.x+fabsf(transform.m6)*radius.y+fabsf(transform.m10)*radius.z};
    BoundingBox bounds={Vector3Subtract(center,extent),Vector3Add(center,extent)};
    if (instance_count==1) actor_bounds=bounds;
    else {
        actor_bounds.min=Vector3Min(actor_bounds.min,bounds.min);
        actor_bounds.max=Vector3Max(actor_bounds.max,bounds.max);
    }
}

static void flush_groups(void) {
    if (!group_count) return;
    size_t count=0;
    for (size_t i=0;i<group_count;i++) count+=groups[i].count;
    if (count>buffer_capacity) {
        if (instance_buffer) rlUnloadVertexBuffer(instance_buffer);
        buffer_capacity=count*2;
        buffer_data=realloc(buffer_data,buffer_capacity*sizeof(*buffer_data));
        assert(buffer_data);
        instance_buffer=rlLoadVertexBuffer(NULL,(int)(buffer_capacity*sizeof(InstanceData)),true);
        if (!instance_buffer) abort();
    }
    size_t offset=0;
    for (size_t i=0;i<group_count;i++) {
        memcpy(buffer_data+offset,groups[i].data,groups[i].count*sizeof(InstanceData));
        offset+=groups[i].count;
    }
    rlUpdateVertexBuffer(instance_buffer,buffer_data,(int)(count*sizeof(InstanceData)),0);
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
    rlEnableShader(opaque_shader.id);
    rlSetUniformMatrix(opaque_shader.locs[SHADER_LOC_MATRIX_MVP],MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection()));
    int texture_unit=0;
    rlSetUniform(opaque_shader.locs[SHADER_LOC_MAP_DIFFUSE],&texture_unit,SHADER_UNIFORM_INT,1);
    rlActiveTextureSlot(0);
    offset=0;
    for (size_t i=0;i<group_count;i++) {
        InstanceGroup *group=&groups[i];
        int byte_offset=(int)(offset*sizeof(InstanceData));
        if (!rlEnableVertexArray(group->part->mesh.vaoId)) abort();
        rlEnableVertexBuffer(instance_buffer);
        for (int column=0;column<4;column++) {
            unsigned location=(unsigned)(transform_location+column);
            rlSetVertexAttribute(location,4,RL_FLOAT,false,sizeof(InstanceData),byte_offset+column*4*sizeof(float));
            rlEnableVertexAttribute(location);rlSetVertexAttributeDivisor(location,1);
        }
        for (int column=0;column<3;column++) {
            unsigned location=(unsigned)(normal_location+column);
            rlSetVertexAttribute(location,3,RL_FLOAT,false,sizeof(InstanceData),byte_offset+(16+column*3)*sizeof(float));
            rlEnableVertexAttribute(location);rlSetVertexAttributeDivisor(location,1);
        }
        float color[4]={group->color.r/255.0f,group->color.g/255.0f,group->color.b/255.0f,1};
        rlSetUniform(opaque_shader.locs[SHADER_LOC_COLOR_DIFFUSE],color,SHADER_UNIFORM_VEC4,1);
        rlSetUniform(alpha_location,&group->opacity,SHADER_UNIFORM_FLOAT,1);
        rlSetUniform(visibility_location,&group->visibility,SHADER_UNIFORM_FLOAT,1);
        rlEnableTexture(group->part->texture.id);
        rlDrawVertexArrayInstanced(0,group->part->mesh.vertexCount,(int)group->count);
        offset+=group->count;
    }
    rlDisableVertexArray();rlDisableVertexBuffer();rlDisableTexture();rlDisableShader();
    group_count=merge_start=0;
}

void gostek_begin(void) {
    assert(draw_phase==DRAW_IMMEDIATE);
    draw_phase=DRAW_COLLECTED;
}

void gostek_end(void) {
    assert(draw_phase==DRAW_COLLECTED);
    flush_groups();
    draw_phase=DRAW_IMMEDIATE;
}

static void draw_instances(float opacity,float visibility) {
    if (draw_phase==DRAW_LOADING) return;
    Matrix clip=MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection());
    unsigned outside=63;
    for (int i=0;i<8;i++) {
        Vector3 p={i&1?actor_bounds.max.x:actor_bounds.min.x,
            i&2?actor_bounds.max.y:actor_bounds.min.y,i&4?actor_bounds.max.z:actor_bounds.min.z};
        Vector4 q={clip.m0*p.x+clip.m4*p.y+clip.m8*p.z+clip.m12,
            clip.m1*p.x+clip.m5*p.y+clip.m9*p.z+clip.m13,
            clip.m2*p.x+clip.m6*p.y+clip.m10*p.z+clip.m14,
            clip.m3*p.x+clip.m7*p.y+clip.m11*p.z+clip.m15};
        outside&=(q.x < -q.w?1u:0)|(q.x > q.w?2u:0)|(q.y < -q.w?4u:0)|
            (q.y > q.w?8u:0)|(q.z < -q.w?16u:0)|(q.z > q.w?32u:0);
    }
    if (outside) return;
    if (opacity<1) merge_start=group_count;
    for (size_t i=0;i<instance_count;i++) {
        const MeshInstance *instance=&instances[i];
        size_t index=merge_start;
        if (opacity==1) while (index<group_count && (groups[index].part!=instance->part ||
            memcmp(&groups[index].color,&instance->color,sizeof(Color)) ||
            groups[index].opacity!=opacity || groups[index].visibility!=visibility)) index++;
        else index=group_count;
        if (index==group_count) {
            if (group_count==group_capacity) {
                size_t old_capacity=group_capacity;
                group_capacity=group_capacity?group_capacity*2:64;
                groups=realloc(groups,group_capacity*sizeof(*groups));
                assert(groups);
                memset(groups+old_capacity,0,(group_capacity-old_capacity)*sizeof(*groups));
            }
            InstanceGroup *group=&groups[group_count++];
            group->part=instance->part;group->color=instance->color;
            group->opacity=opacity;group->visibility=visibility;group->count=0;
        }
        InstanceGroup *group=&groups[index];
        if (group->count==group->capacity) {
            group->capacity=group->capacity?group->capacity*2:16;
            group->data=realloc(group->data,group->capacity*sizeof(*group->data));
            assert(group->data);
        }
        InstanceData *data=&group->data[group->count++];
        memcpy(data->transform,MatrixToFloat(instance->transform),sizeof(data->transform));
        Matrix normal=MatrixTranspose(MatrixInvert(instance->transform));
        const float columns[9]={normal.m0,normal.m1,normal.m2,normal.m4,normal.m5,normal.m6,normal.m8,normal.m9,normal.m10};
        memcpy(data->normal,columns,sizeof(columns));
    }
    if (opacity<1) merge_start=group_count;
    if (draw_phase==DRAW_IMMEDIATE) flush_groups();
}

static void loft(Vector3 a,Vector3 b,Vector3 lateral,float width,float depth,const Ring *rings,int count,SurfaceArt art) {
    LoftMesh *cached=lofts;
    while (cached && (cached->rings!=rings || cached->art.side.id!=art.side.id ||
        memcmp(&cached->art.front,&art.front,sizeof(Rectangle)) || memcmp(&cached->art.back,&art.back,sizeof(Rectangle)))) cached=cached->next;
    if (!cached) {
        cached=calloc(1,sizeof(*cached));
        assert(cached);
        cached->rings=rings;cached->art=art;cached->next=lofts;lofts=cached;
        cached->parts[PART_SIDE].texture=art.side;
        cached->parts[PART_ATLAS].texture=turnaround;
        cached->parts[PART_CAP].texture=cached->parts[PART_CROWN].texture=(Texture2D){.id=rlGetTextureIdDefault()};
        enum { SIDES=12 };
        for (int s=0;s<SIDES;s++) {
            float angle0=2*PI*(float)s/SIDES,angle1=2*PI*(float)(s+1)/SIDES;
            float side=sinf((angle0+angle1)*.5f);
            int head_profile=art.side.id==body_side[2].id && fabsf(side)<=.5f;
            int projection=art.front.width>0 && (fabsf(side)>.5f || head_profile);
            Rectangle crop=side>0 ? art.front:art.back;
            if (head_profile) crop=cosf((angle0+angle1)*.5f)>0 ? (Rectangle){657,62,143,151}:(Rectangle){1678,62,143,151};
            MeshPart *part=&cached->parts[projection?PART_ATLAS:PART_SIDE];
            for (int r=0;r<count-1;r++) {
                Vertex v[4];
                for (int i=0;i<4;i++) {
                    Ring ring=rings[r+(i==1 || i==2)];
                    float angle=(i<2 ? angle0:angle1);
                    float x=cosf(angle)*ring.width,y=sinf(angle)*ring.depth;
                    v[i].position=(Vector3){x,ring.t,y};
                    if (projection) {
                        float u=side>0 ? .5f-.5f*x:.5f+.5f*x;
                        if (head_profile) u=cosf((angle0+angle1)*.5f)>0 ? .5f+.5f*y:.5f-.5f*y;
                        v[i].uv=(Vector2){(crop.x+u*crop.width)/turnaround.width,(crop.y+(1-ring.t)*crop.height)/turnaround.height};
                        if (art.side.id==body_side[2].id && ring.t>=.9f)
                            v[i].uv=(Vector2){255.0f/turnaround.width,80.0f/turnaround.height};
                    } else {
                        v[i].uv=(Vector2){ring.t,.5f+.45f*y};
                        if (art.side.id==body_side[5].id || art.side.id==body_side[6].id)
                            v[i].uv=(Vector2){.2f+.6f*ring.t,.5f+.3f*y};
                    }
                }
                Vector3 outside=(Vector3){cosf((angle0+angle1)*.5f),0,side};
                if (rings[r+1].width>0) triangle(part,v[0],v[1],v[2],outside);
                if (rings[r].width>0) triangle(part,v[0],v[2],v[3],outside);
            }
            int cloth_cap=art.side.id==body_side[5].id || art.side.id==body_side[6].id;
            int head_cap=art.side.id==body_side[2].id;
            for (int end=0;end<2;end++) {
                Ring ring=rings[end ? count-1:0];
                if (ring.width==0) continue;
                Vector3 center={0,ring.t,0};
                Vertex cap={center,{.5f,.5f}},edges[2];
                for (int i=0;i<2;i++) {
                    float angle=i ? angle1:angle0;
                    edges[i]=(Vertex){(Vector3){cosf(angle)*ring.width,ring.t,sinf(angle)*ring.depth},{.5f,.5f}};
                }
                if (projection) cap.uv=edges[0].uv=edges[1].uv=(Vector2){(crop.x+.5f*crop.width)/turnaround.width,(crop.y+(end?8:crop.height-3))/turnaround.height};
                MeshPart *cap_part=cloth_cap||head_cap?&cached->parts[head_cap&&end?PART_CROWN:PART_CAP]:part;
                triangle(cap_part,cap,edges[0],edges[1],(Vector3){0,end?1:-1,0});
            }
        }
        for (int i=0;i<PART_COUNT;i++) if(cached->parts[i].mesh.vertexCount) UploadMesh(&cached->parts[i].mesh,false);
    }
    Vector3 axis=Vector3Subtract(b,a),along=Vector3Normalize(axis);
    lateral=Vector3Normalize(Vector3Subtract(lateral,Vector3Scale(along,Vector3DotProduct(lateral,along))));
    Vector3 front=Vector3CrossProduct(along,lateral);
    Matrix transform=basis(a,Vector3Scale(lateral,width),axis,Vector3Scale(front,depth));
    const Color colors[PART_COUNT]={art.color,art.atlas_color,art.color,{73,44,22,255}};
    for (int i=0;i<PART_COUNT;i++) if (cached->parts[i].mesh.vertexCount) queue_mesh(&cached->parts[i],transform,colors[i]);
}

static void inflate_draw(InflatedArt *art,Vector3 origin,Vector3 along,Vector3 lateral,Vector2 pivot,float thickness,Color tint) {
    InflatedMesh *cached=art->meshes;
    while (cached && cached->thickness!=thickness) cached=cached->next;
    if (!cached) {
        cached=calloc(1,sizeof(*cached));
        assert(cached);
        cached->thickness=thickness;cached->part.texture=art->texture;
        cached->next=art->meshes;art->meshes=cached;
        enum { SIDES=8 };
        for (int x=0;x<art->columns;x++) {
            if (art->bottom[x]<=art->top[x]) continue;
            float bounds[2][2]={{art->top[x],art->bottom[x]},{art->top[x],art->bottom[x]}};
            if (x+1<art->columns && art->bottom[x+1]>art->top[x+1]) {
                bounds[1][0]=art->top[x+1];bounds[1][1]=art->bottom[x+1];
            }
            for (int s=0;s<SIDES;s++) {
                Vertex v[4];
                for (int i=0;i<4;i++) {
                    int end=i==1 || i==2;
                    float angle=2*PI*(float)(s+(i>=2))/SIDES;
                    float top=bounds[end][0],bottom=bounds[end][1];
                    float py=(top+bottom)*.5f+sinf(angle)*(bottom-top)*.5f;
                    float halfdepth=fminf(thickness,(bottom-top)*.55f/image_scale);
                    float image_x=art->vertical?py:(float)(x+end);
                    float image_y=art->vertical?(float)(x+end):py;
                    v[i].position=(Vector3){image_x/image_scale,image_y/image_scale,cosf(angle)*halfdepth};
                    v[i].uv=(Vector2){image_x/art->texture.width,image_y/art->texture.height};
                }
                Vector3 outside=(Vector3){art->vertical?sinf(2*PI*((float)s+.5f)/SIDES):0,
                    art->vertical?0:sinf(2*PI*((float)s+.5f)/SIDES),cosf(2*PI*((float)s+.5f)/SIDES)};
                triangle(&cached->part,v[0],v[1],v[2],outside);
                triangle(&cached->part,v[0],v[2],v[3],outside);
                for (int end=0;end<2;end++) {
                    if ((!end && x>0 && art->bottom[x-1]>art->top[x-1]) ||
                        (end && x+1<art->columns && art->bottom[x+1]>art->top[x+1])) continue;
                    float middle=(bounds[end][0]+bounds[end][1])*.5f;
                    float image_x=art->vertical?middle:(float)(x+end);
                    float image_y=art->vertical?(float)(x+end):middle;
                    Vector3 center=(Vector3){image_x/image_scale,image_y/image_scale,0};
                    Vertex cap={center,v[end].uv};
                    triangle(&cached->part,cap,v[end?1:0],v[end?2:3],art->vertical?(Vector3){0,end?1:-1,0}:(Vector3){end?1:-1,0,0});
                }
            }
        }
        UploadMesh(&cached->part.mesh,false);
    }
    Vector3 down=Vector3Normalize(Vector3CrossProduct(along,lateral));
    Vector3 offset=Vector3Add(Vector3Scale(along,-pivot.x*art->texture.width/image_scale),Vector3Scale(down,-pivot.y*art->texture.height/image_scale));
    queue_mesh(&cached->part,basis(Vector3Add(origin,offset),along,down,lateral),tint);
}

void gostek_draw_shadows(const Game *scene,float alpha) {
    rlDrawRenderBatchActive();
    rlDisableDepthMask();
    for (int i=0;i<ACTOR_COUNT;++i) {
        const Actor *actor=&scene->actors[i];
        if (actor->life==INACTIVE) continue;
        Vec3 feet=add(actor->previous,scale(sub(actor->position,actor->previous),alpha));
        Vec3 start=add(feet,v3(0,1,0)),down=v3(0,-100,0);
        WorldHit floor=world_trace(start,add(start,down),v3(0,0,0));
        if (floor.box>=0 && floor.normal.y>.5f) {
            Vec3 center=add(add(start,scale(down,floor.fraction)),scale(floor.normal,.025f));
            Vector3 normal={floor.normal.x,floor.normal.y,floor.normal.z};
            Vector3 right=Vector3Normalize(Vector3CrossProduct(normal,(Vector3){0,0,1}));
            Vector3 forward=Vector3CrossProduct(right,normal);
            Color shadow=Fade(BLACK,.25f*(1-floor.fraction));
            for (int part=0;part<20;++part) {
                float a=(float)part*2*PI/20,b=(float)(part+1)*2*PI/20;
                Vector3 origin={center.x,center.y,center.z};
                Vector3 p=Vector3Add(origin,Vector3Add(Vector3Scale(right,cosf(a)*4.2f),Vector3Scale(forward,sinf(a)*3)));
                Vector3 q=Vector3Add(origin,Vector3Add(Vector3Scale(right,cosf(b)*4.2f),Vector3Scale(forward,sinf(b)*3)));
                DrawTriangle3D(origin,p,q,shadow);
            }
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void gostek_draw(const Actor *actor,int id,uint64_t tick,float alpha,int jetting,float visibility,const Vec3 pose_points[21]) {
    if(visibility==0)return;
    if (actor->team==TEAM_SPECTATOR || actor->spawn_protection_ticks>SRC_DEFAULT_CEASEFIRE_TIME-5) return;
    if (actor->life==INACTIVE) return;
    Actor posed=*actor;
    posed.position=add(actor->previous,scale(sub(actor->position,actor->previous),alpha));
    Vec3 points[21];
    if (pose_points) memcpy(points,pose_points,sizeof(points));
    else {
        actor_pose(&posed,points);
        if (actor->life==DEAD)
            for (int i=1;i<=20;i++) points[i]=add(actor->ragdoll.previous[i],scale(sub(points[i],actor->ragdoll.previous[i]),alpha));
    }
    Vec3 heading=direction(actor->yaw,0);
    Vector3 forward={heading.x,0,heading.z},right={-heading.z,0,heading.x};
    Vector3 origin={posed.position.x,posed.position.y,posed.position.z};
    Vector3 p[21];
    for (int i=0;i<=20;i++) p[i]=(Vector3){points[i].x,points[i].y,points[i].z};
    Vector3 hip=Vector3Scale(Vector3Add(p[5],p[6]),.5f);
    Vector3 shoulder=Vector3Scale(Vector3Add(p[10],p[11]),.5f);
    Vector3 spine=Vector3Normalize(Vector3Subtract(shoulder,hip));
    if (actor->life==DEAD) {
        right=Vector3Normalize(Vector3Subtract(p[10],p[11]));
        forward=Vector3Normalize(Vector3CrossProduct(spine,right));
    }
    Vec3 aim_value=direction(actor->yaw,actor->pitch);
    Vector3 aim={aim_value.x,aim_value.y,aim_value.z};
    Vector3 hand=p[16];
    const WeaponState *weapon=&actor->slots[actor->active_slot];
    const WeaponDef *definition=&weapons[weapon->id];
    Vector2 gun_pivot={weapon_visuals[weapon->id].cx,weapon_visuals[weapon->id].cy};
    static const Color shirts[]={{245,245,245,255},{190,53,40,255},{75,140,63,255},{210,165,55,255},{75,120,205,255},{160,70,165,255},{225,140,60,255}};
    Color shirt_color=shirts[(size_t)id%(sizeof(shirts)/sizeof(shirts[0]))];
    const Color team_shirts[]={{0},{210,15,5,255},{21,31,217,255},{210,210,5,255},{5,210,5,255}};
    if (actor->team>=TEAM_ALPHA && actor->team<=TEAM_DELTA) shirt_color=team_shirts[actor->team];
    Color skin={230,180,120,255},pants={45,65,225,255};
    static const Ring torso[]={{0,.8f,.8f},{.18f,.85f,.95f},{.62f,1,1},{.85f,1,.84f},{1,.4f,.55f}};
    static const Ring limb[]={{0,.8f,.75f},{.13f,1,1},{.55f,.92f,.88f},{.88f,.82f,.77f},{1,.65f,.65f}};
    static const Ring sleeve_rings[]={{0,0,0},{.08f,.62f,.62f},{.2f,1,1},{.55f,.95f,.93f},{.82f,.83f,.8f},{.94f,.6f,.58f},{1,0,0}};
    static const Ring forearm_rings[]={{0,0,0},{.12f,.86f,.86f},{.28f,1,1},{.65f,.9f,.9f},{1,.72f,.72f}};
    static const Ring shin[]={{0,.83f,.8f},{.16f,1,1},{.65f,.96f,.9f},{1,.7f,.7f}};
    static const Ring rounded[]={{0,.5f,.5f},{.18f,.95f,.95f},{.65f,1,1},{.9f,.83f,.83f},{1,.45f,.45f}};
    static const Ring belt[]={{0,1,1},{1,1,1}};
    SurfaceArt shirt={body_side[0],{176,210,160,196},{1169,210,155,196},shirt_color,shirt_color};
    SurfaceArt trouser={body_side[3],{168,515,54,113},{1153,515,52,113},pants,WHITE};
    SurfaceArt sleeve={body_side[5],{0},{0},shirt_color,shirt_color};
    SurfaceArt flesh={body_side[7],{0},{0},skin,WHITE};
    SurfaceArt boot={body_side[jetting && actor->fuel>0?9:8],{110,650,99,95},{1090,650,95,95},WHITE,WHITE};
    SurfaceArt dark={body_side[1],{0},{0},{47,49,47,255},WHITE};
    instance_count=0;
    float opacity=actor->spawn_protection_ticks>=0 ? fabsf(100+70*sinf((float)tick*SRC_ILUMINATESPEED))/255:1;
    if (actor->life==ALIVE && actor->bonus==BONUS_PREDATOR) opacity=5.0f/255;
    loft(hip,shoulder,right,2.85f,1.9f,torso,5,shirt);
    Vector3 belt_bottom=Vector3Add(hip,Vector3Scale(spine,-.6f));
    loft(belt_bottom,Vector3Add(hip,Vector3Scale(spine,.25f)),right,2.5f,1.8f,belt,2,dark);
    loft(Vector3Add(hip,Vector3Scale(spine,-1.5f)),belt_bottom,right,2.15f,1.55f,torso,5,trouser);
    for (int side=0;side<2;side++) {
        int h=side?6:5,k=side?3:4,f=side?2:1,s=side?11:10,e=side?14:13,w=side?15:16;
        if (!(actor->ragdoll.severed&(1u<<(side?1:3))))
            loft(p[k],p[h],right,1.5f,1.45f,limb,5,trouser);
        trouser.side=body_side[4];
        loft(p[f],p[k],right,1.4f,1.4f,shin,4,trouser);
        trouser.side=body_side[3];
        Vector3 ankle=p[f];
        Vector3 foot_up=actor->life==DEAD ? Vector3Normalize(Vector3Subtract(p[k],p[f])):(Vector3){0,1,0};
        Vector3 foot_forward=actor->life==DEAD ? Vector3Normalize(Vector3Subtract(p[side?18:17],ankle)):forward;
        Vector3 toe=Vector3Add(Vector3Add(ankle,Vector3Scale(foot_forward,1.05f)),Vector3Scale(foot_up,-.55f));
        if (actor->life==ALIVE) toe.y=fmaxf(origin.y+.3f,toe.y);
        loft(Vector3Add(toe,Vector3Scale(foot_up,-.4f)),Vector3Add(ankle,Vector3Scale(foot_up,1.7f)),right,1.3f,1.95f,shin,4,boot);
        SurfaceArt metal={body_side[8],{0},{0},{100,105,103,255},WHITE};
        Vector3 nozzle=Vector3Add(ankle,Vector3Scale(foot_forward,-1.2f));
        loft(Vector3Add(nozzle,Vector3Scale(foot_up,-.5f)),Vector3Add(nozzle,Vector3Scale(foot_up,.7f)),right,.55f,.65f,belt,2,metal);
        Vector3 upper_axis=Vector3Normalize(Vector3Subtract(p[e],p[s]));
        Vector3 lower_axis=Vector3Normalize(Vector3Subtract(p[w],p[e]));
        sleeve.side=body_side[5];
        if (!(actor->ragdoll.severed&(1u<<(side?22:20))))
            loft(Vector3Add(p[s],Vector3Scale(upper_axis,-.85f)),Vector3Add(p[e],Vector3Scale(upper_axis,.65f)),right,1.4f,1.3f,sleeve_rings,7,sleeve);
        sleeve.side=body_side[6];
        loft(Vector3Add(p[e],Vector3Scale(lower_axis,-ACTOR_FOREARM_OVERLAP)),p[w],right,ACTOR_FOREARM_RADIUS,1.16f,forearm_rings,5,sleeve);
        Vector3 hand_axis=actor->life==DEAD ? Vector3Normalize(Vector3Subtract(p[side?19:20],p[w])):aim;
        loft(Vector3Add(p[w],Vector3Scale(hand_axis,-ACTOR_HAND_BACK)),Vector3Add(p[w],Vector3Scale(hand_axis,ACTOR_HAND_FRONT)),right,ACTOR_HAND_RADIUS,.7f,rounded,5,flesh);
    }
    SurfaceArt head={body_side[2],{195,62,121,151},{1184,62,122,151},skin,WHITE};
    Vector3 head_axis=actor->life==DEAD ? Vector3Normalize(Vector3Subtract(p[12],p[9])):spine;
    Vector3 chin=Vector3Add(p[9],Vector3Scale(head_axis,-.4f));
    if (actor->life==DEAD) chin=Vector3Add(p[12],Vector3Scale(head_axis,-2.275f));
    Vector3 crown=Vector3Add(chin,Vector3Scale(head_axis,4.55f));
    SurfaceArt neck={body_side[7],{225,192,64,33},{1210,178,68,31},skin,WHITE};
    if (!(actor->ragdoll.severed&(1u<<19)))
        loft(Vector3Add(shoulder,Vector3Scale(spine,-.4f)),Vector3Add(chin,Vector3Scale(head_axis,.8f)),right,.9f,.95f,rounded,5,neck);
    static const Ring skull[]={{0,.52f,.63f},{.17f,.78f,.88f},{.4f,1,1},{.74f,.98f,1},{.9f,.8f,.87f},{.97f,.45f,.5f},{1,0,0}};
    loft(chin,crown,right,1.75f,1.65f,skull,7,head);
    Vector3 face=Vector3Add(Vector3Lerp(chin,crown,.44f),Vector3Scale(forward,1.52f));
    SurfaceArt nose={body_side[7],{242,136,24,35},{242,136,24,35},skin,WHITE};
    loft(Vector3Add(face,Vector3Scale(head_axis,-.5f)),Vector3Add(face,Vector3Scale(head_axis,.6f)),right,.4f,.55f,rounded,5,nose);
    for (int side=-1;side<=1;side+=2) {
        Vector3 ear=Vector3Add(Vector3Lerp(chin,crown,.43f),Vector3Scale(right,(float)side*1.73f));
        SurfaceArt ear_art={.side={.id=rlGetTextureIdDefault()},.color=skin};
        loft(Vector3Add(ear,Vector3Scale(head_axis,-.5f)),Vector3Add(ear,Vector3Scale(head_axis,.5f)),right,.25f,.4f,rounded,5,ear_art);
    }
    for (int n=0;n<actor->grenades;n++) {
        float offset=((float)n-((float)actor->grenades-1)*.5f)*1.4f;
        Vector3 position=Vector3Add(Vector3Add(hip,Vector3Scale(right,offset)),Vector3Scale(forward,1.75f));
        inflate_draw(&grenade,position,spine,right,(Vector2){.5f,.5f},.67f,WHITE);
    }
    WeaponId back=actor->slots[1-actor->active_slot].id;
    if (actor->life==ALIVE && weapon_visuals[back].file) {
        Vector3 back_direction=Vector3Normalize(Vector3Add(spine,Vector3Scale(right,.28f)));
        Vector3 back_origin=Vector3Add(Vector3Add(hip,Vector3Scale(spine,1.6f)),Vector3Scale(forward,-2.05f));
        inflate_draw(&guns[back],back_origin,back_direction,right,(Vector2){.24f,.5f},.65f,WHITE);
        if (clip_files[back]) inflate_draw(&magazines[back],back_origin,back_direction,right,(Vector2){.24f,.5f},.65f,WHITE);
    }
    if (actor->life==ALIVE && weapon_visuals[weapon->id].file) {
        float thickness=weapon->id==LAW || weapon->id==MINIGUN ? 1.15f:.62f;
        inflate_draw(&guns[weapon->id],hand,aim,right,gun_pivot,thickness,WHITE);
        if (clip_files[weapon->id] && (weapon->ammo>0 || weapon->reload_count<definition->reload_time*.3f || weapon->reload_count>definition->reload_time*.8f))
            inflate_draw(&magazines[weapon->id],hand,aim,right,gun_pivot,thickness,WHITE);
        if (weapon->id==BOW || weapon->id==BOW2) {
            inflate_draw(&bow_string,hand,aim,right,(Vector2){-.4f,.55f},.2f,WHITE);
            if (weapon->ammo>0) inflate_draw(&bow_arrow,hand,aim,right,(Vector2){0,.55f},.3f,WHITE);
        }
        if (tick<fire_until[id]) {
            Vec3 muzzle_point=pose_muzzle(&posed,points);
            Vector3 muzzle={muzzle_point.x,muzzle_point.y,muzzle_point.z};
            SurfaceArt fire={body_side[7],{0},{0},{255,198,67,255},WHITE};
            static const Ring flash[]={{0,.4f,.4f},{.3f,1,1},{1,.01f,.01f}};
            loft(muzzle,Vector3Add(muzzle,Vector3Scale(aim,3.5f)),right,.7f,.7f,flash,3,fire);
        }
    }
    Vec3 wounds[10];
    int wound_count=ragdoll_wounds(actor,wounds);
    SurfaceArt wound_art={.side={.id=rlGetTextureIdDefault()},.color={100,5,9,255}};
    for (int i=0;i<wound_count;i++) {
        Vector3 point={wounds[i].x,wounds[i].y,wounds[i].z};
        loft(Vector3Add(point,Vector3Scale(spine,-.3f)),Vector3Add(point,Vector3Scale(spine,.3f)),right,.9f,.9f,rounded,5,wound_art);
    }
    draw_instances(opacity,visibility);
}

void gostek_draw_weapon(WeaponId id,Vec3 position,float yaw) {
    Vec3 heading=direction(yaw,0);
    Vector3 along={heading.x,0,heading.z},right={-heading.z,0,heading.x};
    Vector3 origin={position.x,position.y,position.z};
    if (id==THROWNKNIFE) id=KNIFE;
    instance_count=0;
    float opacity=1,visibility=1;
    inflate_draw(&guns[id],origin,along,right,(Vector2){.5f,.5f},id==LAW||id==MINIGUN?1.15f:.62f,WHITE);
    if (clip_files[id]) inflate_draw(&magazines[id],origin,along,right,(Vector2){.5f,.5f},.62f,WHITE);
    if (id==BOW || id==BOW2) inflate_draw(&bow_string,origin,along,right,(Vector2){.5f,.5f},.2f,WHITE);
    draw_instances(opacity,visibility);
}

void gostek_free(void) {
    while (lofts) {
        LoftMesh *next=lofts->next;
        for (int i=0;i<PART_COUNT;i++) if(lofts->parts[i].mesh.vertexCount) UnloadMesh(lofts->parts[i].mesh);
        free(lofts);lofts=next;
    }
    InflatedArt *artwork[WEAPON_COUNT*2+3];
    for (int i=0;i<WEAPON_COUNT;i++) {artwork[i]=&guns[i];artwork[WEAPON_COUNT+i]=&magazines[i];}
    artwork[WEAPON_COUNT*2]=&grenade;artwork[WEAPON_COUNT*2+1]=&bow_string;artwork[WEAPON_COUNT*2+2]=&bow_arrow;
    for (size_t i=0;i<sizeof(artwork)/sizeof(artwork[0]);i++) while(artwork[i]->meshes) {
        InflatedMesh *mesh=artwork[i]->meshes;
        artwork[i]->meshes=mesh->next;UnloadMesh(mesh->part.mesh);free(mesh);
    }
    free(instances);instances=NULL;instance_count=instance_capacity=0;
    if (instance_buffer) rlUnloadVertexBuffer(instance_buffer);
    instance_buffer=0;buffer_capacity=0;free(buffer_data);buffer_data=NULL;
    for (size_t i=0;i<group_capacity;i++) free(groups[i].data);
    free(groups);groups=NULL;group_count=group_capacity=0;
    UnloadShader(opaque_shader);
    UnloadTexture(turnaround);
    for (int i=0;i<10;i++) UnloadTexture(body_side[i]);
    for (int i=0;i<WEAPON_COUNT;i++) {
        if (weapon_visuals[i].file) { UnloadTexture(guns[i].texture);free(guns[i].top); }
        if (clip_files[i]) { UnloadTexture(magazines[i].texture);free(magazines[i].top); }
    }
    UnloadTexture(grenade.texture);
    free(grenade.top);
    UnloadTexture(bow_string.texture);free(bow_string.top);
    UnloadTexture(bow_arrow.texture);free(bow_arrow.top);
}
