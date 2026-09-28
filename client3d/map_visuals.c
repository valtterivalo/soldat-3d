#include "map_visuals.h"
#include "world.h"
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { Vec3 position; Vector2 uv; Color color; } MapVertex;
static Model terrain;
static Texture2D terrain_texture;

static Color map_color(const unsigned char color[4])
{
    return (Color){color[0], color[1], color[2], color[3]};
}

static Texture2D texture_load(const char *path)
{
    Image image = LoadImage(path);
    if (!IsImageValid(image)) {
        fprintf(stderr, "Cannot load original Soldat texture: %s\n", path);
        exit(EXIT_FAILURE);
    }
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageColorReplace(&image, (Color){0, 255, 0, 255}, BLANK);
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (!IsTextureValid(texture)) {
        fprintf(stderr, "Cannot upload original Soldat texture: %s\n", path);
        exit(EXIT_FAILURE);
    }
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_16X);
    SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    return texture;
}

static void mesh_triangle(Mesh *mesh, int *index, MapVertex a, MapVertex b, MapVertex c)
{
    MapVertex vertices[] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        int n = (*index)++;
        MapVertex v = vertices[i];
        mesh->vertices[n * 3] = v.position.x;
        mesh->vertices[n * 3 + 1] = v.position.y;
        mesh->vertices[n * 3 + 2] = v.position.z;
        mesh->texcoords[n * 2] = v.uv.x;
        mesh->texcoords[n * 2 + 1] = v.uv.y;
        mesh->colors[n * 4] = v.color.r;
        mesh->colors[n * 4 + 1] = v.color.g;
        mesh->colors[n * 4 + 2] = v.color.b;
        mesh->colors[n * 4 + 3] = v.color.a;
    }
}

typedef struct { MapVertex *vertices; size_t count, capacity; } PropMesh;
typedef enum { SCENERY_OPAQUE, SCENERY_MIST, SCENERY_LIGHT } SceneryKind;
typedef struct { Texture2D texture; Color *pixels; int width, height; SceneryKind kind; } SourceArt;

static void terrain_triangle(PropMesh *mesh, MapVertex a, MapVertex b, MapVertex c, Vec3 normal, float reach)
{
    unsigned steps=normal.y>.5f ? (unsigned)ceilf(fmaxf(length(sub(b.position,a.position)),
        fmaxf(length(sub(c.position,a.position)),length(sub(c.position,b.position))))/12) : 1;
    size_t count=(size_t)(steps+1)*(size_t)(steps+2)/2;
    MapVertex *grid=malloc(count*sizeof(*grid));
    if(!grid)abort();
    Vec3 sun=v3(-.35f,.84f,-.41f);sun=scale(sun,1/length(sun));
    float shade=.58f+.42f*fmaxf(0,dot(normal,sun));
    for(unsigned row=0;row<=steps;++row)for(unsigned col=0;col<=steps-row;++col) {
        float u=(float)row/(float)steps,v=(float)col/(float)steps,w=1-u-v;
        MapVertex vertex={
            .position=add(scale(a.position,w),add(scale(b.position,u),scale(c.position,v))),
            .uv={a.uv.x*w+b.uv.x*u+c.uv.x*v,a.uv.y*w+b.uv.y*u+c.uv.y*v},
            .color={(unsigned char)(a.color.r*w+b.color.r*u+c.color.r*v),
                (unsigned char)(a.color.g*w+b.color.g*u+c.color.g*v),
                (unsigned char)(a.color.b*w+b.color.b*u+c.color.b*v),
                (unsigned char)(a.color.a*w+b.color.a*u+c.color.a*v)}
        };
        Vec3 start=add(vertex.position,scale(normal,.1f));
        WorldHit shadow=world_trace_for(start,add(start,scale(sun,reach)),v3(0,0,0),
            (WorldQuery){WORLD_TRACE_LIGHT,0,WORLD_NO_FLAG});
        if(shadow.box>=0) {
            vertex.color.r=(unsigned char)((float)vertex.color.r*.58f/shade);
            vertex.color.g=(unsigned char)((float)vertex.color.g*.58f/shade);
            vertex.color.b=(unsigned char)((float)vertex.color.b*.58f/shade);
        }
        size_t index=(size_t)row*(2*(size_t)steps+3-row)/2+col;
        grid[index]=vertex;
    }
    size_t required=mesh->count+3*(size_t)steps*(size_t)steps;
    if(required>mesh->capacity) {
        mesh->capacity=required>mesh->capacity*2 ? required : mesh->capacity*2;
        MapVertex *vertices=realloc(mesh->vertices,mesh->capacity*sizeof(*vertices));
        if(!vertices)abort();
        mesh->vertices=vertices;
    }
    for(unsigned row=0;row<steps;++row)for(unsigned col=0;col<steps-row;++col) {
        size_t here=(size_t)row*(2*(size_t)steps+3-row)/2+col;
        size_t next=(size_t)(row+1)*(2*(size_t)steps+2-row)/2+col;
        mesh->vertices[mesh->count++]=grid[here];
        mesh->vertices[mesh->count++]=grid[next];
        mesh->vertices[mesh->count++]=grid[here+1];
        if(col+row+1<steps) {
            mesh->vertices[mesh->count++]=grid[here+1];
            mesh->vertices[mesh->count++]=grid[next];
            mesh->vertices[mesh->count++]=grid[next+1];
        }
    }
    free(grid);
}

