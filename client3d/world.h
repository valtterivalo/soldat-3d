#ifndef SOLDAT3D_WORLD_H
#define SOLDAT3D_WORLD_H

#include "game.h"

typedef enum { WORLD_TERRAIN, WORLD_SKY, WORLD_HIDDEN } WorldTexture;
typedef struct {
    Vec3 vertices[16];
    unsigned char color[16][4];
    unsigned vertex_count, face_count, poly_type, visible_faces;
    unsigned char face_size[10], faces[10][8];
    float bounciness;
    WorldTexture texture;
} WorldSolid;

typedef enum { PROP_TERRAIN, PROP_ARCHITECTURE } PropSupport;
typedef struct {
    unsigned active, style;
    int width, height;
    Vec3 position;
    float yaw, scale_x, scale_y;
    unsigned char color[4];
    PropSupport support;
} WorldProp;

typedef enum { NAV_WALK, NAV_JET, NAV_DROP, NAV_JUMP } NavTravel;
typedef struct { Vec3 position; } NavNode;
typedef struct { int from, to; NavTravel mode; float cost; int fuel; float apex; } NavLink;

extern WorldSolid *world_solids;
extern size_t world_solid_count;
extern Box world_bounds;
extern NavNode *world_nav_nodes;
extern NavLink *world_nav_links;
extern size_t world_nav_node_count, world_nav_link_count;
typedef struct { Vec3 position; unsigned type; } WorldSpawn;
typedef enum { WORLD_TRACE_ENVIRONMENT, WORLD_TRACE_ACTOR, WORLD_TRACE_BULLET, WORLD_TRACE_ITEM, WORLD_TRACE_LIGHT, WORLD_TRACE_GROUND } WorldTraceKind;
typedef enum { WORLD_NO_FLAG, WORLD_HAS_FLAG } WorldFlagState;
typedef struct { WorldTraceKind kind; unsigned team; WorldFlagState flag; } WorldQuery;
extern size_t world_gate_count;
int world_nav_link_allows(const NavLink *link,WorldQuery query);

extern const char *const world_map_names[];
extern const size_t world_map_count;
extern size_t world_map_current;
extern char world_texture[64];
extern WorldSpawn *world_source_spawns;
extern size_t world_source_spawn_count;
extern int world_jet_fuel, world_medikits, world_grenades;
size_t world_map_index(const char *name);
void world_load(size_t index);
size_t world_team_spawn_count(unsigned team);
Vec3 world_team_spawn(unsigned team, size_t index);
size_t world_marker_spawn_count(unsigned type);
Vec3 world_marker_spawn(unsigned type,size_t index);
Vec3 world_objective_spawn(unsigned type);
int world_supports_mode(GameMode mode);
int world_pose_clear_for(Vec3 feet, Pose pose, WorldQuery query);
WorldHit world_trace_for(Vec3 start, Vec3 end, Vec3 extents, WorldQuery query);
int world_occluded_for(Vec3 start, Vec3 end, WorldQuery query);
unsigned world_contact_type(const Actor *actor);
extern WorldProp *world_props;
extern size_t world_prop_count, world_scenery_count;
extern char (*world_scenery)[51];
extern unsigned char world_background[2][4];

#endif
