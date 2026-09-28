#ifndef SOLDAT3D_RAGDOLL_H
#define SOLDAT3D_RAGDOLL_H

#include "game.h"

typedef struct { int a,b; float length; } RagdollLink;
extern RagdollLink *ragdoll_links;
extern const float ragdoll_part_radius[RAGDOLL_PART_COUNT+1];
void ragdolls_init(void);
void ragdolls_free(void);
void ragdoll_start(Actor *actor);
void ragdoll_step(Actor *actor);
void ragdoll_hit(Actor *actor,int bone,Vec3 impulse);
void ragdoll_explosion(Actor *actor,Vec3 center,float radius);
void ragdoll_dismember(Actor *actor,int bone);
int ragdoll_wounds(const Actor *actor,Vec3 positions[10]);

#endif
