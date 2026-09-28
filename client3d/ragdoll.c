#include "ragdoll.h"
#include "pose.h"
#include "world.h"
#include "generated_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

RagdollLink *ragdoll_links;
static Vec3 rest_pose[RAGDOLL_PART_COUNT+1];
const float ragdoll_part_radius[RAGDOLL_PART_COUNT+1]={
    0,.8f,.8f,1.4f,1.4f,1.6f,1.6f,0,0,.9f,1.4f,1.4f,2.275f,1.2f,1.2f,.7f,.7f
};

void ragdolls_init(void) {
    const char *path=SOLDAT_ASSET_DIR "/objects/gostek.po";
    FILE *file=fopen(path,"r");
    if (!file) {perror(path);exit(EXIT_FAILURE);}
    for (int i=1;i<=RAGDOLL_PART_COUNT;i++) {
        int number;
        float x,y,z;
        if (fscanf(file," P%d%f%f%f",&number,&x,&y,&z)!=4 || number!=i) abort();
        rest_pose[i]=v3(-x*3/1.2f,z*3,0);
    }
    char token[16];
    if (fscanf(file,"%15s",token)!=1 || strcmp(token,"CONSTRAINTS")!=0) abort();
    ragdoll_links=malloc(sizeof(*ragdoll_links)*RAGDOLL_CONSTRAINT_COUNT);
    if (!ragdoll_links) abort();
    for (int i=0;i<RAGDOLL_CONSTRAINT_COUNT;i++) {
        int a,b;
        if (fscanf(file," P%d P%d",&a,&b)!=2 || a<1 || a>RAGDOLL_PART_COUNT || b<1 || b>RAGDOLL_PART_COUNT) abort();
        ragdoll_links[i]=(RagdollLink){a,b,length(sub(rest_pose[a],rest_pose[b]))};
    }
    if (fscanf(file,"%15s",token)!=1 || strcmp(token,"ENDFILE")!=0) abort();
    if (fclose(file)!=0) {perror(path);exit(EXIT_FAILURE);}
}

void ragdolls_free(void) {free(ragdoll_links);}

void ragdoll_start(Actor *actor) {
    Actor before=*actor;
    Vec3 current[21],previous[21];
    before.life=ALIVE;
    actor_pose(&before,current);
    before.position=actor->previous;
    if (before.animation_tick>1) before.animation_tick--;
    actor_pose(&before,previous);
    Vec3 shoulder=scale(add(current[10],current[11]),.5f),hip=scale(add(current[5],current[6]),.5f);
    Vec3 spine=sub(shoulder,hip);
    current[12]=add(current[9],scale(spine,1.875f/length(spine)));
    shoulder=scale(add(previous[10],previous[11]),.5f);hip=scale(add(previous[5],previous[6]),.5f);
    spine=sub(shoulder,hip);
    previous[12]=add(previous[9],scale(spine,1.875f/length(spine)));
    actor->ragdoll=(Ragdoll){0};
    for (int i=1;i<=20;i++) {
        actor->ragdoll.position[i]=current[i];
        actor->ragdoll.old_position[i]=sub(current[i],add(actor->velocity,sub(sub(current[i],previous[i]),sub(actor->position,actor->previous))));
        actor->ragdoll.previous[i]=current[i];
    }
    for (int i=21;i<=RAGDOLL_PART_COUNT;i++) {
        int anchor=i<23?9:12;
        Vec3 position=add(current[anchor],v3(0,rest_pose[i].y-rest_pose[i%2==0?i-1:i].y,0));
        actor->ragdoll.position[i]=position;
        actor->ragdoll.old_position[i]=sub(position,actor->velocity);
        actor->ragdoll.previous[i]=position;
    }
}

void ragdoll_hit(Actor *actor,int bone,Vec3 impulse) {
    actor->ragdoll.old_position[bone]=sub(actor->ragdoll.old_position[bone],impulse);
}

