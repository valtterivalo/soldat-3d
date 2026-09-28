#include "snapshot.h"
#include "generated_rules.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>

typedef enum { ENCODE, DECODE } Transfer;
typedef struct {
    unsigned char *data;
    size_t size, capacity, offset;
    Transfer transfer;
    int rejected;
} Codec;

static void bytes(Codec *codec, void *value, size_t size) {
    if (codec->rejected) return;
    if (codec->transfer == DECODE) {
        if (size > codec->size - codec->offset) { codec->rejected = 1; return; }
        memcpy(value, codec->data + codec->offset, size);
    } else {
        if (codec->offset + size > codec->capacity) {
            codec->capacity = (codec->offset + size) * 2;
            codec->data = realloc(codec->data, codec->capacity);
            if (!codec->data) abort();
        }
        memcpy(codec->data + codec->offset, value, size);
    }
    codec->offset += size;
}

static uint32_t word(Codec *codec, uint32_t value) {
    unsigned char data[4] = {value >> 24, value >> 16, value >> 8, value};
    bytes(codec, data, sizeof(data));
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 | (uint32_t)data[2] << 8 | data[3];
}

static uint64_t wide(Codec *codec, uint64_t value) {
    uint32_t high=word(codec,(uint32_t)(value>>32));
    uint32_t low=word(codec,(uint32_t)value);
    return (uint64_t)high<<32 | low;
}

static int integer(Codec *codec, int value) {
    uint32_t bits;
    int32_t signed_value = value;
    memcpy(&bits, &signed_value, sizeof(bits));
    bits = word(codec, bits);
    memcpy(&signed_value, &bits, sizeof(bits));
    return signed_value;
}

