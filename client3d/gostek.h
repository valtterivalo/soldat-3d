#ifndef SOLDAT3D_GOSTEK_H
#define SOLDAT3D_GOSTEK_H

#include "game.h"

void gostek_init(void);
void gostek_begin(void);
void gostek_draw_shadows(const Game *scene,float alpha);
void gostek_end(void);
void gostek_fire(int id, uint64_t tick);
void gostek_draw(const Actor *actor, int id, uint64_t tick, float alpha, int jetting, float visibility, const Vec3 pose_points[21]);
void gostek_draw_weapon(WeaponId id,Vec3 position,float yaw);
void gostek_free(void);

#endif
