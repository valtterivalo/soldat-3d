#include "effects.h"
#include "gostek.h"
#include "ragdoll.h"
#include "raymath.h"
#include "rlgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Texture2D texture(const char *path) {
    Texture2D result = LoadTexture(TextFormat("%s/%s", SOLDAT_ASSET_DIR, path));
    if (!IsTextureValid(result)) {
        fprintf(stderr, "Failed to load original effect: %s\n", path);
        exit(1);
    }
    SetTextureFilter(result, TEXTURE_FILTER_BILINEAR);
    return result;
}

typedef enum { PROFILE_HORIZONTAL, PROFILE_VERTICAL } ProfileAxis;

static Model revolve(const char *path, ProfileAxis axis, unsigned char alpha_threshold) {
    Image image = LoadImage(TextFormat("%s/%s", SOLDAT_ASSET_DIR, path));
    if (!IsImageValid(image)) {
        fprintf(stderr, "Failed to load projectile profile: %s\n", path);
        exit(1);
    }
    Color *pixels = LoadImageColors(image);
    int columns = axis == PROFILE_VERTICAL ? image.height : image.width;
    int rows = axis == PROFILE_VERTICAL ? image.width : image.height;
    int *lower = MemAlloc((unsigned int)columns * sizeof(*lower));
    int *upper = MemAlloc((unsigned int)columns * sizeof(*upper));
    if (!pixels || !lower || !upper) abort();
    int first = columns, last = 0;
    for (int x = 0; x < columns; ++x) {
        lower[x] = rows;
        upper[x] = -1;
        for (int y = 0; y < rows; ++y) {
            int source = axis == PROFILE_VERTICAL ? (image.height - 1 - x) * image.width + y : y * image.width + x;
            if (pixels[source].a < alpha_threshold) continue;
            if (lower[x] == rows) lower[x] = y;
            upper[x] = y;
        }
        if (upper[x] < 0) continue;
        if (first == columns) first = x;
        last = x;
    }
    if (first > last) abort();
    enum { SIDES = 12 };
    Mesh mesh = {0};
    mesh.triangleCount = (last - first + 2) * SIDES * 2;
    mesh.vertexCount = mesh.triangleCount * 3;
    mesh.vertices = MemAlloc((unsigned int)mesh.vertexCount * 3 * sizeof(float));
    mesh.normals = MemAlloc((unsigned int)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = MemAlloc((unsigned int)mesh.vertexCount * 4);
    if (!mesh.vertices || !mesh.normals || !mesh.colors) abort();
    const int corners[6][2] = {{0, 0}, {1, 1}, {1, 0}, {0, 0}, {0, 1}, {1, 1}};
    int vertex = 0;
    for (int segment = first - 1; segment <= last; ++segment) {
        float radii[2];
        for (int end = 0; end < 2; ++end) {
            int x = segment + end;
            radii[end] = x < first || x > last || upper[x] < 0 ? 0 :
                (float)(upper[x] - lower[x] + 1) / (2 * SRC_ART_SCALE);
        }
        for (int side = 0; side < SIDES; ++side) {
            for (int corner = 0; corner < 6; ++corner) {
                int end = corners[corner][0];
                int x = segment + end;
                int sample_x = x < first ? first : x > last ? last : x;
                float angle = (side + corners[corner][1]) * 2 * PI / SIDES;
                int sample_y = upper[sample_x] < 0 ? rows / 2 :
                    (int)lroundf((lower[sample_x] + upper[sample_x]) * .5f +
                    (upper[sample_x] - lower[sample_x]) * .5f * cosf(angle));
                Vector3 normal = Vector3Normalize((Vector3){(radii[0] - radii[1]) * SRC_ART_SCALE, cosf(angle), sinf(angle)});
                int source = axis == PROFILE_VERTICAL ? (image.height - 1 - sample_x) * image.width + sample_y :
                    sample_y * image.width + sample_x;
                Color color = pixels[source];
                float shade = .72f + .28f * fmaxf(0, normal.y * .8f + normal.z * .6f);
                mesh.vertices[vertex * 3] = ((float)x - (first + last) * .5f) / SRC_ART_SCALE;
                mesh.vertices[vertex * 3 + 1] = radii[end] * cosf(angle);
                mesh.vertices[vertex * 3 + 2] = radii[end] * sinf(angle);
                mesh.normals[vertex * 3] = normal.x;
                mesh.normals[vertex * 3 + 1] = normal.y;
                mesh.normals[vertex * 3 + 2] = normal.z;
                mesh.colors[vertex * 4] = (unsigned char)(color.r * shade);
                mesh.colors[vertex * 4 + 1] = (unsigned char)(color.g * shade);
                mesh.colors[vertex * 4 + 2] = (unsigned char)(color.b * shade);
                mesh.colors[vertex * 4 + 3] = 255;
                ++vertex;
            }
        }
    }
    UploadMesh(&mesh, false);
    Model model = LoadModelFromMesh(mesh);
    UnloadImageColors(pixels);
    UnloadImage(image);
    MemFree(lower);
    MemFree(upper);
    return model;
}

Effects effects_load(void) {
    Effects result = {0};
    for (int i = 0; i < SRC_EXPLOSION_ANIMS; ++i)
        result.explosion[i] = texture(TextFormat("sparks-gfx/explosion/explode%d.png", i + 1));
    for (int i = 0; i < SRC_SMOKE_ANIMS; ++i)
        result.smoke[i] = texture(TextFormat("sparks-gfx/explosion/smoke%d.png", i + 1));
    result.smoke[SRC_SMOKE_ANIMS] = texture("sparks-gfx/minismoke.png");
    result.cluster_smoke = texture("sparks-gfx/bigsmoke.png");
    const char *names[COLT + 1] = {
        "eagles", "mp5", "ak74", "steyraug", "spas12", "ruger77", "m79", "barretm82",
        "m249", "minigun", "colt"
    };
    for (int i = 0; i <= COLT; ++i) {
        result.bullets[i] = revolve(TextFormat("weapons-gfx/%s-bullet.png", names[i]), PROFILE_HORIZONTAL, 96);
        result.shells[i] = revolve(TextFormat("weapons-gfx/%s-shell.png", names[i]), PROFILE_HORIZONTAL, 96);
    }
    result.grenade = revolve("weapons-gfx/frag-grenade.png", PROFILE_VERTICAL, 96);
    result.missile = revolve("weapons-gfx/missile.png", PROFILE_HORIZONTAL, 96);
    result.blood = revolve("sparks-gfx/blood.png", PROFILE_HORIZONTAL, 96);
    result.spark = revolve("sparks-gfx/odprysk.png", PROFILE_HORIZONTAL, 96);
    result.jet = revolve("sparks-gfx/jetfire.png", PROFILE_VERTICAL, 96);
    result.cluster_grenade = revolve("weapons-gfx/cluster-grenade.png", PROFILE_VERTICAL, 96);
    result.cluster = revolve("weapons-gfx/cluster.png", PROFILE_VERTICAL, 96);
    result.arrow = revolve("weapons-gfx/arrow.png", PROFILE_HORIZONTAL, 96);
    result.flag_pole = revolve("objects-gfx/flag.png", PROFILE_HORIZONTAL, 96);
    result.flag_cloth[0] = texture("textures/objects/flag.bmp");
    result.flag_cloth[1] = texture("textures/objects/infflag.bmp");
    for (int i=0;i<SRC_FLAMER_TIMEOUT/2;++i)
        result.flames[i] = revolve(TextFormat("sparks-gfx/flames/explode%d.png",i+1), PROFILE_VERTICAL, 1);
    const char *kits[7] = {"medikit.png", "grenadekit.png", "flamerkit.bmp", "predatorkit.bmp",
        "vestkit.png", "berserkerkit.bmp", "clusterkit.png"};
    for (int i = 0; i < 7; ++i) {
        result.kit_textures[i] = texture(TextFormat("textures/objects/%s", kits[i]));
        result.kits[i] = LoadModelFromMesh(GenMeshCube(SRC_KIT_WIDTH, SRC_KIT_HEIGHT, 5.5f));
        result.kits[i].materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = result.kit_textures[i];
    }
    result.instance_shader=LoadShaderFromMemory(
        "#version 330\n"
        "in vec3 vertexPosition; in vec2 vertexTexCoord; in vec4 vertexColor; in mat4 instanceTransform;"
        "uniform mat4 mvp; out vec2 fragTexCoord; out vec4 fragColor;"
        "void main(){fragTexCoord=vertexTexCoord;fragColor=vertexColor;"
        "gl_Position=mvp*instanceTransform*vec4(vertexPosition,1.0);}",
        "#version 330\n"
        "in vec2 fragTexCoord; in vec4 fragColor; uniform sampler2D texture0; uniform vec4 colDiffuse;"
        "out vec4 finalColor; void main(){finalColor=texture(texture0,fragTexCoord)*fragColor*colDiffuse;}");
    if(!IsShaderValid(result.instance_shader) || result.instance_shader.id==rlGetShaderIdDefault())abort();
    result.instance_shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM]=GetShaderLocationAttrib(result.instance_shader,"instanceTransform");
    return result;
}

