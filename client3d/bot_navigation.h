#ifndef SOLDAT3D_BOT_NAVIGATION_H
#define SOLDAT3D_BOT_NAVIGATION_H

#include "game.h"

typedef enum { BOT_HOLD, BOT_ENGAGE, BOT_TRAVERSE, BOT_RETREAT } BotMoveIntent;
void bot_navigation(Game *game, int index, Vec3 destination, BotMoveIntent intent, Input *input);

#endif
