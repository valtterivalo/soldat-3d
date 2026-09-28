#ifndef SOLDAT3D_CAMERA_H
#define SOLDAT3D_CAMERA_H

#include "game.h"

typedef struct { float distance; } CameraRig;
typedef enum { FOCUS_NONE, FOCUS_PRECISION } CameraFocus;
typedef struct { Vec3 position, target; float fov, body_visibility; } ShoulderView;
float camera_focus_fov(const Actor *actor,CameraFocus focus);
ShoulderView camera_view(CameraRig *rig,const Actor *actor,float yaw,float pitch,
    float alpha,float shoulder,float fov,float elapsed);

#endif