void effects_step(Effects *effects, const Game *game, uint64_t tick) {
    for (size_t i = 0; i < game->event_count; ++i) {
        if (effects->count == effects->capacity) {
            size_t capacity = effects->capacity ? effects->capacity * 2 : 64;
            VisualEvent *events = realloc(effects->events, capacity * sizeof(*events));
            if (!events) abort();
            effects->events = events;
            effects->capacity = capacity;
        }
        effects->events[effects->count++] = (VisualEvent){game->events[i], tick};
    }
    for (size_t i = 0; i < effects->count;) {
        int duration = effects->events[i].event.kind == EVENT_EXPLOSION ? SRC_SMOKE_ANIMS * 4 + 10 : 45;
        if (tick - effects->events[i].tick >= (uint64_t)duration)
            effects->events[i] = effects->events[--effects->count];
        else ++i;
    }
}

static void solid(Effects *effects,Model model, Vec3 position, Vec3 axis, Vec3 size, Color color) {
    size_t index=0;
    for(;index<effects->batch_count;++index) {
        EffectBatch *batch=&effects->batches[index];
        if(batch->model.meshes==model.meshes && ColorToInt(batch->color)==ColorToInt(color))break;
    }
    if(index==effects->batch_count) {
        EffectBatch *batches=realloc(effects->batches,(index+1)*sizeof(*batches));
        if(!batches)abort();
        effects->batches=batches;
        effects->batches[effects->batch_count++]=(EffectBatch){.model=model,.color=color};
    }
    EffectBatch *batch=&effects->batches[index];
    if(batch->count==batch->capacity) {
        batch->capacity=batch->capacity ? batch->capacity*2 : 16;
        float (*transforms)[16]=realloc(batch->transforms,batch->capacity*sizeof(*transforms));
        if(!transforms)abort();
        batch->transforms=transforms;
    }
    float yaw=atan2f(axis.z,axis.x);
    float pitch=atan2f(axis.y,sqrtf(axis.x*axis.x+axis.z*axis.z));
    Matrix transform=MatrixMultiply(MatrixScale(size.x,size.y,size.z),MatrixRotateZ(pitch));
    transform=MatrixMultiply(transform,MatrixRotateY(-yaw));
    transform=MatrixMultiply(transform,MatrixTranslate(position.x,position.y,position.z));
    memcpy(batch->transforms[batch->count++],MatrixToFloat(MatrixMultiply(model.transform,transform)),sizeof(*batch->transforms));
}

