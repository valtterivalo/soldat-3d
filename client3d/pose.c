#include "pose.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x,y; } PosePoint;
typedef enum {
    STAND_ANIM, RUN_ANIM, RUNBACK_ANIM, JUMP_ANIM, SIDEJUMP_ANIM, FALL_ANIM,
    CROUCH_ANIM, CROUCHRUN_ANIM, CROUCHBACK_ANIM, PRONE_ANIM, CRAWL_ANIM,
    GETUP_ANIM, ROLL_ANIM, ROLLBACK_ANIM, AIM_ANIM, RELOAD_ANIM, CHANGE_ANIM,
    PUNCH_ANIM, THROW_ANIM, THROWWEAPON_ANIM, CLIPIN_ANIM, CLIPOUT_ANIM, SLIDEBACK_ANIM, UNARMED_ANIM,
    ANIMATION_COUNT
} AnimationId;
typedef struct { const char *file; int speed, frames; PosePoint (*pose)[21]; } Animation;
static Animation animations[ANIMATION_COUNT] = {
    [STAND_ANIM] = {.file="stoi", .speed=3},
    [RUN_ANIM] = {.file="biega", .speed=1},
    [RUNBACK_ANIM] = {.file="biegatyl", .speed=1},
    [JUMP_ANIM] = {.file="skok", .speed=1},
    [SIDEJUMP_ANIM] = {.file="skokwbok", .speed=1},
    [FALL_ANIM] = {.file="spada", .speed=1},
    [CROUCH_ANIM] = {.file="kuca", .speed=1},
    [CROUCHRUN_ANIM] = {.file="kucaidzie", .speed=2},
    [CROUCHBACK_ANIM] = {.file="kucaidzietyl", .speed=2},
    [PRONE_ANIM] = {.file="lezy", .speed=1},
    [CRAWL_ANIM] = {.file="lezyidzie", .speed=2},
    [GETUP_ANIM] = {.file="wstaje", .speed=1},
    [ROLL_ANIM] = {.file="skokdolobrot", .speed=1},
    [ROLLBACK_ANIM] = {.file="skokdolobrottyl", .speed=1},
    [AIM_ANIM] = {.file="celuje", .speed=2},
    [RELOAD_ANIM] = {.file="laduje", .speed=2},
    [CHANGE_ANIM] = {.file="change", .speed=1},
    [PUNCH_ANIM] = {.file="bije", .speed=1},
    [THROW_ANIM] = {.file="rzuca", .speed=1},
    [THROWWEAPON_ANIM] = {.file="wyrzuca", .speed=1},
    [CLIPIN_ANIM] = {.file="clipin", .speed=3},
    [CLIPOUT_ANIM] = {.file="clipout", .speed=3},
    [SLIDEBACK_ANIM] = {.file="slideback", .speed=2},
    [UNARMED_ANIM] = {.file="bezbroni", .speed=3}
};

WeaponVisual weapon_visuals[WEAPON_COUNT] = {
    [EAGLE]={"deserteagle",.1f,.8f,0,0},[MP5]={"mp5",.15f,.6f,0,0},
    [AK74]={"ak74",.15f,.5f,0,0},[STEYRAUG]={"steyraug",.2f,.6f,0,0},
    [SPAS12]={"spas12",.1f,.6f,0,0},[RUGER77]={"ruger77",.1f,.7f,0,0},
    [M79]={"m79",.1f,.7f,0,0},[BARRETT]={"barretm82",.15f,.7f,0,0},
    [M249]={"m249",.15f,.6f,0,0},[MINIGUN]={"minigun",.05f,.5f,0,0},
    [COLT]={"colt1911",.2f,.55f,0,0},[KNIFE]={"knife",-.1f,.6f,0,0},
    [CHAINSAW]={"chainsaw",.1f,.5f,0,0},[LAW]={"law",.1f,.6f,0,0},
    [BOW2]={"bow",-.4f,.55f,0,0},[BOW]={"bow",-.4f,.55f,0,0},
    [FLAMER]={"flamer",.2f,.7f,0,0},[M2]={"m2",.1f,.6f,0,0}
};

