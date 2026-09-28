#ifndef SOLDAT3D_PICKUPS_H
#define SOLDAT3D_PICKUPS_H

#include "game.h"

void pickups_init(Game *game);
void pickups_step(Game *game);
size_t pickups_spawn(Game *game, PickupKind kind, WeaponId weapon, Vec3 position,
    Vec3 velocity, int ammo, int owner);
void pickups_drop_weapon(Game *game, int actor);
void pickups_weapon(Actor *actor, int slot, WeaponId weapon, int ammo);

#endif
