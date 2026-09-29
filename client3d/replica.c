#include "replica.h"
#include "generated_rules.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { REPLICA_MAGIC = 0x53443352, DELTA_MAGIC = 0x53443344, HEADER_BYTES = 976,
    ACTOR_BYTES = 504, ACTOR_POSITION = 28, ACTOR_PREVIOUS = 40,
    PROJECTILE_BYTES = 44, PROJECTILE_POSITION = 8, PROJECTILE_VELOCITY = 20, PICKUP_BYTES = 40 };

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

static void game_transfer(Codec *c, Game *game) {
    if (word(c, REPLICA_MAGIC) != REPLICA_MAGIC) c->rejected = 1;
    game->tick = wide(c, game->tick);
    game->random = word(c, game->random);
    game->mode = word(c, game->mode);
    game->bonus_frequency = integer(c, game->bonus_frequency);
    game->max_grenades = integer(c, game->max_grenades);
    game->phase = word(c, game->phase);
    game->friendly_fire = integer(c, game->friendly_fire);
    game->score_limit = integer(c, game->score_limit);
    game->time_limit_ticks = integer(c, game->time_limit_ticks);
    game->match_ticks = integer(c, game->match_ticks);
    if (game->mode > MODE_HTF || game->bonus_frequency < 0 || game->bonus_frequency > 5 || game->phase > MATCH_FINISHED)
        c->rejected = 1;
    for (int i = 0; i < 5; ++i) game->team_score[i] = integer(c, game->team_score[i]);
    for (int i = 0; i < 3; ++i) {
        Flag *flag = &game->flags[i];
        flag->position = vector(c, flag->position);
        flag->base = vector(c, flag->base);
        flag->state = word(c, flag->state);
        flag->carrier = integer(c, flag->carrier);
        flag->ticks = integer(c, flag->ticks);
        flag->previous = flag->position;
        if (flag->state > FLAG_CARRIED || (flag->state == FLAG_CARRIED &&
            (flag->carrier < 0 || flag->carrier >= ACTOR_COUNT))) c->rejected = 1;
    }
    bytes(c, game->names, sizeof(game->names));
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        if (!memchr(game->names[i], 0, sizeof(game->names[i]))) c->rejected = 1;
        Actor *a = &game->actors[i];
        a->life = word(c, a->life);
        a->team = word(c, a->team);
        a->spawn_id = word(c, a->spawn_id);
        a->motion_tick = wide(c, a->motion_tick);
        a->shot_sequence = wide(c, a->shot_sequence);
        if (a->team > TEAM_SPECTATOR) c->rejected = 1;
        if (a->life > INACTIVE) c->rejected = 1;
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
        a->captures = integer(c, a->captures);
        a->spawn_protection_ticks = integer(c, a->spawn_protection_ticks);
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
        a->active_slot = integer(c, a->active_slot);
        a->bink_count = integer(c, a->bink_count);
        a->burst_count = integer(c, a->burst_count);
        a->grenade_charge = integer(c, a->grenade_charge);
        a->switch_ticks = integer(c, a->switch_ticks);
        a->melee_frame = integer(c, a->melee_frame);
        a->vest = scalar(c, a->vest);
        a->bonus = word(c, a->bonus);
        a->bonus_ticks = integer(c, a->bonus_ticks);
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
        for (int i = 1; i <= 20; ++i) {
            Vec3 point = a->life == DEAD ? a->ragdoll.position[i] : v3(0, 0, 0);
            float coordinates[3] = {point.x, point.y, point.z};
            for (int axis = 0; axis < 3; ++axis) {
                long value = c->transfer == ENCODE ? lroundf(coordinates[axis] * 128) : 0;
                assert(value >= INT32_MIN && value <= INT32_MAX);
                coordinates[axis] = (float)integer(c, (int)value) / 128;
            }
            if (a->life == DEAD) {
                a->ragdoll.position[i] = v3(coordinates[0], coordinates[1], coordinates[2]);
                a->ragdoll.previous[i] = a->ragdoll.old_position[i] = a->ragdoll.position[i];
            }
        }
        a->ragdoll.severed = word(c, a->life == DEAD ? a->ragdoll.severed : 0);
        a->ragdoll.ticks = word(c, a->life == DEAD ? a->ragdoll.ticks : 0);
        if (a->ragdoll.severed & ~((1u << 1) | (1u << 3) | (1u << 19) | (1u << 20) | (1u << 22)))
            c->rejected = 1;
        if (a->life == INACTIVE) continue;
        if (a->active_slot < 0 || a->active_slot > 1 || a->pose > PRONE || a->contact > GROUNDED ||
            a->animation > MOVE_ROLLBACK || a->bonus > BONUS_BERSERKER || a->carried_flag > FLAG_YELLOW ||
            (a->grenade_weapon != FRAGGRENADE && a->grenade_weapon != CLUSTERGRENADE))
            c->rejected = 1;
    }
    uint32_t projectiles = word(c, (uint32_t)game->projectile_count);
    if (c->transfer == DECODE) {
        if (projectiles > (c->size - c->offset) / PROJECTILE_BYTES) { c->rejected = 1; return; }
        game->projectile_count = game->projectile_capacity = projectiles;
        game->projectiles = projectiles ? calloc(projectiles, sizeof(Projectile)) : NULL;
        if (projectiles && !game->projectiles) abort();
    }
    uint64_t last_id = 0;
    for (uint32_t i = 0; i < projectiles; ++i) {
        Projectile value = game->projectiles[i];
        Projectile *p = &value;
        p->id = wide(c, p->id);
        p->position = vector(c, p->position);
        p->velocity = vector(c, p->velocity);
        p->weapon = word(c, p->weapon);
        p->owner = integer(c, p->owner);
        p->ticks = integer(c, p->ticks);
        p->previous = p->position;
        if (p->id <= last_id || p->weapon >= WEAPON_COUNT || p->owner < 0 || p->owner >= ACTOR_COUNT ||
            (p->weapon == FLAMER && (p->ticks < 1 || p->ticks > SRC_FLAMER_TIMEOUT))) c->rejected = 1;
        last_id = p->id;
        if (c->transfer == DECODE) game->projectiles[i] = value;
    }
    uint32_t pickups = word(c, (uint32_t)game->pickup_count);
    if (c->transfer == DECODE) {
        if (pickups > (c->size - c->offset) / PICKUP_BYTES) { c->rejected = 1; return; }
        game->pickup_count = game->pickup_capacity = pickups;
        game->pickups = pickups ? calloc(pickups, sizeof(Pickup)) : NULL;
        if (pickups && !game->pickups) abort();
    }
    for (uint32_t i = 0; i < pickups; ++i) {
        Pickup value = game->pickups[i];
        Pickup *p = &value;
        p->position = vector(c, p->position);
        p->previous = vector(c, p->previous);
        p->kind = word(c, p->kind);
        p->weapon = word(c, p->weapon);
        p->ticks = integer(c, p->ticks);
        p->yaw = scalar(c, p->yaw);
        if (p->kind > PICKUP_BOW || p->weapon >= WEAPON_COUNT ||
            (p->kind == PICKUP_WEAPON && p->weapon > LAW) ||
            (p->kind == PICKUP_BOW && p->weapon != BOW)) c->rejected = 1;
        if (c->transfer == DECODE) game->pickups[i] = value;
    }
}