static Model *props;
static SourceArt *art;

static Vec3 prop_point(const WorldProp *prop, Vec3 p)
{
    float c=cosf(prop->yaw),s=sinf(prop->yaw);
    float x=(p.x-(float)prop->width*.5f)*prop->scale_x;
    float y=((float)prop->height-p.y)*prop->scale_y;
    float z=p.z*(fabsf(prop->scale_x)+fabsf(prop->scale_y))*.5f;
    return add(prop->position,v3(c*x+s*z,y,-s*x+c*z));
}

static void prop_triangle(PropMesh *mesh, const WorldProp *prop,
    Vec3 a, Vec3 b, Vec3 c, Color color)
{
    if (mesh->count + 3 > mesh->capacity) {
        mesh->capacity = mesh->capacity ? mesh->capacity * 2 : 384;
        MapVertex *vertices = realloc(mesh->vertices, mesh->capacity * sizeof(*vertices));
        if (!vertices) abort();
        mesh->vertices = vertices;
    }
    Vec3 local[] = {a, b, c};
    Vec3 world[] = {prop_point(prop, a), prop_point(prop, b), prop_point(prop, c)};
    Vector3 edge_a = {world[1].x-world[0].x, world[1].y-world[0].y, world[1].z-world[0].z};
    Vector3 edge_b = {world[2].x-world[0].x, world[2].y-world[0].y, world[2].z-world[0].z};
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(edge_a, edge_b));
    float light = 0.76f + 0.24f * fabsf(Vector3DotProduct(normal, (Vector3){-.35f,.84f,-.41f}));
    color.r = (unsigned char)((float)color.r * light);
    color.g = (unsigned char)((float)color.g * light);
    color.b = (unsigned char)((float)color.b * light);
    const char *name=world_scenery[prop->style-1];
    Vec3 ab=sub(b,a),ac=sub(c,a);
    Vector3 local_normal=Vector3CrossProduct((Vector3){ab.x,ab.y,ab.z},(Vector3){ac.x,ac.y,ac.z});
    for (int i = 0; i < 3; ++i) {
        Vector2 uv={local[i].x/(float)prop->width,local[i].y/(float)prop->height};
        if (!strncmp(name,"crate",5) || !strncmp(name,"fence",5)) {
            float thickness=(float)prop->height*(!strncmp(name,"fence",5) ? .36f : .8f);
            if (fabsf(local_normal.x)>fabsf(local_normal.z)) uv.x=.5f+local[i].z/thickness;
            if (fabsf(local_normal.y)>fabsf(local_normal.z)) uv.y=.5f+local[i].z/thickness;
        } else if (!strncmp(name,"barrel",6) && fabsf(local_normal.y)>fabsf(local_normal.x)+fabsf(local_normal.z)) {
            float center=Clamp(local[i].y/(float)prop->height,.08f,.92f);
            uv.y=center+.15f*local[i].z/(float)prop->width;
        }
        mesh->vertices[mesh->count++] = (MapVertex){world[i],uv,color};
    }
}