static float scalar(Codec *codec, float value) {
    uint32_t bits;
    _Static_assert(sizeof(value) == sizeof(bits), "Wire floats are 32 bits");
    memcpy(&bits, &value, sizeof(bits));
    bits = word(codec, bits);
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static Vec3 vector(Codec *codec, Vec3 value) {
    value.x = scalar(codec, value.x);
    value.y = scalar(codec, value.y);
    value.z = scalar(codec, value.z);
    return value;
}

static void actor_transfer(Codec *c, Actor *a) {
    a->life = word(c, a->life);
    a->team = word(c, a->team);
    a->spawn_id = word(c, a->spawn_id);
    a->motion_tick = wide(c, a->motion_tick);
    a->shot_sequence = wide(c, a->shot_sequence);
    if (a->team > TEAM_SPECTATOR) c->rejected = 1;
    if (a->life == INACTIVE) return;
    if (a->life != ALIVE && a->life != DEAD) { c->rejected = 1; return; }
    a->position = vector(c, a->position);
    a->previous = vector(c, a->previous);
    a->velocity = vector(c, a->velocity);
    a->force = vector(c, a->force);
    a->move_direction = vector(c, a->move_direction);
    a->yaw = scalar(c, a->yaw);
    a->pitch = scalar(c, a->pitch);
    a->health = scalar(c, a->health);
    a->controls = word(c, a->controls);
    a->carried_flag = word(c, a->carried_flag);
    a->flag_grab_cooldown = integer(c, a->flag_grab_cooldown);
    a->captures = integer(c, a->captures);
    a->multikills = integer(c, a->multikills);
    a->multikill_ticks = integer(c, a->multikill_ticks);
    a->spawn_protection_ticks = integer(c, a->spawn_protection_ticks);
    a->nav_edge = integer(c, a->nav_edge);
    a->nav_goal = integer(c, a->nav_goal);
    a->nav_phase = word(c, a->nav_phase);
    if (a->nav_phase > BOT_LAND) c->rejected = 1;
    a->contact = word(c, a->contact);
    a->pose = word(c, a->pose);
    a->animation = word(c, a->animation);
    a->animation_tick = integer(c, a->animation_tick);
    a->fuel = integer(c, a->fuel);
    a->fuel_capacity = integer(c, a->fuel_capacity);
    a->respawn_ticks = integer(c, a->respawn_ticks);
    a->kills = integer(c, a->kills);
    a->deaths = integer(c, a->deaths);
    a->grenades = integer(c, a->grenades);
    a->grenade_cooldown = integer(c, a->grenade_cooldown);
    a->active_slot = integer(c, a->active_slot);
    a->hit_ticks = integer(c, a->hit_ticks);
    a->bink_count = integer(c, a->bink_count);
    a->burst_count = integer(c, a->burst_count);
    a->grenade_charge = integer(c, a->grenade_charge);
    a->switch_ticks = integer(c, a->switch_ticks);
    a->melee_frame = integer(c, a->melee_frame);
    a->vest = scalar(c, a->vest);
    a->bonus = word(c, a->bonus);
    a->bonus_ticks = integer(c, a->bonus_ticks);
    a->health_cooldown = integer(c, a->health_cooldown);
    a->throw_frame = integer(c, a->throw_frame);
    a->grenade_weapon = word(c, a->grenade_weapon);
    for (int i = 0; i < 2; ++i) {
        WeaponState *w = &a->slots[i];
        w->id = word(c, w->id);
        w->ammo = integer(c, w->ammo);
        w->fire_count = integer(c, w->fire_count);
        w->reload_count = integer(c, w->reload_count);
        w->startup_count = integer(c, w->startup_count);
        w->phase = word(c, w->phase);
        a->loadout[i] = word(c, a->loadout[i]);
        if (w->id >= WEAPON_COUNT || a->loadout[i] >= WEAPON_COUNT || w->phase > WEAPON_RELOADING)
            c->rejected = 1;
    }
    if (a->life == DEAD) {
        for (int i = 0; i <= RAGDOLL_PART_COUNT; ++i) {
            a->ragdoll.position[i] = vector(c, a->ragdoll.position[i]);
            a->ragdoll.old_position[i] = vector(c, a->ragdoll.old_position[i]);
            a->ragdoll.previous[i] = vector(c, a->ragdoll.previous[i]);
        }
        a->ragdoll.severed = word(c, a->ragdoll.severed);
        a->ragdoll.ticks = word(c, a->ragdoll.ticks);
        if (a->ragdoll.severed & ~((1u << 1) | (1u << 3) | (1u << 19) | (1u << 20) | (1u << 22)))
            c->rejected = 1;
    }
    if (a->active_slot < 0 || a->active_slot > 1 || a->pose > PRONE || a->contact > GROUNDED ||
        a->animation > MOVE_ROLLBACK || a->bonus > BONUS_BERSERKER || a->carried_flag > FLAG_YELLOW ||
        (a->grenade_weapon != FRAGGRENADE && a->grenade_weapon != CLUSTERGRENADE))
        c->rejected = 1;
}

static void game_transfer(Codec *c, Game *game) {
    uint32_t magic = word(c, UINT32_C(0x53334350));
    uint32_t version = word(c, 1);
    if (magic != UINT32_C(0x53334350) || version != 1) { c->rejected = 1; return; }
    game->tick = wide(c, game->tick);
    game->next_projectile_id = wide(c, game->next_projectile_id);
    game->next_event_id = wide(c, game->next_event_id);
    game->random = word(c, game->random);
    game->mode = word(c, game->mode);
    game->bonus_frequency = integer(c, game->bonus_frequency);
    game->max_grenades = integer(c, game->max_grenades);
    if (game->mode > MODE_HTF || game->bonus_frequency < 0 || game->bonus_frequency > 5)
        c->rejected = 1;
    game->phase = word(c, game->phase);
    if (game->phase > MATCH_FINISHED) c->rejected = 1;
    game->friendly_fire = integer(c, game->friendly_fire);
    game->score_limit = integer(c, game->score_limit);
    game->time_limit_ticks = integer(c, game->time_limit_ticks);
    game->wave_counter = integer(c, game->wave_counter);
    game->htf_interval = integer(c, game->htf_interval);
    game->match_ticks = integer(c, game->match_ticks);
    for (int i = 0; i < 5; ++i) game->team_score[i] = integer(c, game->team_score[i]);
    for (int i = 0; i < 3; ++i) {
        Flag *flag = &game->flags[i];
        flag->position = vector(c, flag->position);
        flag->previous = vector(c, flag->previous);
        flag->velocity = vector(c, flag->velocity);
        flag->base = vector(c, flag->base);
        flag->state = word(c, flag->state);
        flag->carrier = integer(c, flag->carrier);
        flag->ticks = integer(c, flag->ticks);
        if (flag->state > FLAG_CARRIED || (flag->state == FLAG_CARRIED &&
            (flag->carrier < 0 || flag->carrier >= ACTOR_COUNT))) c->rejected = 1;
    }
    bytes(c, game->names, sizeof(game->names));
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        if (!memchr(game->names[i], 0, sizeof(game->names[i]))) c->rejected = 1;
        actor_transfer(c, &game->actors[i]);
    }
    uint32_t projectiles = word(c, (uint32_t)game->projectile_count);
    if (c->transfer == DECODE) {
        if (projectiles > (c->size - c->offset) / 140) { c->rejected = 1; return; }
        game->projectile_count = game->projectile_capacity = projectiles;
        game->projectiles = projectiles ? calloc(projectiles, sizeof(Projectile)) : NULL;
        if (projectiles && !game->projectiles) abort();
    }
    for (uint32_t i = 0; i < projectiles; ++i) {
        Projectile p = game->projectiles[i];
        p.position = vector(c, p.position);
        p.previous = vector(c, p.previous);
        p.velocity = vector(c, p.velocity);
        p.initial = vector(c, p.initial);
        p.hit_spot = vector(c, p.hit_spot);
        p.weapon = word(c, p.weapon);
        p.owner = integer(c, p.owner);
        p.ticks = integer(c, p.ticks);
        p.ricochets = integer(c, p.ricochets);
        p.degrade_count = integer(c, p.degrade_count);
        p.hit_mask = word(c, p.hit_mask);
        p.hit_multiply = scalar(c, p.hit_multiply);
        p.rewind.mode = word(c, p.rewind.mode);
        p.rewind.before_tick = wide(c, p.rewind.before_tick);
        p.rewind.after_tick = wide(c, p.rewind.after_tick);
        p.rewind.applied_tick = wide(c, p.rewind.applied_tick);
        p.rewind.fraction = scalar(c, p.rewind.fraction);
        p.id = wide(c, p.id);
        for (int flag = 0; flag < 3; ++flag) p.flag_hit_ticks[flag] = integer(c, p.flag_hit_ticks[flag]);
        if (p.weapon >= WEAPON_COUNT || p.owner < 0 || p.owner >= ACTOR_COUNT) c->rejected = 1;
        if (p.rewind.mode > REWIND_RENDERED || (p.rewind.mode == REWIND_RENDERED &&
            (p.rewind.before_tick > p.rewind.after_tick || p.rewind.after_tick > p.rewind.applied_tick ||
             p.rewind.applied_tick - p.rewind.before_tick > SRC_MAX_OLDPOS ||
             !(p.rewind.fraction >= 0 && p.rewind.fraction <= 1)))) c->rejected = 1;
        if (p.weapon == FLAMER && (p.ticks < 1 || p.ticks > SRC_FLAMER_TIMEOUT)) c->rejected = 1;
        if (c->transfer == DECODE) game->projectiles[i] = p;
    }
    uint32_t pickups = word(c, (uint32_t)game->pickup_count);
    if (c->transfer == DECODE) {
        if (pickups > (c->size - c->offset) / 68) { c->rejected = 1; return; }
        game->pickup_count = game->pickup_capacity = pickups;
        game->pickups = pickups ? calloc(pickups, sizeof(Pickup)) : NULL;
        if (pickups && !game->pickups) abort();
    }
    for (uint32_t i = 0; i < pickups; ++i) {
        Pickup p = game->pickups[i];
        p.position = vector(c, p.position);
        p.previous = vector(c, p.previous);
        p.velocity = vector(c, p.velocity);
        p.kind = word(c, p.kind);
        p.weapon = word(c, p.weapon);
        p.ammo = integer(c, p.ammo);
        p.owner = integer(c, p.owner);
        p.ticks = integer(c, p.ticks);
        p.age = integer(c, p.age);
        p.spawn_index = integer(c, p.spawn_index);
        p.yaw = scalar(c, p.yaw);
        if (p.kind > PICKUP_BOW || p.weapon >= WEAPON_COUNT) c->rejected = 1;
        if ((p.kind == PICKUP_WEAPON && p.weapon > LAW) || (p.kind == PICKUP_BOW && p.weapon != BOW))
            c->rejected = 1;
        if (c->transfer == DECODE) game->pickups[i] = p;
    }
    uint32_t events = word(c, (uint32_t)game->event_count);
    if (c->transfer == DECODE) {
        if (events > (c->size - c->offset) / 28) { c->rejected = 1; return; }
        game->event_count = game->event_capacity = events;
        game->events = events ? calloc(events, sizeof(GameEvent)) : NULL;
        if (events && !game->events) abort();
    }
    for (uint32_t i = 0; i < events; ++i) {
        GameEvent e = game->events[i];
        e.kind = word(c, e.kind);
        e.position = vector(c, e.position);
        e.actor = integer(c, e.actor);
        e.target = integer(c, e.target);
        e.weapon = word(c, e.weapon);
        if (e.kind > EVENT_FLAG_CAPTURE || e.weapon >= WEAPON_COUNT || e.actor < -1 || e.actor >= ACTOR_COUNT ||
            (e.actor == -1 && e.kind != EVENT_FLAG_RETURN))
            c->rejected = 1;
        if ((e.kind == EVENT_PICKUP && (e.target < PICKUP_WEAPON || e.target > PICKUP_BOW)) ||
            (e.kind == EVENT_KILL && (e.target < 0 || e.target >= ACTOR_COUNT)) ||
            (e.kind >= EVENT_FLAG_GRAB && (e.target < FLAG_ALPHA || e.target > FLAG_YELLOW))) c->rejected = 1;
        if (c->transfer == DECODE) game->events[i] = e;
    }
}

