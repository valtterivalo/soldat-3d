#ifndef SOLDAT3D_INTERFACE_H
#define SOLDAT3D_INTERFACE_H

#include "game.h"
#include "raylib.h"
#include "lobby.h"
#include "hit_feedback.h"

typedef enum {
    UI_HEALTH, UI_AMMO, UI_JET, UI_HEALTH_BAR, UI_RELOAD_BAR,
    UI_JET_BAR, UI_FIRE_BAR, UI_FIRE_FRAME, UI_GRENADE,
    UI_MENU_CURSOR, UI_BACK, UI_TITLE_LEFT, UI_TITLE_RIGHT,
    UI_VEST_BAR, UI_CLUSTER_GRENADE,
    UI_FLAG, UI_NOFLAG,
    UI_TEXTURE_COUNT
} InterfaceTexture;

typedef struct {
    Font font;
    Texture2D textures[UI_TEXTURE_COUNT], guns[14];
    float asset_scale, title_scale;
} Interface;

typedef enum { INTERFACE_IDLE, INTERFACE_PLAY } InterfaceAction;

typedef struct {
    unsigned char keys[KEY_KB_MENU + 1];
    unsigned int mouse;
} InputPresses;

Interface interface_load(void);
void interface_unload(Interface *interface);
void interface_hud(const Interface *interface, const Game *game, HitFeedback hit,
    const char *killfeed, int feed_ticks, const char *const names[ACTOR_COUNT], int local_actor, Camera3D camera);
int interface_browser(const Interface *interface, const Lobby *lobby, int *selected, const InputPresses *presses);
InterfaceAction interface_menu(const Interface *interface, WeaponId *primary, WeaponId *secondary,
    const InputPresses *presses);

#endif
