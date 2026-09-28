#include "camera.h"
#include "world.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>

int main(void)
{
    world_init();
    Actor actor={.position={0,30,0},.previous={0,30,0},.pose=STANDING,.life=ALIVE};
    Vec3 location=world_nav_nodes[0].position;
    actor.position=actor.previous=location;
    CameraRig rig={48};
    ShoulderView view=camera_view(&rig,&actor,0,0,1,1,72,1);
    Vec3 forward=sub(view.target,view.position);
    assert(fabsf(forward.x)<.001f && fabsf(forward.y)<.001f && fabsf(forward.z-400)<.001f);
    float height=actor_height(actor.pose);
    Vec3 pivot=add(location,v3(0,fminf(height*.83f,height-2.001f),0));
    assert(world_trace(pivot,view.position,v3(1.99f,1.99f,1.99f)).box<0);
    size_t near_walls=0;
    for(size_t node=0;node<world_nav_node_count;++node)for(unsigned angle=0;angle<16;++angle) {
        actor.position=actor.previous=world_nav_nodes[node].position;
        float yaw=(float)angle*.39269908f;
        rig.distance=48;
        view=camera_view(&rig,&actor,yaw,0,1,1,72,1);
        Vec3 base=add(actor.position,v3(0,height*.6f,0));
        if(length(sub(view.position,base))<=22) { assert(view.body_visibility==0);++near_walls; }
        assert(view.body_visibility>=0 && view.body_visibility<=1);
        CameraRig zoom_rig=rig;
        ShoulderView zoom=camera_view(&zoom_rig,&actor,yaw,0,1,1,28,1);
        assert(zoom.body_visibility<=view.body_visibility);
    }
    assert(near_walls>0);
    actor.contact=GROUNDED;
    actor.pose=PRONE;
    actor.animation=MOVE_PRONE;
    actor.slots[0]=(WeaponState){.id=BARRETT,.ammo=10,.phase=WEAPON_READY};
    float prone_fov=camera_focus_fov(&actor,FOCUS_PRECISION);
    assert(fabsf(tanf(prone_fov*.00872664626f)/tanf(72*.00872664626f)-.5f)<.0001f);
    actor.pose=CROUCHING;
    actor.animation=MOVE_CROUCH;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)>prone_fov);
    Actor ready=actor;
    actor.switch_ticks=1;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor=ready;actor.throw_frame=1;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor=ready;actor.grenade_charge=1;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor=ready;actor.melee_frame=1;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor=ready;actor.slots[0].phase=WEAPON_RELOADING;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    const MoveAnimation transitions[]={MOVE_GETUP,MOVE_ROLL,MOVE_ROLLBACK,MOVE_PRONEMOVE};
    for(size_t i=0;i<sizeof(transitions)/sizeof(*transitions);++i) {
        actor=ready;actor.animation=transitions[i];
        assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    }
    actor=ready;
    actor.slots[0].fire_count=1;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor.slots[0].fire_count=0;
    actor.contact=AIRBORNE;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor.contact=GROUNDED;
    actor.slots[0].id=AK74;
    assert(camera_focus_fov(&actor,FOCUS_PRECISION)==72);
    actor.pose=STANDING;
    actor.position=actor.previous=v3(0,world_bounds.max.y-120,0);
    CameraRig whole={8},halves=whole;
    ShoulderView once=camera_view(&whole,&actor,0,0,1,1,72,.04f);
    camera_view(&halves,&actor,0,0,1,1,72,.02f);
    ShoulderView twice=camera_view(&halves,&actor,0,0,1,1,72,.02f);
    assert(length(sub(once.position,twice.position))<.0001f);
    world_free();
    printf("Camera: %zu wall views clear the body, collision and frame-independent recovery passed\n",near_walls);
}