Snapshot snapshot_encode(const Game *game) {
    assert(game->projectile_count <= UINT32_MAX && game->pickup_count <= UINT32_MAX && game->event_count <= UINT32_MAX);
    Codec codec = {.transfer = ENCODE};
    Game copy = *game;
    game_transfer(&codec, &copy);
    if (codec.rejected) abort();
    return (Snapshot){codec.data, codec.offset};
}

int snapshot_decode(Game *game, const unsigned char *data, size_t size) {
    Codec codec = {.data = (unsigned char *)data, .size = size, .transfer = DECODE};
    Game decoded = {0};
    game_transfer(&codec, &decoded);
    if (codec.rejected || codec.offset != size) { game_free(&decoded); return 0; }
    game_free(game);
    *game = decoded;
    return 1;
}

static Snapshot pack(const Snapshot *raw,const Snapshot *base) {
    assert(raw->size<=UINT32_MAX && (!base || base->size<=UINT32_MAX));
    size_t capacity=ZSTD_compressBound(raw->size);
    unsigned char *data=malloc(capacity+5);
    if (!data) abort();
    data[0]=1;
    for (unsigned i=0;i<4;++i) data[i+1]=(unsigned char)(raw->size>>(24-8*i));
    ZSTD_CCtx *context=ZSTD_createCCtx();
    if (!context) abort();
    if (ZSTD_isError(ZSTD_CCtx_setParameter(context,ZSTD_c_compressionLevel,1)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(context,ZSTD_c_checksumFlag,1))) abort();
    if (base && ZSTD_isError(ZSTD_CCtx_loadDictionary(context,base->data,base->size))) abort();
    size_t compressed=ZSTD_compress2(context,data+5,capacity,raw->data,raw->size);
    if (ZSTD_isError(compressed)) abort();
    ZSTD_freeCCtx(context);
    size_t size=compressed+5;
    if (size>=raw->size+5) {
        data[0]=0;
        if (raw->size) memcpy(data+5,raw->data,raw->size);
        size=raw->size+5;
    }
    return (Snapshot){data,size};
}