static void sprite(Camera3D camera, Texture2D texture, Vec3 position, float scale, Color color) {
    Vector2 size = {(float)texture.width / SRC_ART_SCALE * scale,
        (float)texture.height / SRC_ART_SCALE * scale};
    DrawBillboardPro(camera, texture, (Rectangle){0, 0, (float)texture.width, (float)texture.height},
        (Vector3){position.x, position.y, position.z}, camera.up, size,
        (Vector2){size.x * .5f, size.y * .5f}, 0, color);
}

void effects_draw(Effects *effects, const Game *game, Camera3D camera, float alpha, double visual_tick,
    const uint32_t held[ACTOR_COUNT]) {
    for(size_t i=0;i<effects->batch_count;++i)effects->batches[i].count=0;
    const Color flag_colors[]={{255,30,20,255},{35,55,255,255},{255,235,30,255}};
    for (int i=0;i<3;++i) {
        const Flag *flag=&game->flags[i];
        if (flag->state==FLAG_ABSENT) continue;
        if (flag->state==FLAG_DROPPED && flag->ticks<300 && flag->ticks%6<3) continue;
        Vec3 base=sub(add(flag->previous,scale(sub(flag->position,flag->previous),alpha)),v3(0,8,0));
        float yaw=i==0 ? 0 : PI;
        if (flag->state==FLAG_CARRIED) {
            const Actor *carrier=&game->actors[flag->carrier];
            base=add(carrier->previous,scale(sub(carrier->position,carrier->previous),alpha));
            base=add(base,add(v3(0,5,0),scale(direction(carrier->yaw,0),-3)));
            yaw=carrier->yaw+PI*.5f;
        }
        solid(effects,effects->flag_pole,add(base,v3(0,10,0)),v3(0,1,0),v3(1,1,1),WHITE);
        rlPushMatrix();
        rlTranslatef(base.x,base.y,base.z);
        rlRotatef(yaw*RAD2DEG,0,1,0);
        rlSetTexture(effects->flag_cloth[game->mode==MODE_INF].id);
        rlBegin(RL_QUADS);
        Color color=flag_colors[i];
        if (game->mode==MODE_INF) color=i==0 ? WHITE : (Color){50,50,50,255};
        rlColor4ub(color.r,color.g,color.b,255);
        for (int strip=0;strip<8;++strip) {
            float left=(float)strip/8,right=(float)(strip+1)/8;
            float phase=(float)visual_tick*.12f;
            float z0=sinf(phase-left*4)*left*1.3f,z1=sinf(phase-right*4)*right*1.3f;
            for (int side=-1;side<=1;side+=2) {
                rlNormal3f(0,0,(float)side);
                rlTexCoord2f(left,1);rlVertex3f(left*12,10,z0+side*.08f);
                rlTexCoord2f(right,1);rlVertex3f(right*12,10,z1+side*.08f);
                rlTexCoord2f(right,0);rlVertex3f(right*12,19,z1+side*.08f);
                rlTexCoord2f(left,0);rlVertex3f(left*12,19,z0+side*.08f);
            }
        }
        rlEnd();rlSetTexture(0);rlPopMatrix();
        if (flag->state==FLAG_BASE)
            DrawCylinder((Vector3){flag->base.x,flag->base.y-8.1f,flag->base.z},6,6,.2f,20,Fade(color,.65f));
    }
    gostek_begin();
    for (size_t i = 0; i < game->pickup_count; ++i) {
        const Pickup *item = &game->pickups[i];
        if ((item->kind==PICKUP_WEAPON || item->kind==PICKUP_BOW) && item->ticks<300 && item->ticks%6<3) continue;
        Vec3 position = add(item->previous, scale(sub(item->position, item->previous), alpha));
        if (item->kind == PICKUP_WEAPON || item->kind == PICKUP_BOW)
            gostek_draw_weapon(item->weapon, position, item->yaw);
        else
            DrawModelEx(effects->kits[item->kind - PICKUP_HEALTH],
                (Vector3){position.x,position.y,position.z}, (Vector3){0,1,0}, item->yaw*RAD2DEG,
                (Vector3){1,1,1}, WHITE);
    }
    for (size_t i = 0; i < game->projectile_count; ++i) {
        const Projectile *p = &game->projectiles[i];
        Vec3 position = add(p->previous, scale(sub(p->position, p->previous), alpha));
        if (p->weapon == FRAGGRENADE)
            solid(effects,effects->grenade, position, v3(0, 1, 0), v3(1, 1, 1), WHITE);
        else if (p->weapon == CLUSTERGRENADE)
            solid(effects,effects->cluster_grenade, position, v3(0, 1, 0), v3(1, 1, 1), WHITE);
        else if (p->weapon == CLUSTER)
            solid(effects,effects->cluster, position, p->velocity, v3(1, 1, 1), WHITE);
        else if (p->weapon == THROWNKNIFE)
            gostek_draw_weapon(THROWNKNIFE, position, atan2f(p->velocity.x,p->velocity.z));
        else if (p->weapon == BOW || p->weapon == BOW2)
            solid(effects,effects->arrow, position, p->velocity, v3(1,1,1), p->weapon==BOW2?(Color){255,155,55,255}:WHITE);
        else if (p->weapon == FLAMER)
            solid(effects,effects->flames[(SRC_FLAMER_TIMEOUT-1-p->ticks)/2], position, v3(0,1,0), v3(1,1,1), WHITE);
        else if (p->weapon == LAW)
            solid(effects,effects->missile, position, p->velocity, v3(1, 1, 1), WHITE);
        else if (p->weapon == M79) {
            float angle = (p->ticks - alpha) * 6 * DEG2RAD;
            solid(effects,effects->bullets[M79], position, v3(cosf(angle), sinf(angle), 0), v3(1, 1, 1), WHITE);
        } else if (p->weapon <= COLT) {
            float trail = p->weapon == SPAS12 ? 1 : length(p->velocity) / SRC_BULLETTRAIL;
            solid(effects,effects->bullets[p->weapon], position, p->velocity, v3(trail, 1, 1), WHITE);
        }
    }
    gostek_end();
    for (size_t i = 0; i < effects->count; ++i) {
        const VisualEvent *fx = &effects->events[i];
        float age = (float)(visual_tick - fx->tick);
        const GameEvent *event = &fx->event;
        if ((event->kind == EVENT_HIT || event->kind == EVENT_KILL) && age < 40) {
            int count = event->kind == EVENT_KILL ? 16 : 6;
            for (int j = 0; j < count; ++j) {
                float a = (float)j * 2.39996f + (float)fx->tick;
                Vec3 velocity = v3(cosf(a) * 1.1f, 1 + sinf(a * 3.1f) * .7f, sinf(a) * 1.1f);
                Vec3 position = add(add(event->position, v3(0, 8, 0)),
                    add(scale(velocity, age), v3(0, -SRC_GRAV * age * age * .5f, 0)));
                float size = (j % 3 ? .08f : .16f) * (1 - age / 50);
                solid(effects,effects->blood, position, velocity, v3(size * 2, size, size), WHITE);
            }
        } else if (event->kind == EVENT_IMPACT && age < 20) {
            for (int j = 0; j < 5; ++j) {
                Vec3 velocity = v3(sinf((float)j * 3.7f) * .35f, .25f, sinf((float)j * 8.1f) * .35f);
                Vec3 position = add(event->position, add(scale(velocity, age), v3(0, -SRC_GRAV * age * age * .5f, 0)));
                float size = .4f * (1 - age / 20);
                solid(effects,effects->spark, position, velocity, v3(size * 2, size, size), (Color){255, 254, 53, 255});
            }
        } else if (event->kind == EVENT_SHOT && event->weapon <= COLT && age < 45) {
            int count = event->weapon == EAGLE ? 2 : 1;
            for (int j = 0; j < count; ++j) {
                float phase = (float)fx->tick + j * PI;
                Vec3 position = add(event->position, v3(sinf(phase) * age * .4f,
                    age * .4f - SRC_GRAV * age * age * .5f, cosf(phase) * age * .4f));
                solid(effects,effects->shells[event->weapon], position,
                    v3(cosf(age * .2f), sinf(age * .2f), sinf(age * .31f)), v3(1, 1, 1), WHITE);
            }
        } else if (event->kind == EVENT_EXPLOSION && event->weapon != CLUSTERGRENADE && age < 12) {
            float size = event->weapon == FRAGGRENADE ? 1 : event->weapon == CLUSTER ? .5f : .75f;
            float radius = (2 + age * 1.3f) * (1 - age / 12) * size;
            Vector3 center = {event->position.x, event->position.y + radius * .35f, event->position.z};
            DrawSphereEx(center, radius, 8, 12, (Color){255, (unsigned char)(210 - age * 8), 47, 255});
            DrawSphereEx((Vector3){center.x, center.y + radius * .2f, center.z}, radius * .6f,
                6, 10, (Color){255, 224, 145, 255});
        }
    }
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        const Actor *actor = &game->actors[i];
        if (actor->life==DEAD && actor->ragdoll.ticks<SRC_NOBLEED_TIME) {
            Vec3 wounds[10];
            int count=ragdoll_wounds(actor,wounds);
            for (int wound=0;wound<count;++wound) {
                for (int drop=0;drop<3;++drop) {
                    float age=fmodf((float)visual_tick+drop*7+wound*3,actor->ragdoll.ticks<SRC_LESSBLEED_TIME?21:42);
                    if (age>21) continue;
                    Vec3 velocity=v3(sinf(wound*3.7f)*.15f,.12f,cosf(wound*2.1f)*.15f);
                    Vec3 position=add(wounds[wound],add(scale(velocity,age),v3(0,-SRC_GRAV*age*age*.5f,0)));
                    solid(effects,effects->blood,position,velocity,v3(.24f,.12f,.12f),WHITE);
                }
            }
        }
        if (actor->life!=ALIVE) continue;
        Vec3 position = add(actor->previous, scale(sub(actor->position, actor->previous), alpha));
        if (actor->bonus==BONUS_FLAMEGOD || actor->bonus==BONUS_BERSERKER) {
            float age=fmodf((float)visual_tick+i*3,20);
            Vec3 particle=add(position,v3(sinf(age*2)*2,8+age*.1f,cosf(age*2)*2));
            if (actor->bonus==BONUS_FLAMEGOD)
                solid(effects,effects->flames[(int)age%(SRC_FLAMER_TIMEOUT/2)],particle,v3(0,1,0),v3(.35f,.35f,.35f),WHITE);
            else solid(effects,effects->blood,particle,v3(0,-1,0),v3(.25f,.15f,.15f),WHITE);
        }
        if (!(held[i] & INPUT_JETS) || actor->fuel == 0) continue;
        Vec3 right = v3(-cosf(actor->yaw), 0, sinf(actor->yaw));
        for (int foot = -1; foot <= 1; foot += 2) {
            Vec3 nozzle = add(position, scale(right, (float)foot * 1.8f));
            for (int j = 0; j < 4; ++j) {
                float age = fmodf((float)visual_tick + j * 4, 16);
                float size = .35f * (1 - age / 16);
                Vec3 particle = add(nozzle, v3(sinf(age + i) * .15f, -age * .5f, cosf(age + i) * .15f));
                solid(effects,effects->jet, particle, v3(0, -1, 0), v3(size * 1.6f, size, size),
                    (Color){255, 186, 38, 255});
            }
        }
    }
    rlDrawRenderBatchActive();
    size_t instance_count=0;
    for(size_t i=0;i<effects->batch_count;++i)instance_count+=effects->batches[i].count;
    if(instance_count>effects->instance_capacity) {
        if(effects->instance_buffer)rlUnloadVertexBuffer(effects->instance_buffer);
        effects->instance_capacity=instance_count*2;
        effects->instance_data=realloc(effects->instance_data,effects->instance_capacity*sizeof(*effects->instance_data));
        if(!effects->instance_data)abort();
        effects->instance_buffer=rlLoadVertexBuffer(NULL,(int)(effects->instance_capacity*sizeof(*effects->instance_data)),true);
        if(!effects->instance_buffer)abort();
    }
    size_t offset=0;
    for(size_t i=0;i<effects->batch_count;++i) {
        const EffectBatch *batch=&effects->batches[i];
        if(!batch->count)continue;
        memcpy(effects->instance_data+offset,batch->transforms,batch->count*sizeof(*batch->transforms));
        offset+=batch->count;
    }
    if(instance_count)rlUpdateVertexBuffer(effects->instance_buffer,effects->instance_data,(int)(instance_count*sizeof(*effects->instance_data)),0);
    Shader shader=effects->instance_shader;
    rlEnableShader(shader.id);
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP],MatrixMultiply(rlGetMatrixModelview(),rlGetMatrixProjection()));
    int texture_unit=0;
    rlSetUniform(shader.locs[SHADER_LOC_MAP_DIFFUSE],&texture_unit,SHADER_UNIFORM_INT,1);
    rlActiveTextureSlot(0);
    offset=0;
    for(size_t i=0;i<effects->batch_count;++i) {
        const EffectBatch *batch=&effects->batches[i];
        if(!batch->count)continue;
        for(int m=0;m<batch->model.meshCount;++m) {
            Material material=batch->model.materials[batch->model.meshMaterial[m]];
            Color color=ColorTint(material.maps[MATERIAL_MAP_DIFFUSE].color,batch->color);
            float tint[4]={color.r/255.0f,color.g/255.0f,color.b/255.0f,color.a/255.0f};
            rlSetUniform(shader.locs[SHADER_LOC_COLOR_DIFFUSE],tint,SHADER_UNIFORM_VEC4,1);
            if(!rlEnableVertexArray(batch->model.meshes[m].vaoId))abort();
            rlEnableVertexBuffer(effects->instance_buffer);
            for(int column=0;column<4;++column) {
                unsigned location=(unsigned)(shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM]+column);
                rlSetVertexAttribute(location,4,RL_FLOAT,false,sizeof(*effects->instance_data),
                    (int)(offset*sizeof(*effects->instance_data)+column*4*sizeof(float)));
                rlEnableVertexAttribute(location);rlSetVertexAttributeDivisor(location,1);
            }
            rlEnableTexture(material.maps[MATERIAL_MAP_DIFFUSE].texture.id);
            rlDrawVertexArrayInstanced(0,batch->model.meshes[m].vertexCount,(int)batch->count);
        }
        offset+=batch->count;
    }
    rlDisableVertexArray();rlDisableVertexBuffer();rlDisableTexture();rlDisableShader();
    rlDisableDepthMask();
    for (size_t i = 0; i < effects->count; ++i) {
        const VisualEvent *fx = &effects->events[i];
        if (fx->event.kind != EVENT_EXPLOSION) continue;
        float age = (float)(visual_tick - fx->tick);
        if (fx->event.weapon==CLUSTERGRENADE) {
            float life=55-age;
            if (life>0)
                sprite(camera,effects->cluster_smoke,add(fx->event.position,v3(0,age/1.5f,0)),
                    .5f*(.6f+(75/life)/96),(Color){255,255,255,(unsigned char)(2.5f*life)});
            continue;
        }
        float life = SRC_EXPLOSION_ANIMS * 3 - age;
        if (fx->event.weapon==CLUSTER) {
            if (life>0) {
                int frame=(int)fmaxf(0,SRC_EXPLOSION_ANIMS-1-lrintf(life/3));
                sprite(camera,effects->explosion[frame],fx->event.position,.5f,
                    (Color){255,255,255,(unsigned char)(255-2*life)});
            }
            continue;
        }
        float size = fx->event.weapon == FRAGGRENADE ? 1 : .75f;
        if (life > 0) {
            int frame = SRC_EXPLOSION_ANIMS - 1 - (int)lrintf(life / 4);
            unsigned char grey = fx->event.weapon == FRAGGRENADE ? 170 : 173;
            if (frame > 0)
                sprite(camera, effects->explosion[frame - 1], fx->event.position, size, (Color){grey, grey, grey, 100});
            sprite(camera, effects->explosion[frame], fx->event.position, size,
                (Color){255, 255, 255, (unsigned char)(255 - SRC_EXPLOSION_ANIMS * 5 + life)});
        }
        float smoke_life = SRC_SMOKE_ANIMS * 4 + 10 - age;
        if (smoke_life > 0 && smoke_life <= SRC_SMOKE_ANIMS * 4) {
            int frame = SRC_SMOKE_ANIMS - (int)lrintf(smoke_life / 4);
            if (frame > 0)
                sprite(camera, effects->smoke[frame - 1], fx->event.position, 1,
                    (Color){204, 204, 204, (unsigned char)(2 * smoke_life + 10)});
            sprite(camera, effects->smoke[frame], fx->event.position, 1,
                (Color){221, 221, 221, (unsigned char)(3 * smoke_life + 10)});
        }
    }
    rlDrawRenderBatchActive();
    rlEnableDepthMask();
}