Snapshot replica_encode(const Game *game) {
    assert(game->projectile_count <= UINT32_MAX && game->pickup_count <= UINT32_MAX);
    size_t size = HEADER_BYTES + ACTOR_COUNT * ACTOR_BYTES + 8 +
        game->projectile_count * PROJECTILE_BYTES + game->pickup_count * PICKUP_BYTES;
    Codec codec = {.data = malloc(size), .capacity = size, .transfer = ENCODE};
    if (!codec.data) abort();
    Game copy = *game;
    game_transfer(&codec, &copy);
    if (codec.rejected) abort();
    assert(codec.offset == size);
    return (Snapshot){codec.data, codec.offset};
}

int replica_decode(Game *game, const unsigned char *data, size_t size) {
    Codec codec = {.data = (unsigned char *)data, .size = size, .transfer = DECODE};
    Game decoded = {0};
    game_transfer(&codec, &decoded);
    if (codec.rejected || codec.offset != size) { game_free(&decoded); return 0; }
    game_free(game);
    *game = decoded;
    return 1;
}

static uint32_t readword(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static void writeword(unsigned char *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}

static uint64_t readwide(const unsigned char *p) {
    return (uint64_t)readword(p) << 32 | readword(p + 4);
}

static uint64_t fingerprint(const Snapshot *snapshot) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < snapshot->size; ++i) hash = (hash ^ snapshot->data[i]) * UINT64_C(1099511628211);
    return hash;
}