static void prop_tube(PropMesh *mesh, const WorldProp *prop,
    Vec3 start, Vec3 end, float start_radius, float end_radius, Color color, int sides)
{
    Vector3 axis = Vector3Normalize((Vector3){end.x-start.x,end.y-start.y,end.z-start.z});
    Vector3 side = Vector3Normalize(Vector3CrossProduct(axis,
        fabsf(axis.z)<.9f ? (Vector3){0,0,1} : (Vector3){0,1,0}));
    Vector3 other = Vector3CrossProduct(axis, side);
    for (int i = 0; i < sides; ++i) {
        float angle = 2 * PI * (float)i / (float)sides;
        float next = 2 * PI * (float)(i+1) / (float)sides;
        Vec3 radial = v3(side.x*cosf(angle)+other.x*sinf(angle),side.y*cosf(angle)+other.y*sinf(angle),side.z*cosf(angle)+other.z*sinf(angle));
        Vec3 radial_next = v3(side.x*cosf(next)+other.x*sinf(next),side.y*cosf(next)+other.y*sinf(next),side.z*cosf(next)+other.z*sinf(next));
        Vec3 a = add(start,scale(radial,start_radius)), b = add(start,scale(radial_next,start_radius));
        Vec3 c = add(end,scale(radial_next,end_radius)), d = add(end,scale(radial,end_radius));
        prop_triangle(mesh,prop,a,b,c,color);
        if (end_radius > 0) {
            prop_triangle(mesh,prop,a,c,d,color);
            prop_triangle(mesh,prop,end,d,c,color);
        }
        prop_triangle(mesh,prop,start,b,a,color);
    }
}

static void prop_box(PropMesh *mesh, const WorldProp *prop, Vec3 lo, Vec3 hi, Color color)
{
    Vec3 faces[6][4] = {
        {{lo.x,lo.y,lo.z},{hi.x,lo.y,lo.z},{hi.x,hi.y,lo.z},{lo.x,hi.y,lo.z}},
        {{hi.x,lo.y,hi.z},{lo.x,lo.y,hi.z},{lo.x,hi.y,hi.z},{hi.x,hi.y,hi.z}},
        {{lo.x,lo.y,hi.z},{lo.x,lo.y,lo.z},{lo.x,hi.y,lo.z},{lo.x,hi.y,hi.z}},
        {{hi.x,lo.y,lo.z},{hi.x,lo.y,hi.z},{hi.x,hi.y,hi.z},{hi.x,hi.y,lo.z}},
        {{lo.x,lo.y,hi.z},{hi.x,lo.y,hi.z},{hi.x,lo.y,lo.z},{lo.x,lo.y,lo.z}},
        {{lo.x,hi.y,lo.z},{hi.x,hi.y,lo.z},{hi.x,hi.y,hi.z},{lo.x,hi.y,hi.z}}
    };
    for (int i=0;i<6;++i) {
        prop_triangle(mesh,prop,faces[i][0],faces[i][1],faces[i][2],color);
        prop_triangle(mesh,prop,faces[i][0],faces[i][2],faces[i][3],color);
    }
}

static void prop_rock(PropMesh *mesh, const WorldProp *prop,
    Vec3 center, Vec3 radius, float phase, Color color)
{
    for (int ring=0;ring<6;++ring) {
        float a=PI*(float)ring/6,b=PI*(float)(ring+1)/6;
        for (int slice=0;slice<12;++slice) {
            float u=2*PI*(float)slice/12,v=2*PI*(float)(slice+1)/12;
            float angles[4][2]={{a,u},{a,v},{b,v},{b,u}};
            Vec3 p[4];
            for (int k=0;k<4;++k) {
                float latitude=angles[k][0],longitude=angles[k][1];
                float lump=1+.13f*sinf(longitude*3+phase)*sinf(latitude)*sinf(latitude*2+phase);
                p[k]=add(center,v3(radius.x*sinf(latitude)*cosf(longitude)*lump,
                    radius.y*cosf(latitude),radius.z*sinf(latitude)*sinf(longitude)*lump));
            }
            if (ring>0) prop_triangle(mesh,prop,p[0],p[1],p[2],color);
            if (ring<5) prop_triangle(mesh,prop,p[0],p[2],p[3],color);
        }
    }
}

