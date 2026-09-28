#ifndef SOLDAT3D_OBJECTIVES_H
#define SOLDAT3D_OBJECTIVES_H

#include "game.h"

void objectives_init(Game *game);
void objectives_step(Game *game, const Input inputs[ACTOR_COUNT]);
void objectives_drop(Game *game, int actor, int throw_flag);
void objectives_return(Game *game, FlagId flag, int actor, EventKind event);
void objectives_kill(Game *game, int victim, int killer, WeaponId weapon);
Vec3 objectives_target(const Game *game, int actor, Vec3 combat_target);

#endif
