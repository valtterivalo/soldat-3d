#ifndef SOLDAT3D_EFFECTS_H
#define SOLDAT3D_EFFECTS_H

#include "game.h"
#include "generated_rules.h"
#include "raylib.h"

typedef struct { GameEvent event; uint64_t tick; } VisualEvent;
typedef struct { Model model; Color color; Matrix *transforms; size_t count,capacity; } EffectBatch;
typedef struct {
    VisualEvent *events;
    size_t count, capacity;
    Texture2D explosion[SRC_EXPLOSION_ANIMS], smoke[SRC_SMOKE_ANIMS + 1];
    Texture2D cluster_smoke;
    Model bullets[COLT + 1], shells[COLT + 1], grenade, missile, blood, spark, jet;
    Model kits[7], cluster_grenade, cluster, arrow, flames[SRC_FLAMER_TIMEOUT / 2];
    Texture2D kit_textures[7];
    Model flag_pole;
    Texture2D flag_cloth[2];
    Shader instance_shader;
    EffectBatch *batches;
    size_t batch_count;
} Effects;

Effects effects_load(void);
void effects_step(Effects *effects, const Game *game, uint64_t tick);
void effects_draw(Effects *effects, const Game *game, Camera3D camera, float alpha, double visual_tick,
    const uint32_t held[ACTOR_COUNT]);
void effects_unload(Effects *effects);

#endif
