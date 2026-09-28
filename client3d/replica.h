#ifndef SOLDAT3D_REPLICA_H
#define SOLDAT3D_REPLICA_H

#include "snapshot.h"

Snapshot replica_encode(const Game *game);
int replica_decode(Game *game, const unsigned char *data, size_t size);
Snapshot replica_delta_pack(const Snapshot *raw, const Snapshot *base);
int replica_delta_unpack(Snapshot *raw, const Snapshot *base, const unsigned char *packed, size_t size);

#endif
