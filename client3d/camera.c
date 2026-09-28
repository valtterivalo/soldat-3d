#include "camera.h"
#include "world.h"
#include "generated_rules.h"

float camera_focus_fov(const Actor *actor,CameraFocus focus)
{
    const WeaponState *weapon=&actor->slots[actor->active_slot];
    if(focus==FOCUS_NONE || actor->life!=ALIVE || actor->contact!=GROUNDED ||
        actor->pose==STANDING || weapon->id!=BARRETT || weapon->phase!=WEAPON_READY ||
        !weapon->ammo || weapon->fire_count)return 72;
    if(actor->animation==MOVE_PRONEMOVE || actor->animation==MOVE_GETUP ||
        actor->animation==MOVE_ROLL || actor->animation==MOVE_ROLLBACK ||
        actor->switch_ticks>0 || actor->throw_frame>0 || actor->melee_frame>0 ||
        actor->grenade_charge>0)return 72;
    float ratio=(float)(actor->pose==PRONE ? SRC_SNIPERAIMDIST : SRC_CROUCHAIMDIST)/SRC_DEFAULTAIMDIST;
    return atanf(tanf(72*.00872664626f)*ratio)*114.591559f;
}

ShoulderView camera_view(CameraRig *rig,const Actor *actor,float yaw,float pitch,
    float alpha,float shoulder,float fov,float elapsed)
{
    Vec3 base=add(actor->previous,scale(sub(actor->position,actor->previous),alpha));
    float height=actor_height(actor->pose);
    Vec3 pivot=actor->life==DEAD ? base : add(base,v3(0,fminf(height*.83f,height-2.001f),0));
    Vec3 forward=direction(yaw,pitch),right=v3(-cosf(yaw),0,sinf(yaw));
    Vec3 wanted=add(add(pivot,scale(forward,-48)),add(scale(right,shoulder*8),v3(0,7,0)));
    WorldHit hit=world_trace(pivot,wanted,v3(2,2,2));
    if(hit.box>=0 && fabsf(hit.normal.y)>.5f) {
        Vec3 contact=add(pivot,scale(sub(wanted,pivot),hit.fraction));
        wanted.y=contact.y+hit.normal.y*.01f;
        hit=world_trace(pivot,wanted,v3(2,2,2));
    }
    Vec3 boom=sub(wanted,pivot);
    float span=length(boom);
    float allowed=span*(hit.box>=0 ? fmaxf(0,hit.fraction-.015f) : 1);
    rig->distance=allowed<rig->distance ? allowed : allowed+(rig->distance-allowed)*expf(-20*elapsed);
    Vec3 position=add(pivot,scale(boom,rig->distance/span));
    Vec3 torso=add(base,v3(0,height*.6f,0));
    float apparent_distance=length(sub(position,torso))*tanf(fov*.00872664626f)/tanf(72*.00872664626f);
    float visibility=fmaxf(0,fminf(1,(apparent_distance-22)/14));
    visibility=visibility*visibility*(3-2*visibility);
    return (ShoulderView){position,add(position,scale(forward,400)),fov,visibility};
}