void effects_unload(Effects *effects) {
    if(effects->instance_buffer)rlUnloadVertexBuffer(effects->instance_buffer);
    free(effects->instance_data);
    for(size_t i=0;i<effects->batch_count;++i)free(effects->batches[i].transforms);
    free(effects->batches);
    UnloadShader(effects->instance_shader);
    for (int i = 0; i < SRC_EXPLOSION_ANIMS; ++i) UnloadTexture(effects->explosion[i]);
    for (int i = 0; i <= SRC_SMOKE_ANIMS; ++i) UnloadTexture(effects->smoke[i]);
    UnloadTexture(effects->cluster_smoke);
    for (int i = 0; i <= COLT; ++i) {
        UnloadModel(effects->bullets[i]);
        UnloadModel(effects->shells[i]);
    }
    UnloadModel(effects->grenade);
    UnloadModel(effects->missile);
    UnloadModel(effects->blood);
    UnloadModel(effects->spark);
    UnloadModel(effects->jet);
    UnloadModel(effects->cluster_grenade);
    UnloadModel(effects->cluster);
    UnloadModel(effects->arrow);
    UnloadModel(effects->flag_pole);
    for (int i=0;i<2;++i) UnloadTexture(effects->flag_cloth[i]);
    for (int i=0;i<SRC_FLAMER_TIMEOUT/2;++i) UnloadModel(effects->flames[i]);
    for (int i=0;i<7;++i) {
        UnloadModel(effects->kits[i]);
        UnloadTexture(effects->kit_textures[i]);
    }
    free(effects->events);
}