void poses_init(void) {
    for (int i = 0; i < ANIMATION_COUNT; i++) {
        Animation *animation = &animations[i];
        char path[sizeof(SOLDAT_ASSET_DIR) + 64];
        int written = snprintf(path, sizeof(path), SOLDAT_ASSET_DIR "/anims/%s.poa", animation->file);
        if (written < 0 || (size_t)written >= sizeof(path)) abort();
        FILE *file = fopen(path, "r");
        if (!file) { perror(path); exit(EXIT_FAILURE); }
        animation->pose=malloc(sizeof(*animation->pose));
        if (!animation->pose) abort();
        animation->frames = 1;
        uint32_t points = 0;
        for (;;) {
            char token[16];
            if (fscanf(file, "%15s", token) != 1) abort();
            if (strcmp(token, "ENDFILE") == 0) {
                if (points != 0xfffffu) abort();
                break;
            }
            if (strcmp(token, "NEXTFRAME") == 0) {
                if (points != 0xfffffu) abort();
                animation->frames++;
                PosePoint (*grown)[21]=realloc(animation->pose,sizeof(*animation->pose)*(size_t)animation->frames);
                if (!grown) abort();
                animation->pose=grown;
                points = 0;
                continue;
            }
            int point = atoi(token);
            float x, y, z;
            if (point < 1 || point > 20 || fscanf(file, "%f%f%f", &x, &y, &z) != 3) abort();
            animation->pose[animation->frames - 1][point] = (PosePoint){-3 * x / 1.1f, -3 * z};
            points |= 1u << (point - 1);
        }
        if (fclose(file)!=0) {perror(path);exit(EXIT_FAILURE);}
    }
    for (int i=0;i<WEAPON_COUNT;i++) {
        WeaponVisual *visual=&weapon_visuals[i];
        if (!visual->file) continue;
        char path[sizeof(SOLDAT_ASSET_DIR)+128];
        int written=snprintf(path,sizeof(path),SOLDAT_ASSET_DIR "/weapons-gfx/%s.png",visual->file);
        if (written<0 || (size_t)written>=sizeof(path)) abort();
        FILE *file=fopen(path,"rb");
        if (!file) {perror(path);exit(EXIT_FAILURE);}
        unsigned char header[24];
        if (fread(header,1,sizeof(header),file)!=sizeof(header) || memcmp(header,"\x89PNG\r\n\x1a\n",8)!=0) abort();
        if (fclose(file)!=0) {perror(path);exit(EXIT_FAILURE);}
        visual->width=(int)((uint32_t)header[16]<<24 | (uint32_t)header[17]<<16 | (uint32_t)header[18]<<8 | header[19]);
        visual->height=(int)((uint32_t)header[20]<<24 | (uint32_t)header[21]<<16 | (uint32_t)header[22]<<8 | header[23]);
    }
}

void poses_free(void) {
    for (int i=0;i<ANIMATION_COUNT;i++) free(animations[i].pose);
}