typedef struct { const unsigned char *projectiles, *pickups; uint32_t projectile_count, pickup_count; } Layout;

static int layout(const Snapshot *raw, Layout *out) {
    size_t offset = HEADER_BYTES + ACTOR_COUNT * ACTOR_BYTES;
    if (raw->size < offset + 8 || readword(raw->data) != REPLICA_MAGIC) return 0;
    out->projectile_count = readword(raw->data + offset);
    offset += 4;
    if (out->projectile_count > (raw->size - offset - 4) / PROJECTILE_BYTES) return 0;
    out->projectiles = raw->data + offset;
    uint64_t last_id = 0;
    for (uint32_t i = 0; i < out->projectile_count; ++i) {
        uint64_t id = readwide(out->projectiles + (size_t)i * PROJECTILE_BYTES);
        if (id <= last_id) return 0;
        last_id = id;
    }
    offset += (size_t)out->projectile_count * PROJECTILE_BYTES;
    out->pickup_count = readword(raw->data + offset);
    offset += 4;
    out->pickups = raw->data + offset;
    return out->pickup_count == (raw->size - offset) / PICKUP_BYTES &&
        (raw->size - offset) % PICKUP_BYTES == 0;
}

static uint32_t varint(Codec *c, uint32_t value) {
    if (c->transfer == ENCODE) {
        do {
            unsigned char byte = (value & 127) | (value >= 128 ? 128 : 0);
            bytes(c, &byte, 1);
            value >>= 7;
        } while (value);
        return 0;
    }
    uint32_t decoded = 0;
    for (unsigned shift = 0; shift <= 28; shift += 7) {
        unsigned char byte = 0;
        bytes(c, &byte, 1);
        if (c->rejected) return 0;
        if (shift == 28 && byte > 15) { c->rejected = 1; return 0; }
        decoded |= (uint32_t)(byte & 127) << shift;
        if (!(byte & 128)) return decoded;
    }
    c->rejected = 1;
    return 0;
}

static void entity_encode(Codec *c, const unsigned char *current, const unsigned char *base, size_t size) {
    unsigned char mask[(HEADER_BYTES / 4 + 7) / 8] = {0};
    size_t words = size / 4, mask_size = (words + 7) / 8;
    assert(mask_size <= sizeof(mask));
    for (size_t i = 0; i < words; ++i)
        if (readword(current + i * 4) != (base ? readword(base + i * 4) : 0)) mask[i / 8] |= 1u << (i % 8);
    bytes(c, mask, mask_size);
    for (size_t i = 0; i < words; ++i) {
        if (!(mask[i / 8] & (1u << (i % 8)))) continue;
        uint32_t previous = base ? readword(base + i * 4) : 0;
        uint32_t delta = readword(current + i * 4) - previous;
        varint(c, (delta << 1) ^ (0u - (delta >> 31)));
    }
}

