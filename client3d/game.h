#ifndef SOLDAT3D_GAME_H
#define SOLDAT3D_GAME_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { float x, y, z; } Vec3;
static inline Vec3 v3(float x, float y, float z) { return (Vec3){x, y, z}; }
static inline Vec3 add(Vec3 a, Vec3 b) { return v3(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline Vec3 sub(Vec3 a, Vec3 b) { return v3(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline Vec3 scale(Vec3 a, float s) { return v3(a.x*s, a.y*s, a.z*s); }
static inline float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline float length(Vec3 a) { return sqrtf(dot(a,a)); }
static inline Vec3 direction(float yaw, float pitch) {
    return v3(sinf(yaw)*cosf(pitch), sinf(pitch), cosf(yaw)*cosf(pitch));
}

enum { TICK_RATE = 60, ACTOR_COUNT = 32, PLAYER_NAME_LENGTH = 24 };
typedef enum {
    EAGLE, MP5, AK74, STEYRAUG, SPAS12, RUGER77, M79, BARRETT,
    M249, MINIGUN, COLT, KNIFE, CHAINSAW, LAW, BOW2, BOW,
    FLAMER, M2, NOWEAPON, FRAGGRENADE, CLUSTERGRENADE, CLUSTER, THROWNKNIFE, WEAPON_COUNT
} WeaponId;

typedef struct {
    const char *name;
    int num, fire_mode, clip_reload, fire_interval, ammo, reload_time;
    int bullet_style, startup_time, bink, recoil, timeout;
    float hit_multiply, speed, movement_acc, bullet_spread, push;
    float inherited_velocity, modifier_head, modifier_chest, modifier_legs;
} WeaponDef;
extern const WeaponDef weapons[WEAPON_COUNT];

typedef enum { AIRBORNE, GROUNDED } Contact;
typedef enum { STANDING, CROUCHING, PRONE } Pose;
typedef enum {
    MOVE_IDLE, MOVE_RUN, MOVE_JUMP, MOVE_SIDEJUMP, MOVE_ROLL,
    MOVE_CROUCH, MOVE_PRONE, MOVE_PRONEMOVE, MOVE_GETUP, MOVE_ROLLBACK
} MoveAnimation;
typedef enum { ALIVE, DEAD, INACTIVE } Life;
typedef enum { TEAM_NONE, TEAM_ALPHA, TEAM_BRAVO, TEAM_CHARLIE, TEAM_DELTA, TEAM_SPECTATOR } Team;
typedef enum { FLAG_NONE, FLAG_ALPHA, FLAG_BRAVO, FLAG_YELLOW } FlagId;
typedef enum { FLAG_ABSENT, FLAG_BASE, FLAG_DROPPED, FLAG_CARRIED } FlagState;
typedef enum {
    MODE_DEATHMATCH, MODE_POINTMATCH, MODE_TEAMMATCH, MODE_CTF, MODE_RAMBO, MODE_INF, MODE_HTF
} GameMode;
typedef enum { MATCH_PLAYING, MATCH_FINISHED } MatchPhase;
extern const char *const game_mode_names[7];
extern const char *const game_mode_ids[7];
extern const char *const game_team_names[6];
typedef enum { BONUS_NONE, BONUS_FLAMEGOD, BONUS_PREDATOR, BONUS_BERSERKER } Bonus;
typedef enum { WEAPON_READY, WEAPON_RELOADING } WeaponPhase;
typedef struct {
    WeaponId id;
    int ammo, fire_count, reload_count, startup_count;
    WeaponPhase phase;
} WeaponState;

enum {
    INPUT_FIRE = 1u << 0, INPUT_JUMP = 1u << 1, INPUT_JETS = 1u << 2,
    INPUT_CROUCH = 1u << 3, INPUT_PRONE = 1u << 4, INPUT_RELOAD = 1u << 5,
    INPUT_GRENADE = 1u << 6, INPUT_SWITCH = 1u << 7, INPUT_ROLL = 1u << 8,
    INPUT_THROW = 1u << 9, INPUT_FLAG_THROW = 1u << 10
};
typedef struct {
    float right, forward, yaw, pitch;
    uint32_t held, pressed;
    float rewind_ticks;
    uint64_t command_sequence;
} Input;

enum { RAGDOLL_PART_COUNT = 24, RAGDOLL_CONSTRAINT_COUNT = 30 };
typedef enum { BOT_ASCEND, BOT_CROSS, BOT_LAND } BotTravelPhase;
typedef struct {
    Vec3 position[RAGDOLL_PART_COUNT + 1], old_position[RAGDOLL_PART_COUNT + 1];
    Vec3 previous[RAGDOLL_PART_COUNT + 1];
    uint32_t severed, ticks;
} Ragdoll;

typedef struct {
    Vec3 position, previous, velocity, force, move_direction;
    float yaw, pitch, health;
    uint32_t controls, spawn_id;
    uint64_t motion_tick, shot_sequence;
    Contact contact;
    Pose pose;
    MoveAnimation animation;
    Life life;
    Team team;
    FlagId carried_flag;
    int flag_grab_cooldown, captures, multikills, multikill_ticks, spawn_protection_ticks;
    int nav_edge, nav_goal;
    BotTravelPhase nav_phase;
    int animation_tick, fuel, fuel_capacity, respawn_ticks;
    int kills, deaths, grenades, grenade_cooldown, active_slot, hit_ticks;
    int bink_count, burst_count, grenade_charge, switch_ticks, melee_frame;
    float vest;
    Bonus bonus;
    int bonus_ticks, health_cooldown, throw_frame;
    WeaponId grenade_weapon;
    WeaponState slots[2];
    WeaponId loadout[2];
    Ragdoll ragdoll;
} Actor;

typedef enum { SURFACE_STONE, SURFACE_GRASS, SURFACE_METAL } Surface;
typedef struct { Vec3 min, max; Surface surface; } Box;
typedef struct { float fraction; Vec3 normal; int box; } WorldHit;
extern Vec3 world_spawns[ACTOR_COUNT];
void world_init(void);
void world_free(void);
WorldHit world_trace(Vec3 start, Vec3 end, Vec3 extents);
int world_pose_clear(Vec3 feet, Pose pose);
Contact world_move(Actor *actor);
float actor_height(Pose pose);
Vec3 actor_muzzle(const Actor *actor);
void movement_step(Actor *actor, Input input, uint64_t tick);

typedef struct {
    Vec3 position, previous, velocity, initial, hit_spot;
    WeaponId weapon;
    int owner, ticks, ricochets, degrade_count;
    int flag_hit_ticks[3];
    uint32_t hit_mask;
    float hit_multiply, rewind_ticks;
    uint64_t id;
} Projectile;
typedef enum { EVENT_SHOT, EVENT_IMPACT, EVENT_EXPLOSION, EVENT_HIT, EVENT_KILL,
    EVENT_PICKUP, EVENT_DROP, EVENT_FLAG_GRAB, EVENT_FLAG_RETURN, EVENT_FLAG_DROP,
    EVENT_FLAG_CAPTURE } EventKind;
typedef struct { EventKind kind; Vec3 position; int actor, target; WeaponId weapon; } GameEvent;
typedef enum {
    PICKUP_WEAPON, PICKUP_HEALTH, PICKUP_GRENADES, PICKUP_FLAMER, PICKUP_PREDATOR,
    PICKUP_VEST, PICKUP_BERSERKER, PICKUP_CLUSTER, PICKUP_BOW
} PickupKind;
typedef struct {
    Vec3 position, previous, velocity;
    PickupKind kind;
    WeaponId weapon;
    int ammo, owner, ticks, age, spawn_index;
    float yaw;
} Pickup;
typedef struct {
    Vec3 position, previous, velocity, base;
    FlagState state;
    int carrier, ticks;
} Flag;
typedef struct CombatHistory CombatHistory;
typedef struct {
    Vec3 target_position, target_velocity, aim_error, cover, grenade_target;
    Vec3 nav_dodge_direction;
    uint64_t nav_dodge_projectile, seen_tick, perceive_tick, cover_tick, grenade_tick;
    uint32_t spawn_id, target_spawn, random;
    int target, reaction_ticks, burst_remaining, burst_pause, observed_ammo;
    int grenade_hold, roam_node, nav_strafe_sign, nav_neighbors;
    WeaponId observed_weapon;
    float yaw_speed, pitch_speed, aim_yaw, aim_pitch;
} BotMemory;
typedef struct {
    Actor actors[ACTOR_COUNT];
    BotMemory bots[ACTOR_COUNT];
    char names[ACTOR_COUNT][PLAYER_NAME_LENGTH + 1];
    Projectile *projectiles;
    size_t projectile_count, projectile_capacity;
    GameEvent *events;
    size_t event_count, event_capacity;
    Pickup *pickups;
    size_t pickup_count, pickup_capacity;
    int bonus_frequency, max_grenades;
    GameMode mode;
    Flag flags[3];
    int team_score[5], friendly_fire, score_limit, time_limit_ticks;
    int wave_counter, htf_interval, match_ticks;
    MatchPhase phase;
    uint64_t tick;
    uint32_t random;
    uint64_t next_projectile_id, next_event_id;
    CombatHistory *history;
} Game;

void game_init(Game *game, uint32_t seed, GameMode mode);
void game_free(Game *game);
void game_step(Game *game, const Input inputs[ACTOR_COUNT]);
typedef enum { SPAWN_READY, SPAWN_BLOCKED } SpawnResult;
SpawnResult game_respawn(Game *game, int index);
void game_equip(Actor *actor, WeaponId primary, WeaponId secondary);
void game_select_loadout(Actor *actor, WeaponId primary, WeaponId secondary);
int game_team_mode(GameMode mode);
void game_set_mode(Game *game, GameMode mode);
void game_restart(Game *game);
void game_select_team(Game *game, int actor, Team team);
Input bot_input(Game *game, int index);
void combat_step(Game *game, const Input inputs[ACTOR_COUNT]);
void combat_history_enable(Game *game);
void combat_predict_actor(Game *game, int index, Input input);
void combat_environment(Game *game, int actor, unsigned polygon_type);
Vec3 combat_aim_target(const Game *game, int shooter, Vec3 start, Vec3 end, const Vec3 poses[ACTOR_COUNT][21]);
void combat_event(Game *game, GameEvent event);
float game_random(Game *game);

#endif