static Vec3 pose_points(const Actor *actor,Vec3 points[21]) {
    if (actor->life==DEAD) {
        memcpy(points,actor->ragdoll.position,sizeof(*points)*21);
        return points[16];
    }
    Vec3 heading=direction(actor->yaw,0);
    int backwards = dot(actor->velocity, heading) < 0;
    int moving = actor->velocity.x != 0 || actor->velocity.z != 0;
    AnimationId legs = STAND_ANIM;
    switch (actor->animation) {
        case MOVE_IDLE: legs = actor->contact == AIRBORNE ? FALL_ANIM : STAND_ANIM; break;
        case MOVE_RUN: legs = backwards ? RUNBACK_ANIM : RUN_ANIM; break;
        case MOVE_JUMP: legs = JUMP_ANIM; break;
        case MOVE_SIDEJUMP: legs = SIDEJUMP_ANIM; break;
        case MOVE_ROLL: legs = ROLL_ANIM; break;
        case MOVE_ROLLBACK: legs = ROLLBACK_ANIM; break;
        case MOVE_CROUCH: legs = moving ? (backwards ? CROUCHBACK_ANIM : CROUCHRUN_ANIM) : CROUCH_ANIM; break;
        case MOVE_PRONE: legs = PRONE_ANIM; break;
        case MOVE_PRONEMOVE: legs = CRAWL_ANIM; break;
        case MOVE_GETUP: legs = GETUP_ANIM; break;
    }
    int leg_frame = (actor->animation_tick > 0 ? actor->animation_tick - 1 : 0) / animations[legs].speed;
    if (legs == RUN_ANIM || legs == RUNBACK_ANIM || legs == CROUCHRUN_ANIM ||
        legs == CROUCHBACK_ANIM || legs == CRAWL_ANIM || legs == STAND_ANIM)
        leg_frame %= animations[legs].frames;
    else if (leg_frame >= animations[legs].frames) leg_frame = animations[legs].frames - 1;

    const WeaponState *weapon = &actor->slots[actor->active_slot];
    const WeaponDef *definition = &weapons[weapon->id];
    AnimationId body = weapon->id == NOWEAPON ? UNARMED_ANIM :
        (actor->pose == CROUCHING ? AIM_ANIM : STAND_ANIM);
    int body_frame = (actor->animation_tick > 0 ? actor->animation_tick-1:0) / animations[body].speed % animations[body].frames;
    if (legs == ROLL_ANIM || legs == ROLLBACK_ANIM || legs == PRONE_ANIM ||
        legs == CRAWL_ANIM || legs == GETUP_ANIM) {
        body = legs;
        body_frame = leg_frame;
    } else if (actor->switch_ticks > 0) {
        body = CHANGE_ANIM;
        body_frame = actor->switch_ticks - 1;
    } else if (actor->melee_frame > 0) {
        body = PUNCH_ANIM;
        body_frame = actor->melee_frame - 1;
    } else if (actor->throw_frame > 0) {
        body = THROWWEAPON_ANIM;
        body_frame = (actor->throw_frame - 1) / (weapon->id == KNIFE ? 2 : 1);
    } else if (actor->grenade_charge > 0) {
        body = THROW_ANIM;
        body_frame = actor->grenade_charge - 1;
    } else if (weapon->phase == WEAPON_RELOADING) {
        if (!definition->clip_reload) {
            body = RELOAD_ANIM;
            body_frame = (26 - weapon->reload_count) / 2;
        } else if (weapon->reload_count > definition->reload_time * .8f) {
            body = CLIPOUT_ANIM;
            body_frame = (definition->reload_time - weapon->reload_count) / 3;
        } else if (weapon->reload_count > definition->reload_time * .3f) {
            body = CLIPIN_ANIM;
            body_frame = ((int)(definition->reload_time * .8f) - weapon->reload_count) / 3;
        } else {
            body = SLIDEBACK_ANIM;
            body_frame = ((int)(definition->reload_time * .3f) - weapon->reload_count) / 2;
        }
    }
    if (body_frame >= animations[body].frames) body_frame = animations[body].frames - 1;
    if (body_frame < 0) body_frame = 0;
    PosePoint pose[21];
    float body_y = actor->pose == CROUCHING ? 9 : 8;
    if (body == PRONE_ANIM) body_y = body_frame > 8 ? -2 : 13 - (float)body_frame;
    if (body == CRAWL_ANIM) body_y = 0;
    if (body == GETUP_ANIM) body_y = body_frame > 17 ? 8 : 4;
    for (int i = 1; i <= 20; i++) {
        if (i <= 6 || i == 17 || i == 18) {
            pose[i] = animations[legs].pose[leg_frame][i];
        } else {
            pose[i] = animations[body].pose[body_frame][i];
            pose[i].y += animations[legs].pose[leg_frame][6].y + body_y;
        }
    }
    Vec3 right=v3(-heading.z,0,heading.x);
    points[0]=v3(0,0,0);
    for (int i=1;i<=20;i++) {
        points[i]=add(actor->position,scale(heading,pose[i].x));
        points[i].y+=1-pose[i].y;
    }
    Vec3 hip=scale(add(points[5],points[6]),.5f);
    Vec3 shoulder=scale(add(points[10],points[11]),.5f);
    Vec3 spine=sub(shoulder,hip);
    spine=scale(spine,1/length(spine));
    points[5]=add(hip,scale(right,1.45f));
    points[6]=add(hip,scale(right,-1.45f));
    points[10]=add(shoulder,scale(right,2.2f));
    points[11]=add(shoulder,scale(right,-2.2f));
    for (int i=1;i<=4;i++) points[i]=add(points[i],scale(right,(i==1 || i==4)?1.45f:-1.45f));
    for (int i=13;i<=20;i++) if (i!=17 && i!=18)
        points[i]=add(points[i],scale(right,(i==13 || i==16 || i==20)?2.2f:-2.2f));
    Vec3 hand_anchor=points[16];
    if (actor->life==ALIVE) {
        points[12]=add(points[9],add(scale(heading,-.1f*sinf(actor->pitch)),v3(0,.1f*cosf(actor->pitch),0)));
        if (body==STAND_ANIM || body==AIM_ANIM || body==PRONE_ANIM || body==CRAWL_ANIM) {
            Vec3 aim=direction(actor->yaw,actor->pitch);
            Vec3 offset=add(scale(spine,-2.8f),scale(right,1.25f));
            hand_anchor=add(shoulder,offset);
            points[16]=add(add(shoulder,scale(aim,3)),offset);
            points[15]=add(points[16],scale(aim,5));
            points[13]=add(scale(add(points[10],points[16]),.5f),scale(spine,-1.9f));
            points[14]=add(scale(add(points[11],points[15]),.5f),scale(spine,-2));
        }
    }
    return hand_anchor;
}