static void prop_roots(PropMesh *mesh, const WorldProp *prop, const SourceArt *source, Color color)
{
    int w=source->width,h=source->height;
    size_t count=(size_t)w*(size_t)h;
    unsigned char *mask=calloc(count,1),*remove=calloc(count,1);
    int *distance=malloc(count*sizeof(*distance));
    if (!mask || !remove || !distance) abort();
    for (size_t i=0;i<count;++i) {
        mask[i]=source->pixels[i].a>127;
        distance[i]=mask[i] ? w+h : 0;
    }
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        int i=y*w+x;
        if (x>0 && distance[i]>distance[i-1]+1) distance[i]=distance[i-1]+1;
        if (y>0 && distance[i]>distance[i-w]+1) distance[i]=distance[i-w]+1;
    }
    for (int y=h-1;y>=0;--y) for (int x=w-1;x>=0;--x) {
        int i=y*w+x;
        if (x+1<w && distance[i]>distance[i+1]+1) distance[i]=distance[i+1]+1;
        if (y+1<h && distance[i]>distance[i+w]+1) distance[i]=distance[i+w]+1;
    }
    int changed;
    do {
        changed=0;
        for (int pass=0;pass<2;++pass) {
            memset(remove,0,count);
            for (int y=1;y<h-1;++y) for (int x=1;x<w-1;++x) {
                int i=y*w+x;
                if (!mask[i]) continue;
                unsigned char n[]={mask[i-w],mask[i-w+1],mask[i+1],mask[i+w+1],mask[i+w],mask[i+w-1],mask[i-1],mask[i-w-1]};
                int neighbors=0,transitions=0;
                for (int k=0;k<8;++k) {neighbors+=n[k];transitions+=!n[k] && n[(k+1)%8];}
                int first=pass ? n[0]*n[2]*n[6] : n[0]*n[2]*n[4];
                int second=pass ? n[0]*n[4]*n[6] : n[2]*n[4]*n[6];
                if (neighbors>=2 && neighbors<=6 && transitions==1 && first==0 && second==0)
                    remove[i]=1;
            }
            for (size_t i=0;i<count;++i) if (remove[i]) {mask[i]=0;changed=1;}
        }
    } while (changed);
    const int neighbors[4][2]={{1,0},{0,1},{1,1},{-1,1}};
    float unit=.5f*((float)prop->width/(float)w+(float)prop->height/(float)h);
    for (int y=1;y<h-1;++y) for (int x=1;x<w-1;++x) {
        int i=y*w+x;
        if (!mask[i]) continue;
        for (int k=0;k<4;++k) {
            int nx=x+neighbors[k][0],ny=y+neighbors[k][1],j=ny*w+nx;
            if (!mask[j]) continue;
            Vec3 a=v3((float)prop->width*((float)x+.5f)/(float)w,(float)prop->height*((float)y+.5f)/(float)h,
                (float)prop->height*.09f*sinf((float)x*.025f)+(float)prop->width*.05f*cosf((float)y*.037f));
            Vec3 b=v3((float)prop->width*((float)nx+.5f)/(float)w,(float)prop->height*((float)ny+.5f)/(float)h,
                (float)prop->height*.09f*sinf((float)nx*.025f)+(float)prop->width*.05f*cosf((float)ny*.037f));
            prop_tube(mesh,prop,a,b,unit*(float)distance[i],unit*(float)distance[j],color,6);
        }
    }
    free(mask);free(remove);free(distance);
}

static void prop_grass(PropMesh *mesh, const WorldProp *prop, const SourceArt *source, int layer)
{
    int blades=(int)((float)prop->width/1.7f);
    for (int i=0;i<blades;++i) {
        float x=((float)i+.5f)*(float)prop->width/(float)blades;
        int column=(int)(x*(float)source->width/(float)prop->width),top=0;
        while (top<source->height && source->pixels[top*source->width+column].a<128) ++top;
        if (top==source->height) continue;
        float height=(float)prop->height*(1-(float)top/(float)source->height);
        float phase=(float)i*2.399963f+(float)layer*1.7f;
        float z=sinf(phase)*height*.7f;
        float lean_x=cosf(phase*1.3f)*height*.23f,lean_z=sinf(phase*.8f)*height*.3f;
        float width=.4f+height*.065f,thickness=width*.24f;
        Color color=source->pixels[top*source->width+column];
        for (int y=top+1;y<source->height;++y) {
            Color candidate=source->pixels[y*source->width+column];
            if (candidate.a>127 && candidate.g>color.g) color=candidate;
        }
        color.r=(unsigned char)((unsigned)color.r*prop->color[0]/255);
        color.g=(unsigned char)((unsigned)color.g*prop->color[1]/255);
        color.b=(unsigned char)((unsigned)color.b*prop->color[2]/255);
        Vec3 base[4],mid[4];
        const float corners[4][2]={{-1,-1},{1,-1},{1,1},{-1,1}};
        for (int k=0;k<4;++k) {
            float a=corners[k][0]*width,b=corners[k][1]*thickness;
            base[k]=v3(x+a*cosf(phase)-b*sinf(phase),(float)prop->height,z+a*sinf(phase)+b*cosf(phase));
            mid[k]=v3(x+lean_x*.4f+(a*cosf(phase)-b*sinf(phase))*.7f,
                (float)prop->height-height*.55f,z+lean_z*.4f+(a*sinf(phase)+b*cosf(phase))*.7f);
        }
        Vec3 tip=v3(x+lean_x,(float)prop->height-height,z+lean_z);
        prop_triangle(mesh,prop,base[0],base[2],base[1],color);
        prop_triangle(mesh,prop,base[0],base[3],base[2],color);
        for (int k=0;k<4;++k) {
            int next=(k+1)%4;
            prop_triangle(mesh,prop,base[k],base[next],mid[next],color);
            prop_triangle(mesh,prop,base[k],mid[next],mid[k],color);
            prop_triangle(mesh,prop,mid[k],mid[next],tip,color);
        }
    }
}