void ragdoll_explosion(Actor *actor,Vec3 center,float radius) {
    for (int i=1;i<=16;i++) {
        Vec3 toward=sub(center,actor->ragdoll.position[i]);
        float distance=length(toward);
        if (distance<radius)
            actor->ragdoll.old_position[i]=add(actor->ragdoll.old_position[i],scale(toward,SRC_EXPLOSION_DEADIMPACT_MULTIPLY/(distance+1)));
    }
}

void ragdoll_dismember(Actor *actor,int bone) {
    if (actor->health<=SRC_BRUTALDEATHHEALTH) actor->ragdoll.severed|=(1u<<1)|(1u<<3)|(1u<<19)|(1u<<20)|(1u<<22);
    else if (actor->health<=SRC_HEADCHOPDEATHHEALTH) {
        if (bone==12) actor->ragdoll.severed|=1u<<19;
        if (bone==3) actor->ragdoll.severed|=1u<<1;
        if (bone==4) actor->ragdoll.severed|=1u<<3;
    }
}

int ragdoll_wounds(const Actor *actor,Vec3 positions[10]) {
    int count=0;
    for (int i=0;i<RAGDOLL_CONSTRAINT_COUNT;i++) if (actor->ragdoll.severed&(1u<<i)) {
        positions[count++]=actor->ragdoll.position[ragdoll_links[i].a];
        positions[count++]=actor->ragdoll.position[ragdoll_links[i].b];
    }
    return count;
}

void ragdoll_step(Actor *actor) {
    Ragdoll *body=&actor->ragdoll;
    for (int i=1;i<=RAGDOLL_PART_COUNT;i++) {
        body->previous[i]=body->position[i];
        Vec3 velocity=scale(sub(body->position[i],body->old_position[i]),SRC_RAGDOLL_DAMPING);
        body->old_position[i]=body->position[i];
        body->position[i]=add(add(body->position[i],velocity),v3(0,-SRC_RAGDOLL_GRAVITY,0));
    }
    body->position[21]=body->position[9];
    body->position[23]=body->position[12];
    for (int i=0;i<RAGDOLL_CONSTRAINT_COUNT;i++) {
        if (body->severed&(1u<<i)) continue;
        RagdollLink link=ragdoll_links[i];
        Vec3 delta=sub(body->position[link.b],body->position[link.a]);
        float distance=length(delta);
        if (distance==0) continue;
        Vec3 correction=scale(delta,.5f*(distance-link.length)/distance);
        body->position[link.a]=add(body->position[link.a],correction);
        body->position[link.b]=sub(body->position[link.b],correction);
    }
    actor->contact=AIRBORNE;
    for (int i=1;i<=16;i++) {
        if (i==7 || i==8) continue;
        float radius=ragdoll_part_radius[i];
        Vec3 extents=v3(radius,radius,radius);
        WorldHit hit=world_trace(body->previous[i],body->position[i],extents);
        if (hit.box<0) continue;
        Vec3 intended=body->position[i];
        Vec3 position=add(body->previous[i],scale(sub(body->position[i],body->previous[i]),hit.fraction));
        if (hit.fraction==0) {
            const WorldSolid *solid=&world_solids[hit.box];
            float support=-FLT_MAX;
            for (unsigned vertex=0;vertex<solid->vertex_count;vertex++)
                support=fmaxf(support,dot(solid->vertices[vertex],hit.normal));
            support+=radius*(fabsf(hit.normal.x)+fabsf(hit.normal.y)+fabsf(hit.normal.z));
            position=add(position,scale(hit.normal,fmaxf(0,support-dot(position,hit.normal))));
        }
        body->position[i]=add(position,scale(hit.normal,.001f));
        float penetration=fmaxf(0,dot(sub(body->position[i],intended),hit.normal));
        body->old_position[i]=sub(body->position[i],scale(hit.normal,penetration));
        if (hit.normal.y>.5f) actor->contact=GROUNDED;
    }
    actor->previous=actor->position;
    actor->position=body->position[12];
    actor->velocity=sub(actor->position,body->previous[12]);
    actor->force=v3(0,0,0);
    body->ticks++;
}
