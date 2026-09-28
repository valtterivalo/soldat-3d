#ifndef SOLDAT3D_SHOWCASE_H
#define SOLDAT3D_SHOWCASE_H

#include "game.h"

extern const char *const showcase_presets[];
extern const size_t showcase_preset_count;
void showcase_init(Game *game,const char *preset,uint32_t seed,int actors);

#endif