void actor_pose(const Actor *actor,Vec3 points[21]) { (void)pose_points(actor,points); }

void actor_pose_between(const Actor *before,const Actor *after,float fraction,Vec3 points[21]) {
    actor_pose(after,points);
    if (before->spawn_id!=after->spawn_id || before->life!=after->life) return;
    Vec3 previous[21];
    actor_pose(before,previous);
    for (int i=1;i<=20;++i) points[i]=add(previous[i],scale(sub(points[i],previous[i]),fraction));
}

Vec3 pose_muzzle(const Actor *actor,const Vec3 points[21]) {
    const WeaponVisual *visual=&weapon_visuals[actor->slots[actor->active_slot].id];
    return add(points[16],scale(direction(actor->yaw,actor->pitch),visual->width/SRC_ART_SCALE*(1-visual->cx)));
}

Vec3 actor_aim_direction(const Actor *actor,Vec3 target) {
    Vec3 delta=sub(target,actor->position);
    float bearing=atan2f(delta.x,delta.z);
    Actor reference=*actor;
    reference.position=v3(0,0,0);
    reference.yaw=bearing;
    reference.pitch=0;
    Vec3 points[21],anchor=pose_points(&reference,points);
    Vec3 right=direction(bearing-1.57079632679f,0);
    float lateral=dot(anchor,right);
    float radius=sqrtf(delta.x*delta.x+delta.z*delta.z);
    float yaw=bearing+asinf(lateral/fmaxf(radius,fabsf(lateral)));
    reference.yaw=yaw;
    anchor=pose_points(&reference,points);
    float forward=fmaxf(0,dot(sub(delta,anchor),direction(yaw,0)));
    return direction(yaw,atan2f(delta.y-anchor.y,forward));
}