static void prop_inflate(PropMesh *mesh, const WorldProp *prop, const SourceArt *source, Color color)
{
    int columns=(int)ceilf(fminf((float)source->width/4,(float)prop->width*fabsf(prop->scale_x)/2));
    int rows=(int)ceilf(fminf((float)source->height/4,(float)prop->height*fabsf(prop->scale_y)/2));
    columns=columns<2 ? 2 : columns;rows=rows<2 ? 2 : rows;
    size_t count=(size_t)(columns+1)*(size_t)(rows+1);
    int *distance=calloc(count,sizeof(*distance));if (!distance) abort();
    for (int y=1;y<rows;++y) for (int x=1;x<columns;++x) {
        int sx=x*source->width/columns,sy=y*source->height/rows;
        distance[y*(columns+1)+x]=source->pixels[sy*source->width+sx].a>127 ? columns+rows : 0;
    }
    for (int y=1;y<rows;++y) for (int x=1;x<columns;++x) {
        int p=y*(columns+1)+x;
        if (distance[p]>distance[p-1]+1) distance[p]=distance[p-1]+1;
        if (distance[p]>distance[p-columns-1]+1) distance[p]=distance[p-columns-1]+1;
    }
    int maximum=1;
    for (int y=rows-1;y>0;--y) for (int x=columns-1;x>0;--x) {
        int p=y*(columns+1)+x;
        if (distance[p]>distance[p+1]+1) distance[p]=distance[p+1]+1;
        if (distance[p]>distance[p+columns+1]+1) distance[p]=distance[p+columns+1]+1;
        if (distance[p]>maximum) maximum=distance[p];
    }
    float thickness=.3f*fminf((float)prop->width,(float)prop->height);
    const int offsets[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for (int y=0;y<rows;++y) for (int x=0;x<columns;++x) {
        Vec3 front[4],back[4];int volume=0;
        for (int v=0;v<4;++v) {
            int column=x+offsets[v][0],row=y+offsets[v][1];
            int d=distance[row*(columns+1)+column];volume+=d;
            float z=thickness*sqrtf((float)d/(float)maximum);
            front[v]=v3((float)prop->width*(float)column/(float)columns,(float)prop->height*(float)row/(float)rows,z);
            back[v]=front[v];back[v].z=-z;
        }
        if (!volume) continue;
        prop_triangle(mesh,prop,front[0],front[1],front[2],color);
        prop_triangle(mesh,prop,front[0],front[2],front[3],color);
        prop_triangle(mesh,prop,back[2],back[1],back[0],color);
        prop_triangle(mesh,prop,back[3],back[2],back[0],color);
    }
    free(distance);
}

static int map_face_banded(const WorldSolid *solid,unsigned face)
{
    if(solid->texture!=WORLD_TERRAIN || face<2 || solid->face_size[face]!=4)return 0;
    unsigned bottom=solid->faces[face][0],top=solid->faces[face][3];
    return solid->vertices[top].y-solid->vertices[bottom].y>8 &&
        memcmp(solid->color[bottom],solid->color[top],3)!=0;
}


void map_visuals_init(void)
{
    char terrain_path[512];snprintf(terrain_path,sizeof(terrain_path),"%s/%s",SOLDAT_ASSET_DIR,world_texture);
    terrain_texture=texture_load(terrain_path);
    art=calloc(world_scenery_count,sizeof(*art));
    props=calloc(world_scenery_count,sizeof(*props));
    PropMesh *build=calloc(world_scenery_count,sizeof(*build));
    if ((!art || !props || !build) && world_scenery_count) abort();
    for (size_t i=0;i<world_scenery_count;++i) {
        const char *name=world_scenery[i];
        art[i].kind=(!strncmp(name,"blank",5) || !strncmp(name,"light",5)) ? SCENERY_LIGHT :
            (!strncmp(name,"cloud",5) || !strncmp(name,"fog",3)) ? SCENERY_MIST : SCENERY_OPAQUE;
        char path[512];
        snprintf(path,sizeof(path),"%s/scenery-gfx/%s",SOLDAT_ASSET_DIR,world_scenery[i]);
        strcpy(strrchr(path,'.'),".png");
        if (!FileExists(path))
            snprintf(path,sizeof(path),"%s/scenery-gfx/%s",SOLDAT_ASSET_DIR,world_scenery[i]);
        Image image=LoadImage(path);
        if (!IsImageValid(image)) {fprintf(stderr,"Cannot load original prop: %s\n",path);exit(EXIT_FAILURE);}
        ImageFormat(&image,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        ImageColorReplace(&image,(Color){0,255,0,255},BLANK);
        art[i].pixels=LoadImageColors(image);
        art[i].width=image.width;art[i].height=image.height;
        uint64_t red=0,green=0,blue=0,opaque=0;
        size_t count=(size_t)image.width*(size_t)image.height;
        for (size_t p=0;p<count;++p) if (art[i].pixels[p].a>127) {
            red+=art[i].pixels[p].r;green+=art[i].pixels[p].g;blue+=art[i].pixels[p].b;++opaque;
        }
        Color average={(unsigned char)(red/opaque),(unsigned char)(green/opaque),(unsigned char)(blue/opaque),255};
        Color *pixels=image.data;
        for (size_t p=0;p<count && art[i].kind==SCENERY_OPAQUE;++p) {
            if (pixels[p].a<128) pixels[p]=average;
            pixels[p].a=255;
        }
        art[i].texture=LoadTextureFromImage(image);
        UnloadImage(image);
        if (!IsTextureValid(art[i].texture)) abort();
        GenTextureMipmaps(&art[i].texture);
        SetTextureFilter(art[i].texture,TEXTURE_FILTER_ANISOTROPIC_16X);
        SetTextureWrap(art[i].texture,TEXTURE_WRAP_CLAMP);
    }
    PropMesh terrain_build={0};
    float reach=2*length(sub(world_bounds.max,world_bounds.min));
    for (size_t p=0;p<world_solid_count;++p) {
        const WorldSolid *solid=&world_solids[p];if(solid->texture!=WORLD_TERRAIN)continue;
        Vec3 center=v3(0,0,0);
        for(unsigned v=0;v<solid->vertex_count;++v)center=add(center,scale(solid->vertices[v],1/(float)solid->vertex_count));
        for (unsigned face=0;face<solid->face_count;++face) {
            if(!(solid->visible_faces&(1u<<face)))continue;
            MapVertex vertices[8];
            Vec3 a=solid->vertices[solid->faces[face][0]];
            Vec3 edge=sub(solid->vertices[solid->faces[face][1]],a);
            Vec3 other=sub(solid->vertices[solid->faces[face][2]],a);
            Vector3 normal=Vector3Normalize(Vector3CrossProduct((Vector3){edge.x,edge.y,edge.z},(Vector3){other.x,other.y,other.z}));
            if(dot(v3(normal.x,normal.y,normal.z),sub(center,a))>0)normal=Vector3Negate(normal);
            float shade=.58f+.42f*fmaxf(0,Vector3DotProduct(normal,(Vector3){-.35f,.84f,-.41f}));
            for (unsigned k=0;k<solid->face_size[face];++k) {
                unsigned v=solid->faces[face][k];
                Vec3 position=solid->vertices[v];
                Vector2 uv;
                if (fabsf(normal.y)>=fabsf(normal.x) && fabsf(normal.y)>=fabsf(normal.z))
                    uv=(Vector2){position.x/80,position.z/80};
                else if (fabsf(normal.x)>=fabsf(normal.z))
                    uv=(Vector2){position.z/80,position.y/80};
                else uv=(Vector2){position.x/80,position.y/80};
                Color color=map_color(solid->color[v]);
                color.r=(unsigned char)((float)color.r*shade);
                color.g=(unsigned char)((float)color.g*shade);
                color.b=(unsigned char)((float)color.b*shade);
                for (size_t light=0;light<world_prop_count;++light) {
                    const WorldProp *prop=&world_props[light];
                    if (!prop->active || art[prop->style-1].kind!=SCENERY_LIGHT) continue;
                    const SourceArt *source=&art[prop->style-1];
                    Vec3 delta=sub(position,prop->position);
                    float c=cosf(prop->yaw),s=sinf(prop->yaw);
                    float u=.5f+(delta.x*c-delta.z*s)/((float)prop->width*prop->scale_x);
                    float v=.5f+(delta.x*s+delta.z*c)/((float)prop->height*prop->scale_y);
                    if (u<0 || u>=1 || v<0 || v>=1) continue;
                    Color sample=source->pixels[(int)(v*(float)source->height)*source->width+(int)(u*(float)source->width)];
                    float alpha=(float)sample.a*(float)prop->color[3]/65025;
                    color.r=(unsigned char)((float)color.r*(1-alpha)+(float)sample.r*(float)prop->color[0]/255*alpha);
                    color.g=(unsigned char)((float)color.g*(1-alpha)+(float)sample.g*(float)prop->color[1]/255*alpha);
                    color.b=(unsigned char)((float)color.b*(1-alpha)+(float)sample.b*(float)prop->color[2]/255*alpha);
                }
                vertices[k]=(MapVertex){position,uv,color};
            }
            if(map_face_banded(solid,face)) {
                MapVertex soil[2],crest[2];
                for(unsigned k=0;k<2;++k) {
                    MapVertex low=vertices[k],high=vertices[3-k];
                    float fraction=1-6/(high.position.y-low.position.y);
                    soil[k]=low;
                    soil[k].position=add(low.position,scale(sub(high.position,low.position),fraction));
                    soil[k].uv=Vector2Lerp(low.uv,high.uv,fraction);
                    fraction=1-4/(high.position.y-low.position.y);
                    crest[k]=high;
                    crest[k].position=add(low.position,scale(sub(high.position,low.position),fraction));
                    crest[k].uv=Vector2Lerp(low.uv,high.uv,fraction);
                }
                terrain_triangle(&terrain_build,vertices[0],vertices[1],soil[1],v3(normal.x,normal.y,normal.z),reach);
                terrain_triangle(&terrain_build,vertices[0],soil[1],soil[0],v3(normal.x,normal.y,normal.z),reach);
                terrain_triangle(&terrain_build,soil[0],soil[1],crest[1],v3(normal.x,normal.y,normal.z),reach);
                terrain_triangle(&terrain_build,soil[0],crest[1],crest[0],v3(normal.x,normal.y,normal.z),reach);
                terrain_triangle(&terrain_build,crest[0],crest[1],vertices[2],v3(normal.x,normal.y,normal.z),reach);
                terrain_triangle(&terrain_build,crest[0],vertices[2],vertices[3],v3(normal.x,normal.y,normal.z),reach);
                continue;
            }
            for(unsigned k=1;k+1<solid->face_size[face];++k)
                terrain_triangle(&terrain_build,vertices[0],vertices[k],vertices[k+1],v3(normal.x,normal.y,normal.z),reach);
        }
    }
    Mesh mesh={0};
    mesh.vertexCount=(int)terrain_build.count;
    mesh.triangleCount=mesh.vertexCount/3;
    mesh.vertices=MemAlloc((unsigned)mesh.vertexCount*3*sizeof(float));
    mesh.texcoords=MemAlloc((unsigned)mesh.vertexCount*2*sizeof(float));
    mesh.colors=MemAlloc((unsigned)mesh.vertexCount*4);
    if(!mesh.vertices || !mesh.texcoords || !mesh.colors)abort();
    int index=0;
    for(size_t i=0;i<terrain_build.count;i+=3)
        mesh_triangle(&mesh,&index,terrain_build.vertices[i],terrain_build.vertices[i+1],terrain_build.vertices[i+2]);
    free(terrain_build.vertices);
    UploadMesh(&mesh,false);
    terrain=LoadModelFromMesh(mesh);
    terrain.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=terrain_texture;
    for (size_t i=0;i<world_prop_count;++i) {
        const WorldProp *prop=&world_props[i];
        if (!prop->active) continue;
        const char *name=world_scenery[prop->style-1];
        PropMesh *out=&build[prop->style-1];
        const SourceArt *source=&art[prop->style-1];
        if (source->kind==SCENERY_LIGHT) continue;
        Color color=map_color(prop->color);
        float w=(float)prop->width,h=(float)prop->height;
        if (!strncmp(name,"grass",5)) {
            prop_grass(out,prop,source,(int)i);
        } else if (!strncmp(name,"barrel",6)) {
            float r=w*.46f;
            prop_tube(out,prop,v3(w*.5f,h*.06f,0),v3(w*.5f,h*.94f,0),r,r,color,16);
            prop_tube(out,prop,v3(w*.5f,0,0),v3(w*.5f,h*.065f,0),r*.87f,r*.99f,color,16);
            prop_tube(out,prop,v3(w*.5f,h*.935f,0),v3(w*.5f,h,0),r*.99f,r*.87f,color,16);
            for (int band=0;band<2;++band) {
                float y=h*(band ? .78f : .22f);
                prop_tube(out,prop,v3(w*.5f,y-h*.025f,0),v3(w*.5f,y+h*.025f,0),r*1.04f,r*1.04f,color,16);
            }
        } else if (!strncmp(name,"crate",5) || !strncmp(name,"fence",5)) {
            float thickness=h*(!strncmp(name,"fence",5) ? .36f : .8f);
            prop_box(out,prop,v3(0,0,-thickness*.5f),v3(w,h,thickness*.5f),color);
        } else if (!strncmp(name,"rocks",5)) {
            for (int stone=0;stone<5;++stone) {
                float phase=(float)stone*2.39f;
                prop_rock(out,prop,v3(w*((float)stone+.5f)/5,h*.72f,sinf(phase)*h*.12f),
                    v3(w*.145f,h*(.27f+.09f*sinf(phase)),h*.38f),phase,color);
            }
        } else if (!strncmp(name,"roots",5)) {
            prop_roots(out,prop,source,color);
        } else if (!strncmp(name,"camonet",7)) {
            for (int y=0;y<=8;++y) for (int x=0;x<=8;++x) {
                float px=w*(float)x/8,py=h*(float)y/8;
                Vec3 a=v3(px,py,1.7f*sinf(px*.17f+py*.23f));
                if (x<8) {
                    Vec3 b=v3(px+w/8,py,1.7f*sinf((px+w/8)*.17f+py*.23f));
                    prop_tube(out,prop,a,b,.17f,.17f,color,6);
                }
                if (y<8) {
                    Vec3 b=v3(px,py+h/8,1.7f*sinf(px*.17f+(py+h/8)*.23f));
                    prop_tube(out,prop,a,b,.17f,.17f,color,6);
                }
                if (x<8 && y<8) {
                    int sx=(int)((px+w/16)*(float)source->width/w);
                    int sy=(int)((py+h/16)*(float)source->height/h);
                    if (source->pixels[sy*source->width+sx].a>127)
                        prop_rock(out,prop,add(a,v3(w/16,h/16,0)),v3(w*.045f,h*.035f,.35f),(float)(x+y),color);
                }
            }
        } else {
            prop_inflate(out,prop,source,color);
        }
    }
    for (size_t i=0;i<world_scenery_count;++i) {
        UnloadImageColors(art[i].pixels);
        art[i].pixels=NULL;
        if (art[i].kind==SCENERY_LIGHT) continue;
        Mesh prop={0};
        prop.vertexCount=(int)build[i].count;
        prop.triangleCount=prop.vertexCount/3;
        prop.vertices=MemAlloc((unsigned)prop.vertexCount*3*sizeof(float));
        prop.texcoords=MemAlloc((unsigned)prop.vertexCount*2*sizeof(float));
        prop.colors=MemAlloc((unsigned)prop.vertexCount*4);
        if (!prop.vertices || !prop.texcoords || !prop.colors) abort();
        int n=0;
        for (size_t v=0;v<build[i].count;v+=3)
            mesh_triangle(&prop,&n,build[i].vertices[v],build[i].vertices[v+1],build[i].vertices[v+2]);
        UploadMesh(&prop,false);
        props[i]=LoadModelFromMesh(prop);
        if (strncmp(world_scenery[i],"grass",5))
            props[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture=art[i].texture;
        free(build[i].vertices);
    }
    free(build);
}

void map_visuals_background(void)
{
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
        map_color(world_background[0]), map_color(world_background[1]));
}

void map_visuals_draw(void)
{
    rlDisableBackfaceCulling();
    DrawModel(terrain,(Vector3){0,0,0},1,WHITE);
}

void map_visuals_scenery(void)
{
    rlEnableDepthMask();
    rlDisableBackfaceCulling();
    for (size_t i=0;i<world_scenery_count;++i)
        if (art[i].kind==SCENERY_OPAQUE) DrawModel(props[i],(Vector3){0,0,0},1,WHITE);
    rlDisableDepthMask();
    for (size_t i=0;i<world_scenery_count;++i)
        if (art[i].kind==SCENERY_MIST) DrawModel(props[i],(Vector3){0,0,0},1,WHITE);
    rlEnableDepthMask();
}

void map_visuals_free(void)
{
    UnloadModel(terrain);
    UnloadTexture(terrain_texture);
    for (size_t i=0;i<world_scenery_count;++i) {
        if (art[i].kind!=SCENERY_LIGHT) UnloadModel(props[i]);
        UnloadTexture(art[i].texture);
        UnloadImageColors(art[i].pixels);
    }
    free(props);
    free(art);
}
