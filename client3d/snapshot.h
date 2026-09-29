#ifndef SOLDAT3D_SNAPSHOT_H
#define SOLDAT3D_SNAPSHOT_H

#include "game.h"

typedef struct { unsigned char *data; size_t size; } Snapshot;
Snapshot snapshot_encode(const Game *game);
int snapshot_decode(Game *game, const unsigned char *data, size_t size);
Snapshot snapshot_pack(const Snapshot *raw);
int snapshot_unpack_into(unsigned char *data,size_t capacity,size_t *decoded_size,
    const unsigned char *packed,size_t size);
int snapshot_unpack(Snapshot *raw, const unsigned char *packed, size_t size);
Snapshot snapshot_delta_pack(const Snapshot *raw, const Snapshot *base);
int snapshot_delta_unpack(Snapshot *raw, const Snapshot *base, const unsigned char *packed, size_t size);

#endif