static int unpack_frame(unsigned char *output,size_t expected,const Snapshot *base,
    const unsigned char *packed,size_t size) {
    if (ZSTD_getFrameContentSize(packed,size)!=expected) return 0;
    ZSTD_DCtx *context=ZSTD_createDCtx();
    if (!context) abort();
    if (base && ZSTD_isError(ZSTD_DCtx_loadDictionary(context,base->data,base->size))) abort();
    ZSTD_inBuffer input={packed,size,0};
    unsigned char scratch[8192];
    size_t produced=0;
    int valid=0;
    for (;;) {
        size_t remaining=expected-produced;
        ZSTD_outBuffer target={output && remaining ? output+produced : scratch,
            output && remaining ? remaining : sizeof(scratch),0};
        size_t consumed=input.pos;
        size_t status=ZSTD_decompressStream(context,&target,&input);
        if (ZSTD_isError(status)) break;
        produced+=target.pos;
        if (produced>expected) break;
        if (status==0) {
            valid=produced==expected && input.pos==size;
            break;
        }
        if (input.pos==consumed && target.pos==0) break;
    }
    ZSTD_freeDCtx(context);
    return valid;
}

static int unpack(Snapshot *raw,const Snapshot *base,const unsigned char *packed,size_t size) {
    if (size<5 || packed[0]>1) return 0;
    size_t expected=(uint32_t)packed[1]<<24 | (uint32_t)packed[2]<<16 |
        (uint32_t)packed[3]<<8 | packed[4];
    if (packed[0]==0) {
        if (size-5!=expected) return 0;
    } else if (!unpack_frame(NULL,expected,base,packed+5,size-5)) return 0;
    unsigned char *data=expected ? malloc(expected) : NULL;
    if (expected && !data) abort();
    if (packed[0]==0) {
        if (expected) memcpy(data,packed+5,expected);
    } else if (!unpack_frame(data,expected,base,packed+5,size-5)) abort();
    free(raw->data);
    *raw=(Snapshot){data,expected};
    return 1;
}

Snapshot snapshot_pack(const Snapshot *raw) { return pack(raw,NULL); }
int snapshot_unpack(Snapshot *raw,const unsigned char *packed,size_t size) {
    return unpack(raw,NULL,packed,size);
}
Snapshot snapshot_delta_pack(const Snapshot *raw,const Snapshot *base) { return pack(raw,base); }
int snapshot_delta_unpack(Snapshot *raw,const Snapshot *base,const unsigned char *packed,size_t size) {
    return unpack(raw,base,packed,size);
}
