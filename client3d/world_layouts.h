#ifndef SOLDAT3D_WORLD_LAYOUTS_H
#define SOLDAT3D_WORLD_LAYOUTS_H

#include "world.h"

typedef enum { COURTYARD, HALL, ROCK, BRIDGE, PIT } LayoutShape;
typedef struct {
    float x,z,y,width,depth,roof;
    LayoutShape shape;
} LayoutRoom;
typedef struct {
    unsigned from,to;
    NavTravel travel;
    float width;
} LayoutLink;
typedef struct {
    const LayoutRoom *rooms;
    size_t room_count;
    const LayoutLink *links;
    size_t link_count;
    unsigned alpha_room,bravo_room,neutral_room;
} Layout;

typedef struct {
    unsigned room;
    float x,z,width,depth,height;
    unsigned type;
    float bounciness;
} LayoutMaterial;

const Layout *world_layout(const char *name);
const Layout *world_layout_dm(const char *name);
const Layout *world_layout_team(const char *name);

#endif
