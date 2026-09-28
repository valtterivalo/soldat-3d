#include "camera.h"
#include "pose.h"
#include "world.h"

#include <stdio.h>
#include <stdlib.h>

static void check(int condition,const char *message) {
    if (condition) return;
    fprintf(stderr,"pose: %s\n",message);
    exit(EXIT_FAILURE);
}

static float aim_error(Actor actor,Vec3 target) {
    Vec3 aim=actor_aim_direction(&actor,target);
    actor.yaw=atan2f(aim.x,aim.z);
    actor.pitch=atan2f(aim.y,sqrtf(aim.x*aim.x+aim.z*aim.z));
    Vec3 points[21];
    actor_pose(&actor,points);
    Vec3 delta=sub(target,pose_muzzle(&actor,points));
    return length(sub(delta,scale(aim,dot(delta,aim))));
}

int main(void) {
    world_init();poses_init();
    Game game={0};
    for (int i=0;i<ACTOR_COUNT;++i) game.actors[i].life=INACTIVE;
    Actor *actor=&game.actors[0],*target_actor=&game.actors[1];
    *actor=(Actor){.life=ALIVE,.contact=GROUNDED,.animation_tick=90,.spawn_protection_ticks=-1,
        .position={0,1000,0},.previous={0,1000,0}};
    *target_actor=*actor;
    const float distances[]={50,100,300};
    for (int pose=STANDING;pose<=PRONE;++pose) {
        actor->pose=(Pose)pose;
        actor->animation=pose==STANDING ? MOVE_IDLE : pose==CROUCHING ? MOVE_CROUCH : MOVE_PRONE;
        for (int weapon=EAGLE;weapon<=LAW;++weapon) for (int side=-1;side<=1;side+=2) {
            actor->slots[0].id=(WeaponId)weapon;
            CameraRig rig={48};
            ShoulderView camera=camera_view(&rig,actor,0,0,1,(float)side,72,1);
            for (size_t d=0;d<sizeof(distances)/sizeof(*distances);++d) {
                target_actor->position=v3(camera.position.x,0,distances[d]);
                Vec3 bones[21];actor_pose(target_actor,bones);
                target_actor->position.y=camera.position.y-(bones[10].y+bones[11].y)*.5f;
                Vec3 target=combat_aim_target(&game,0,camera.position,add(camera.position,v3(0,0,1800)),NULL);
                check(aim_error(*actor,target)<.003f,"all gun lengths and stances align with a centered static target");
            }
        }
    }
    actor->position=actor->previous=v3(0,0,0);
    actor->pose=STANDING;actor->animation=MOVE_RUN;actor->velocity=v3(0,0,3);
    for (int frame=1;frame<=37;++frame) for (int angle=880;angle<=910;++angle) {
        actor->animation_tick=frame;
        Vec3 target=add(scale(direction((float)angle*.00174532925f,0),50),v3(0,20,0));
        check(aim_error(*actor,target)<.003f,"sideways aim uses the final facing direction's forward or backward animation");
    }
    actor->velocity=v3(0,0,0);actor->animation=MOVE_IDLE;actor->slots[0].id=BARRETT;
    for (int state=0;state<4;++state) {
        Actor transition=*actor;
        if (state==0) transition.switch_ticks=10;
        if (state==1) transition.throw_frame=5;
        if (state==2) transition.grenade_charge=5;
        if (state==3) transition.slots[0].phase=WEAPON_RELOADING;
        check(aim_error(transition,v3(-8,20,50))<.003f,"pose transitions retain their actual hand anchor");
    }
    size_t pulled=0;
    for (size_t node=0;node<world_nav_node_count;++node) for (int angle=0;angle<16;++angle) {
        actor->position=actor->previous=world_nav_nodes[node].position;
        actor->yaw=(float)angle*.3926990817f;actor->pitch=0;
        CameraRig rig={48};
        ShoulderView camera=camera_view(&rig,actor,actor->yaw,0,1,1,72,1);
        Vec3 end=add(camera.position,scale(direction(actor->yaw,0),1800));
        WorldHit hit=world_trace(camera.position,end,v3(0,0,0));
        Vec3 target=add(camera.position,scale(sub(end,camera.position),hit.fraction));
        if (rig.distance>=30 || length(sub(target,actor->position))<40) continue;
        check(aim_error(*actor,target)<.003f,"camera wall pull-in does not move the barrel off its reticle target");
        ++pulled;
    }
    check(pulled>0,"wall regression exercises actual retracted camera views");
    Actor before=*actor,after=*actor;
    before.animation=after.animation=MOVE_RUN;before.animation_tick=1;after.animation_tick=4;
    after.position=add(before.position,v3(5,2,-3));
    Vec3 a[21],b[21],between[21];
    actor_pose(&before,a);actor_pose(&after,b);actor_pose_between(&before,&after,.5f,between);
    for (int bone=1;bone<=20;++bone)
        check(length(sub(between[bone],scale(add(a[bone],b[bone]),.5f)))<.001f,
            "presentation interpolates every bone between authoritative poses");
    ++after.spawn_id;actor_pose_between(&before,&after,.5f,between);
    for (int bone=1;bone<=20;++bone) check(length(sub(between[bone],b[bone]))==0,
        "presentation never blends bones across respawn generations");
    poses_free();world_free();
    printf("Pose: analytic gun convergence, animation boundaries and %zu retracted camera views passed\n",pulled);
}
