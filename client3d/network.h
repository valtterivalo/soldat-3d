#ifndef SOLDAT3D_NETWORK_H
#define SOLDAT3D_NETWORK_H

#include "game.h"

typedef struct Network Network;
typedef enum { NET_CONNECTING, NET_CONNECTED, NET_DISCONNECTED, NET_REJECTED } NetStatus;
enum { NETWORK_PROTOCOL_VERSION = 5 };
Network *network_host(unsigned short port, int local_actor, Game *game);
Network *network_join(const char *host, unsigned short port, const char *name);
void network_close(Network *network);
int network_receive(Network *network, Game *game);
int network_receive_at(Network *network, Game *game, double now);
void network_inputs(Network *network, Input inputs[ACTOR_COUNT]);
void network_broadcast(Network *network, const Game *game);
void network_send_input(Network *network, Game *game, Input input, WeaponId primary, WeaponId secondary);
void network_select_team(Network *network, Team team);
void network_set_name(Network *network, const char *name);
int network_socket(const Network *network);
int network_remote(const Network *network, int actor);
uint64_t network_received_sequence(const Network *network, int actor);
int network_actor(const Network *network);
size_t network_map(const Network *network);
unsigned short network_port(const Network *network);
NetStatus network_status(const Network *network);
const char *network_message(const Network *network);
typedef struct {
    uint64_t sent_bytes, received_bytes, sent_packets, received_packets, snapshots;
    uint64_t raw_snapshot_bytes, packed_snapshot_bytes, extrapolated_inputs, repaired_fragments;
    double correction_distance;
    float max_correction;
} NetworkStats;
NetworkStats network_stats(const Network *network);
const Game *network_render(Network *network, const Game *predicted, double now, float local_alpha);
double network_view_tick(const Network *network);
const Vec3 *network_actor_pose(const Network *network, int actor);
double network_time(void);

#endif