static void entity_decode(Codec *c, Codec *out, const unsigned char *base, size_t size) {
    unsigned char mask[(HEADER_BYTES / 4 + 7) / 8] = {0};
    size_t words = size / 4, mask_size = (words + 7) / 8;
    assert(mask_size <= sizeof(mask));
    bytes(c, mask, mask_size);
    if (words % 8 && (mask[mask_size - 1] >> (words % 8))) c->rejected = 1;
    for (size_t i = 0; i < words && !c->rejected; ++i) {
        uint32_t value = base ? readword(base + i * 4) : 0;
        if (mask[i / 8] & (1u << (i % 8))) {
            uint32_t delta = varint(c, 0);
            value += (delta >> 1) ^ (0u - (delta & 1));
        }
        word(out, value);
    }
}

static void actor_predict(unsigned char predicted[ACTOR_BYTES], const unsigned char *base, uint32_t ticks) {
    memcpy(predicted, base, ACTOR_BYTES);
    for (unsigned axis = 0; axis < 3; ++axis) {
        uint32_t position = readword(base + ACTOR_POSITION + axis * 4);
        uint32_t velocity = position - readword(base + ACTOR_PREVIOUS + axis * 4);
        for (unsigned previous = 0; previous < 2; ++previous)
            writeword(predicted + ACTOR_POSITION + (previous * 3 + axis) * 4,
                position + velocity * (ticks - previous));
    }
}

static const unsigned char *projectile_predict(unsigned char predicted[PROJECTILE_BYTES],
    const unsigned char *base, uint32_t ticks) {
    if (!base) return NULL;
    memcpy(predicted, base, PROJECTILE_BYTES);
    for (unsigned axis = 0; axis < 3; ++axis) {
        uint32_t position = readword(base + PROJECTILE_POSITION + axis * 4);
        uint32_t velocity = readword(base + PROJECTILE_VELOCITY + axis * 4);
        unsigned exponent = (velocity >> 23) & 255;
        uint32_t mantissa = (velocity & UINT32_C(0x7fffff)) | (exponent ? UINT32_C(0x800000) : 0);
        int shift = (int)exponent - (int)((position >> 23) & 255);
        uint32_t change = shift >= 0 ? (shift < 32 ? mantissa << shift : 0) :
            (shift > -32 ? mantissa >> -shift : 0);
        if ((position ^ velocity) >> 31) change = 0u - change;
        writeword(predicted + PROJECTILE_POSITION + axis * 4, position + change * ticks);
    }
    return predicted;
}

Snapshot replica_delta_pack(const Snapshot *raw, const Snapshot *base) {
    Layout current, previous;
    if (!layout(raw, &current) || !layout(base, &previous)) abort();
    Codec codec = {.transfer = ENCODE};
    word(&codec, DELTA_MAGIC);
    wide(&codec, fingerprint(base));
    wide(&codec, fingerprint(raw));
    entity_encode(&codec, raw->data, base->data, HEADER_BYTES);
    uint32_t ticks = (uint32_t)(readwide(raw->data + 4) - readwide(base->data + 4));
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        unsigned char predicted[ACTOR_BYTES];
        actor_predict(predicted, base->data + HEADER_BYTES + i * ACTOR_BYTES, ticks);
        entity_encode(&codec, raw->data + HEADER_BYTES + i * ACTOR_BYTES, predicted, ACTOR_BYTES);
    }
    varint(&codec, current.projectile_count);
    uint32_t previous_index = 0;
    for (uint32_t i = 0; i < current.projectile_count; ++i) {
        const unsigned char *p = current.projectiles + (size_t)i * PROJECTILE_BYTES;
        uint64_t id = readwide(p);
        wide(&codec, id);
        while (previous_index < previous.projectile_count &&
            readwide(previous.projectiles + (size_t)previous_index * PROJECTILE_BYTES) < id) ++previous_index;
        const unsigned char *old = previous_index < previous.projectile_count &&
            readwide(previous.projectiles + (size_t)previous_index * PROJECTILE_BYTES) == id ?
            previous.projectiles + (size_t)previous_index * PROJECTILE_BYTES : NULL;
        unsigned char predicted[PROJECTILE_BYTES];
        old = projectile_predict(predicted, old, ticks);
        entity_encode(&codec, p + 8, old ? old + 8 : NULL, PROJECTILE_BYTES - 8);
    }
    varint(&codec, current.pickup_count);
    for (uint32_t i = 0; i < current.pickup_count; ++i)
        entity_encode(&codec, current.pickups + (size_t)i * PICKUP_BYTES,
            i < previous.pickup_count ? previous.pickups + (size_t)i * PICKUP_BYTES : NULL, PICKUP_BYTES);
    Snapshot changes = {codec.data, codec.offset};
    Snapshot packed = snapshot_pack(&changes);
    free(changes.data);
    return packed;
}

