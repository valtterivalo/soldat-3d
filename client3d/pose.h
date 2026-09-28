#ifndef SOLDAT3D_POSE_H
#define SOLDAT3D_POSE_H

#include "game.h"

#define ACTOR_FOREARM_RADIUS 1.2f
#define ACTOR_FOREARM_OVERLAP .65f
#define ACTOR_HAND_RADIUS .72f
#define ACTOR_HAND_BACK .6f
#define ACTOR_HAND_FRONT 1.1f

typedef struct {const char *file;float cx,cy;int width,height;} WeaponVisual;
extern WeaponVisual weapon_visuals[WEAPON_COUNT];

void poses_init(void);
void poses_free(void);
void actor_pose(const Actor *actor,Vec3 points[21]);
void actor_pose_between(const Actor *before,const Actor *after,float fraction,Vec3 points[21]);
Vec3 pose_muzzle(const Actor *actor,const Vec3 points[21]);
Vec3 actor_aim_direction(const Actor *actor,Vec3 target);

#endif