int replica_delta_unpack(Snapshot *raw, const Snapshot *base, const unsigned char *packed, size_t size) {
    Layout previous;
    if (!layout(base, &previous)) return 0;
    Snapshot changes = {0};
    if (!snapshot_unpack(&changes, packed, size)) return 0;
    Codec codec = {.data = changes.data, .size = changes.size, .transfer = DECODE};
    Codec out = {.transfer = ENCODE};
    if (word(&codec, 0) != DELTA_MAGIC) codec.rejected = 1;
    uint64_t base_hash = wide(&codec, 0), result_hash = wide(&codec, 0);
    if (base_hash != fingerprint(base)) codec.rejected = 1;
    entity_decode(&codec, &out, base->data, HEADER_BYTES);
    uint32_t ticks = 0;
    if (!codec.rejected) ticks = (uint32_t)(readwide(out.data + 4) - readwide(base->data + 4));
    for (int i = 0; i < ACTOR_COUNT && !codec.rejected; ++i) {
        unsigned char predicted[ACTOR_BYTES];
        actor_predict(predicted, base->data + HEADER_BYTES + i * ACTOR_BYTES, ticks);
        entity_decode(&codec, &out, predicted, ACTOR_BYTES);
    }
    uint32_t count = varint(&codec, 0);
    if (count > (codec.size - codec.offset) / 10) codec.rejected = 1;
    word(&out, count);
    uint32_t previous_index = 0;
    uint64_t last_id = 0;
    for (uint32_t i = 0; i < count && !codec.rejected; ++i) {
        uint64_t id = wide(&codec, 0);
        if (id <= last_id) { codec.rejected = 1; break; }
        last_id = id;
        wide(&out, id);
        while (previous_index < previous.projectile_count &&
            readwide(previous.projectiles + (size_t)previous_index * PROJECTILE_BYTES) < id) ++previous_index;
        const unsigned char *old = previous_index < previous.projectile_count &&
            readwide(previous.projectiles + (size_t)previous_index * PROJECTILE_BYTES) == id ?
            previous.projectiles + (size_t)previous_index * PROJECTILE_BYTES : NULL;
        unsigned char predicted[PROJECTILE_BYTES];
        old = projectile_predict(predicted, old, ticks);
        entity_decode(&codec, &out, old ? old + 8 : NULL, PROJECTILE_BYTES - 8);
    }
    count = varint(&codec, 0);
    if (count > (codec.size - codec.offset) / 2) codec.rejected = 1;
    word(&out, count);
    for (uint32_t i = 0; i < count && !codec.rejected; ++i)
        entity_decode(&codec, &out,
            i < previous.pickup_count ? previous.pickups + (size_t)i * PICKUP_BYTES : NULL, PICKUP_BYTES);
    Snapshot decoded = {out.data, out.offset};
    int valid = !codec.rejected && codec.offset == codec.size && fingerprint(&decoded) == result_hash;
    free(changes.data);
    if (!valid) { free(decoded.data); return 0; }
    free(raw->data);
    *raw = decoded;
    return 1;
}
