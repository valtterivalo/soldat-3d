#if defined(__linux__)
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "network.h"
#include "snapshot.h"
#include "replica.h"
#include "world.h"
#include "generated_rules.h"
#include "objectives.h"
#include "lobby.h"
#include "pose.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#if defined(__linux__)
#include <sys/uio.h>
#endif
#include <time.h>
#include <unistd.h>

enum { MAGIC = 0x534f4c33, VERSION = NETWORK_PROTOCOL_VERSION, DATAGRAM = 1200, HEADER = NETWORK_PACKET_HEADER, STATE_HEADER = NETWORK_STATE_HEADER, STATE_DATA = NETWORK_STATE_DATA, BUTTONS = 11, COMMAND_SIZE = 104, INTERPOLATION_TICKS = 2, REPAIR_GROUP = 255, EVENT_SIZE = 64 };
typedef enum { HELLO = 1, WELCOME, COMMAND, STATE, LEAVE, REJECT, REPAIR, EVENTS = 9, EVENT_ACK, CHALLENGE, CONNECT } PacketKind;
typedef enum { HOST, CLIENT } Role;
typedef enum { DELIVERY_PROTECTED, DELIVERY_CLEAN } Delivery;
typedef struct {
    uint64_t sequence, view_frame, view_start_frame;
    Input input;
    double view_tick;
    uint32_t presses[BUTTONS], spawn_id;
    WeaponId primary, secondary;
    Team team;
} PendingInput;
typedef struct {
    struct sockaddr_in6 address;
    uint64_t token, received_sequence, applied_sequence, confirmed_frame, joined_frame, event_ack;
    uint64_t delivery_sequence, receipt_sequence, receipt_previous, receipt_mask;
    Delivery delivery;
    uint32_t presses[BUTTONS];
    Input input;
    PendingInput *commands;
    size_t command_count, command_capacity;
    Team requested_team;
    double last_seen;
    int joined, started;
    uint32_t spawn_id;
} Peer;
typedef struct {
    struct sockaddr_in6 address;
    uint64_t token;
    double issued;
} Admission;
typedef struct {
    unsigned char *data, *fragments, *parity, *parity_received;
    size_t size, count, received;
    unsigned repairs;
    uint64_t sequence, ack;
    size_t map;
} Assembly;
typedef struct { uint64_t sequence, tick; size_t map; Snapshot raw; } SentFrame;
typedef struct { uint64_t sequence; WeaponId weapon; EventKind kind; } PredictedEvent;
typedef struct { uint64_t id, tick, command, epoch; size_t map; GameEvent event; } JournalEvent;
typedef struct { uint64_t sequence, gap; double receipt; size_t map; Game game; Snapshot raw; } StateFrame;
struct Network {
    int socket, local_actor;
    unsigned short port;
    Role role;
    NetStatus status;
    Peer peers[ACTOR_COUNT], server;
    Admission admissions[ACTOR_COUNT];
    size_t admission_count;
    uint64_t sequence, frame, completed_frame, authority_tick, input_ack;
    uint64_t received_delivery, delivery_mask;
    uint32_t presses[BUTTONS], spawn_ids[ACTOR_COUNT];
    size_t map;
    char name[PLAYER_NAME_LENGTH + 1], message[128], server_name[64];
    Team team;
    PendingInput *pending;
    size_t pending_count, pending_capacity;
    PredictedEvent *predicted_events;
    size_t predicted_event_count, predicted_event_capacity;
    JournalEvent *presentation_events;
    size_t presentation_count, presentation_capacity;
    GameEvent *presented_events;
    size_t presented_capacity;
    JournalEvent *journal;
    size_t journal_count, journal_capacity;
    uint64_t next_event_id, received_event_id, source_event_id, source_event_tick, input_ack_events, map_first_event;
    size_t journal_map;
    Assembly assemblies[SRC_MAX_OLDPOS + 1];
    SentFrame sent[SRC_MAX_OLDPOS + 1];
    StateFrame *states;
    uint64_t rendered_frame, rendered_start_frame;
    double view_tick, render_time;
    Vec3 correction;
    Game render;
    Actor local_before;
    Vec3 rendered_poses[ACTOR_COUNT][21];
    Projectile *render_projectiles;
    size_t render_projectile_capacity;
    double last_hello;
    NetworkStats stats;
};

static unsigned char field[256][256], inverse[256];
static void repair_field(void) {
    if (inverse[1]) return;
    for (unsigned a = 0; a < 256; ++a)
        for (unsigned b = 0; b < 256; ++b) {
            unsigned x = a, y = b, result = 0;
            while (y) {
                if (y & 1) result ^= x;
                y >>= 1;
                x <<= 1;
                if (x & 256) x ^= 0x11d;
            }
            field[a][b] = (unsigned char)result;
            if (result == 1) inverse[a] = (unsigned char)b;
        }
}

static void repair_assembly(Assembly *assembly, size_t group, NetworkStats *stats) {
    size_t first = group * REPAIR_GROUP, end = first + REPAIR_GROUP;
    if (end > assembly->count) end = assembly->count;
    size_t missing[2], count = 0;
    for (size_t i = first; i < end; ++i)
        if (!assembly->fragments[i]) {
            if (count == 2) return;
            missing[count++] = i;
        }
    if (!count || !assembly->parity_received[group] || (count == 2 && assembly->parity_received[group] != 3)) return;
    unsigned char residual[2][STATE_DATA];
    memcpy(residual, assembly->parity + group * sizeof(residual), sizeof(residual));
    for (size_t fragment = first; fragment < end; ++fragment) {
        if (!assembly->fragments[fragment]) continue;
        size_t offset = fragment * STATE_DATA, size = assembly->size - offset;
        if (size > STATE_DATA) size = STATE_DATA;
        unsigned char coefficient = (unsigned char)(fragment - first + 1);
        for (size_t byte = 0; byte < size; ++byte) {
            unsigned char value = assembly->data[offset + byte];
            residual[0][byte] ^= value;
            residual[1][byte] ^= field[coefficient][value];
        }
    }
    unsigned char a = (unsigned char)(missing[0] - first + 1);
    unsigned char b = count == 2 ? (unsigned char)(missing[1] - first + 1) : 0;
    for (size_t byte = 0; byte < STATE_DATA; ++byte) {
        unsigned char recovered = count == 2 ? field[inverse[a ^ b]][residual[1][byte] ^ field[b][residual[0][byte]]] :
            (assembly->parity_received[group] & 1 ? residual[0][byte] : field[inverse[a]][residual[1][byte]]);
        size_t offset = missing[0] * STATE_DATA + byte;
        if (offset < assembly->size) assembly->data[offset] = recovered;
        if (count == 2) {
            offset = missing[1] * STATE_DATA + byte;
            if (offset < assembly->size) assembly->data[offset] = residual[0][byte] ^ recovered;
        }
    }
    for (size_t i = 0; i < count; ++i) assembly->fragments[missing[i]] = 1;
    assembly->received += count;
    stats->repaired_fragments += count;
}

static void fail(const char *operation) { perror(operation); exit(EXIT_FAILURE); }

double network_time(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) fail("clock_gettime");
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void put32(unsigned char *p, uint32_t value) {
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}
static uint32_t get32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static void put64(unsigned char *p, uint64_t value) { put32(p, value >> 32); put32(p + 4, value); }
static uint64_t get64(const unsigned char *p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }
static void putfloat(unsigned char *p, float value) { uint32_t bits; memcpy(&bits, &value, 4); put32(p, bits); }
static float getfloat(const unsigned char *p) { uint32_t bits = get32(p); float value; memcpy(&value, &bits, 4); return value; }

#if defined(__linux__)
typedef struct {
    struct mmsghdr messages[UIO_MAXIOV];
    struct iovec parts[UIO_MAXIOV][2];
    unsigned char headers[UIO_MAXIOV][STATE_HEADER];
    unsigned count;
} PacketBatch;

static void flush_packets(Network *net, PacketBatch *batch) {
    unsigned first = 0;
    while (first < batch->count) {
        int count = sendmmsg(net->socket, batch->messages + first, batch->count - first, 0);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) { ++first; continue; }
        if (count < 0) fail("sendmmsg");
        if (!count) abort();
        for (unsigned i = first; i < first + (unsigned)count; ++i) {
            size_t size = batch->parts[i][0].iov_len + batch->parts[i][1].iov_len;
            if (batch->messages[i].msg_len != size) { fprintf(stderr, "UDP datagram was incomplete\n"); abort(); }
            net->stats.sent_bytes += batch->messages[i].msg_len;
            ++net->stats.sent_packets;
        }
        first += (unsigned)count;
    }
    batch->count = 0;
}
#else
typedef void PacketBatch;
#endif

static void send_packet(Network *net, PacketBatch *batch, const struct sockaddr_in6 *address, PacketKind kind,
    uint64_t token, uint64_t sequence, uint64_t ack, uint32_t map, uint32_t total,
    uint32_t offset, uint64_t delivery, unsigned repairs, const unsigned char *data, size_t size) {
    unsigned char storage[DATAGRAM], *packet = storage;
#if defined(__linux__)
    if (batch) packet = batch->headers[batch->count];
#else
    (void)batch;
#endif
    size_t header = kind == STATE || kind == REPAIR ? STATE_HEADER : HEADER;
    put32(packet, MAGIC); put32(packet + 4, VERSION); put32(packet + 8, kind);
    put64(packet + 12, token); put64(packet + 20, sequence); put64(packet + 28, ack);
    put32(packet + 36, map); put32(packet + 40, total); put32(packet + 44, offset);
    put64(packet + 48, delivery); packet[56] = (unsigned char)repairs;
    if (header == STATE_HEADER) put64(packet + HEADER, net->next_event_id);
#if defined(__linux__)
    if (batch) {
        unsigned index = batch->count++;
        batch->parts[index][0] = (struct iovec){packet, header};
        batch->parts[index][1] = (struct iovec){(void *)data, size};
        batch->messages[index] = (struct mmsghdr){.msg_hdr = {
            .msg_name = (void *)address, .msg_namelen = sizeof(*address),
            .msg_iov = batch->parts[index], .msg_iovlen = 2}};
        if (batch->count == UIO_MAXIOV) flush_packets(net, batch);
        return;
    }
#endif
    if (size) memcpy(packet + header, data, size);
    ssize_t sent = sendto(net->socket, packet, header + size, 0, (const struct sockaddr *)address, sizeof(*address));
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)) return;
    if (sent < 0) fail("sendto");
    net->stats.sent_bytes += (uint64_t)sent;
    ++net->stats.sent_packets;
    if ((size_t)sent != header + size) { fprintf(stderr, "UDP datagram was incomplete\n"); abort(); }
}

static Network *create(Role role, unsigned short port) {
    repair_field();
    Network *net = calloc(1, sizeof(*net));
    if (!net) abort();
    net->role = role;
    net->local_actor = -1;
    net->socket = socket(AF_INET6, SOCK_DGRAM, 0);
    if (net->socket < 0) fail("socket");
    int dual_stack = 0;
    if (setsockopt(net->socket, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof(dual_stack)) != 0)
        fail("setsockopt IPV6_V6ONLY");
    struct sockaddr_in6 address = {.sin6_family = AF_INET6, .sin6_port = htons(port), .sin6_addr = IN6ADDR_ANY_INIT};
    if (bind(net->socket, (struct sockaddr *)&address, sizeof(address)) != 0) fail("bind");
    socklen_t address_length = sizeof(address);
    if (getsockname(net->socket, (struct sockaddr *)&address, &address_length) != 0) fail("getsockname");
    net->port = ntohs(address.sin6_port);
    int flags = fcntl(net->socket, F_GETFL);
    if (flags < 0 || fcntl(net->socket, F_SETFL, flags | O_NONBLOCK) != 0) fail("fcntl O_NONBLOCK");
    return net;
}

Network *network_host(unsigned short port, int local_actor, Game *game) {
    Network *net = create(HOST, port);
    net->local_actor = local_actor;
    net->status = NET_CONNECTED;
    net->map = net->journal_map = world_map_current;
    net->map_first_event = 1;
    strcpy(net->server_name, "Soldat 3D");
    if (local_actor < 0) game->actors[0].life = INACTIVE;
    snprintf(net->message, sizeof(net->message), "Hosting UDP %u", net->port);
    return net;
}

Network *network_join(const char *host, unsigned short port, const char *name) {
    Network *net = create(CLIENT, 0);
    struct addrinfo hints = {.ai_family = AF_INET6, .ai_socktype = SOCK_DGRAM, .ai_flags = AI_V4MAPPED};
    struct addrinfo *address;
    char service[6];
    snprintf(service, sizeof(service), "%u", port);
    int result = getaddrinfo(host, service, &hints, &address);
    if (result != 0) { fprintf(stderr, "Resolve %s: %s\n", host, gai_strerror(result)); exit(EXIT_FAILURE); }
    memcpy(&net->server.address, address->ai_addr, sizeof(net->server.address));
    freeaddrinfo(address);
    if (strlen(name) > PLAYER_NAME_LENGTH) { fprintf(stderr, "Player names allow %d characters\n", PLAYER_NAME_LENGTH); exit(EXIT_FAILURE); }
    strcpy(net->name, name);
    net->status = NET_CONNECTING;
    net->map = world_map_current;
    net->server.last_seen = network_time();
    net->states = calloc(SRC_MAX_OLDPOS + 1, sizeof(*net->states));
    if (!net->states) abort();
    snprintf(net->message, sizeof(net->message), "Connecting to %s:%u", host, port);
    return net;
}

static int same_address(const struct sockaddr_in6 *a, const struct sockaddr_in6 *b) {
    return a->sin6_port == b->sin6_port && a->sin6_scope_id == b->sin6_scope_id &&
        memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
}

int network_receive_at(Network *net, Game *game, double now) {
    net->authority_tick = game->tick;
    for (int i = 0; i < ACTOR_COUNT; ++i) net->spawn_ids[i] = game->actors[i].spawn_id;
    if (net->role == HOST) {
        combat_history_enable(game);
        if (net->map != world_map_current) {
            net->map = world_map_current;
            for (int i = 0; i < ACTOR_COUNT; ++i) {
                Peer *peer = &net->peers[i];
                peer->command_count = 0;
                peer->input = (Input){0};
                peer->started = 0;
                peer->applied_sequence = peer->received_sequence;
            }
        }
    }
    if (net->role == CLIENT && net->status == NET_CONNECTING && now - net->last_hello >= 1) {
        send_packet(net, NULL, &net->server.address, HELLO, 0, 0, 0, 0, 0, 0, 0, 0,
            (unsigned char *)net->name, strlen(net->name) + 1);
        net->last_hello = now;
    }
    int updated = 0;
    for (;;) {
        unsigned char packet[65536];
        struct sockaddr_in6 address;
        socklen_t address_length = sizeof(address);
        ssize_t received = recvfrom(net->socket, packet, sizeof(packet), 0, (struct sockaddr *)&address, &address_length);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (received < 0 && errno == EINTR) continue;
        if (received < 0) fail("recvfrom");
        net->stats.received_bytes += (uint64_t)received;
        ++net->stats.received_packets;
        if (net->role == HOST) {
            int humans = net->local_actor >= 0, bots = 0;
            for (int i = 0; i < ACTOR_COUNT; ++i) {
                humans += net->peers[i].joined;
                bots += i != net->local_actor && !net->peers[i].joined && game->actors[i].life != INACTIVE;
            }
            if (lobby_server_packet(net->socket, packet, (size_t)received, &address, game,
                net->server_name, humans, bots)) continue;
        }
        if (received < HEADER || get32(packet) != MAGIC || get32(packet + 4) != VERSION) {
            fprintf(stderr, "Rejected incompatible UDP packet\n"); continue;
        }
        PacketKind kind = get32(packet + 8);
        size_t header = kind == STATE || kind == REPAIR ? STATE_HEADER : HEADER;
        if ((size_t)received < header) continue;
        uint64_t token = get64(packet + 12), sequence = get64(packet + 20), ack = get64(packet + 28);
        uint32_t map = get32(packet + 36), total = get32(packet + 40), offset = get32(packet + 44);
        uint64_t delivery = get64(packet + 48);
        unsigned repairs = packet[56];
        size_t size = (size_t)received - header;
        unsigned char *data = packet + header;
        if (net->role == HOST) {
            int slot = -1;
            for (int i = 0; i < ACTOR_COUNT; ++i)
                if (net->peers[i].joined && same_address(&address, &net->peers[i].address)) slot = i;
            if (kind == HELLO || kind == CONNECT) {
                if (size < 1 || size > PLAYER_NAME_LENGTH + 1 || data[size - 1] != 0) continue;
                size_t admission = 0;
                while (admission < net->admission_count && !same_address(&address, &net->admissions[admission].address)) ++admission;
                if (kind == HELLO) {
                    if (admission == net->admission_count || now - net->admissions[admission].issued > (double)SRC_DISCONNECTION_TIME / TICK_RATE) {
                        if (admission == net->admission_count) {
                            if (net->admission_count < ACTOR_COUNT) ++net->admission_count;
                            else {
                                admission = 0;
                                for (size_t i = 1; i < net->admission_count; ++i)
                                    if (net->admissions[i].issued < net->admissions[admission].issued) admission = i;
                            }
                        }
                        uint64_t challenge;
                        int random = open("/dev/urandom", O_RDONLY);
                        if (random < 0) fail("open /dev/urandom");
                        if (read(random, &challenge, sizeof(challenge)) != sizeof(challenge)) fail("read /dev/urandom");
                        if (close(random) != 0) fail("close /dev/urandom");
                        net->admissions[admission] = (Admission){address, challenge, now};
                    }
                    send_packet(net, NULL, &address, CHALLENGE, net->admissions[admission].token, 0, 0, 0, 0, 0, 0, 0, NULL, 0);
                    continue;
                }
                if (slot < 0 || token != net->peers[slot].token) {
                    if (admission == net->admission_count || token != net->admissions[admission].token ||
                        now - net->admissions[admission].issued > (double)SRC_DISCONNECTION_TIME / TICK_RATE) continue;
                    net->admissions[admission] = net->admissions[--net->admission_count];
                }
                if (slot < 0) {
                    for (int i = 0; i < ACTOR_COUNT; ++i)
                        if (i != net->local_actor && !net->peers[i].joined && game->actors[i].life == INACTIVE) { slot = i; break; }
                    if (slot < 0)
                        for (int i = 0; i < ACTOR_COUNT; ++i)
                            if (i != net->local_actor && !net->peers[i].joined) { slot = i; break; }
                    if (slot < 0) {
                        const unsigned char full[] = "Server is full";
                        send_packet(net, NULL, &address, REJECT, 0, 0, 0, 0, 0, 0, 0, 0, full, sizeof(full));
                        continue;
                    }
                    free(net->peers[slot].commands);
                    net->peers[slot] = (Peer){.address = address, .token = token, .joined = 1, .last_seen = now, .joined_frame = net->frame, .event_ack = net->next_event_id};
                    objectives_drop(game, slot, 0);
                    memcpy(game->names[slot], data, size);
                    game->actors[slot].kills = game->actors[slot].deaths = game->actors[slot].captures = 0;
                    game->actors[slot].life = INACTIVE;
                    game->actors[slot].team = TEAM_SPECTATOR;
                    game_select_team(game, slot, TEAM_NONE);
                }
                unsigned char assigned[12]; put32(assigned, (uint32_t)slot); put64(assigned + 4, net->next_event_id);
                send_packet(net, NULL, &address, WELCOME, net->peers[slot].token, 0, 0,
                    (uint32_t)world_map_current, 0, 0, 0, 0, assigned, sizeof(assigned));
                continue;
            }
            if (slot < 0 || token != net->peers[slot].token) continue;
            Peer *peer = &net->peers[slot];
            peer->last_seen = now;
            if (ack > peer->confirmed_frame && ack > peer->joined_frame &&
                net->sent[ack % (SRC_MAX_OLDPOS + 1)].sequence == ack) peer->confirmed_frame = ack;
            if (kind == EVENT_ACK) {
                if (sequence > peer->event_ack && sequence <= net->next_event_id) peer->event_ack = sequence;
                continue;
            }
            if (kind == LEAVE) {
                objectives_drop(game, slot, 0);
                peer->joined = 0; game->actors[slot].life = INACTIVE; game->actors[slot].respawn_ticks = 0; continue;
            }
            if (kind != COMMAND || size < 5 || get32(data + 1) > DATAGRAM - HEADER || map != world_map_current) continue;
            if (sequence && sequence <= peer->delivery_sequence && sequence >= peer->receipt_sequence) {
                if (sequence > peer->receipt_sequence) {
                    uint64_t distance = sequence - peer->receipt_sequence;
                    peer->receipt_previous = peer->receipt_sequence;
                    peer->receipt_sequence = sequence;
                    peer->receipt_mask = distance < 64 ? peer->receipt_mask << distance : 0;
                }
                peer->receipt_mask |= (uint64_t)total << 32 | offset;
                uint64_t distance = sequence - peer->receipt_previous;
                uint64_t valid = sequence >= 64 ? UINT64_MAX : (UINT64_C(1) << sequence) - 1;
                uint64_t settled = distance < 64 ? valid & (UINT64_MAX << distance) : 0;
                if (settled) peer->delivery = (peer->receipt_mask & settled) == settled ? DELIVERY_CLEAN : DELIVERY_PROTECTED;
            }
            unsigned char command_data[DATAGRAM - HEADER];
            Snapshot commands = {.data = command_data};
            if (!snapshot_unpack_into(commands.data, sizeof(command_data), &commands.size, data, size)) continue;
            if (!commands.size || commands.size % COMMAND_SIZE != 0) continue;
            for (size_t at = 0; at < commands.size; at += COMMAND_SIZE) {
                const unsigned char *command = commands.data + at;
                PendingInput next = {.sequence = get64(command), .view_frame = get64(command + 8),
                    .input = {.right = getfloat(command + 16), .forward = getfloat(command + 20),
                        .yaw = getfloat(command + 24), .pitch = getfloat(command + 28), .held = get32(command + 32)},
                    .primary = command[36], .secondary = command[37], .team = command[38],
                    .spawn_id = get32(command + 40), .view_start_frame = get64(command + 96)};
                next.input.command_sequence = next.sequence;
                uint64_t view_bits = get64(command + 44);
                memcpy(&next.view_tick, &view_bits, sizeof(view_bits));
                for (int button = 0; button < BUTTONS; ++button) next.presses[button] = get32(command + 52 + button * 4);
                Input input = next.input;
                if (!(input.right >= -1.5f && input.right <= 1.5f && input.forward >= -1.5f && input.forward <= 1.5f &&
                    input.pitch >= -1.570797f && input.pitch <= 1.570797f && isfinite(input.yaw)) || next.primary > MINIGUN || next.secondary < COLT || next.secondary > LAW ||
                    input.held >= (1u << BUTTONS) || next.team > TEAM_SPECTATOR) { fprintf(stderr, "Rejected invalid player input\n"); continue; }
                if (next.team >= TEAM_ALPHA && next.team <= TEAM_DELTA && (!game_team_mode(game->mode) ||
                    (game->mode != MODE_TEAMMATCH && next.team > TEAM_BRAVO) || world_team_spawn_count(next.team) == 0)) continue;
                if (next.sequence > peer->received_sequence) {
                    if (peer->requested_team != next.team) {
                        game_select_team(game, slot, next.team);
                        peer->requested_team = next.team;
                    }
                    game_select_loadout(&game->actors[slot], next.primary, next.secondary);
                    peer->received_sequence = next.sequence;
                }
                if (peer->started && next.sequence <= peer->applied_sequence) continue;
                size_t position = 0;
                while (position < peer->command_count && peer->commands[position].sequence < next.sequence) ++position;
                if (position < peer->command_count && peer->commands[position].sequence == next.sequence) continue;
                if (peer->command_count == peer->command_capacity) {
                    peer->command_capacity = peer->command_capacity ? peer->command_capacity * 2 : TICK_RATE;
                    peer->commands = realloc(peer->commands, peer->command_capacity * sizeof(*peer->commands));
                    if (!peer->commands) abort();
                }
                memmove(peer->commands + position + 1, peer->commands + position,
                    (peer->command_count - position) * sizeof(*peer->commands));
                peer->commands[position] = next;
                ++peer->command_count;
            }
        } else {
            if (!same_address(&address, &net->server.address)) continue;
            if (kind == CHALLENGE && net->status == NET_CONNECTING && size == 0) {
                send_packet(net, NULL, &net->server.address, CONNECT, token, 0, 0, 0, 0, 0, 0, 0,
                    (unsigned char *)net->name, strlen(net->name) + 1);
                continue;
            }
            if (kind == REJECT && net->status == NET_CONNECTING && size > 0 && size < sizeof(net->message) && data[size - 1] == 0) {
                memcpy(net->message, data, size); net->status = NET_REJECTED; continue;
            }
            if (kind == WELCOME && net->status == NET_CONNECTING && size == 12 && get32(data) < ACTOR_COUNT && map < world_map_count) {
                net->server.token = token; net->local_actor = (int)get32(data); net->map = map;
                net->received_event_id = get64(data + 4);
                net->status = NET_CONNECTED; net->server.last_seen = now;
                snprintf(net->message, sizeof(net->message), "Connected as %s", net->name);
                continue;
            }
            if (net->status != NET_CONNECTED || token != net->server.token) continue;
            net->server.last_seen = now;
            if ((kind == STATE || kind == REPAIR) && delivery) {
                if (delivery > net->received_delivery) {
                    uint64_t distance = delivery - net->received_delivery;
                    net->delivery_mask = distance < 64 ? net->delivery_mask << distance : 0;
                    net->received_delivery = delivery;
                }
                uint64_t distance = net->received_delivery - delivery;
                if (distance < 64) net->delivery_mask |= UINT64_C(1) << distance;
            }
            if (kind == LEAVE) { net->status = NET_DISCONNECTED; strcpy(net->message, "Host disconnected"); continue; }
            if (kind == EVENTS) {
                if (size < 5 || get32(data + 1) > DATAGRAM - HEADER) continue;
                unsigned char event_data[DATAGRAM - HEADER];
                Snapshot decoded = {.data = event_data};
                if (!snapshot_unpack_into(decoded.data, sizeof(event_data), &decoded.size, data, size)) continue;
                if (!decoded.size || decoded.size % EVENT_SIZE != 0) continue;
                for (size_t at = 0; at < decoded.size; at += EVENT_SIZE) {
                    const unsigned char *entry = decoded.data + at;
                    JournalEvent current = {.id = get64(entry), .tick = get64(entry + 8), .command = get64(entry + 16),
                        .epoch = get64(entry + 24), .map = get32(entry + 32),
                        .event = {.kind = get32(entry + 36), .position = {getfloat(entry + 40), getfloat(entry + 44), getfloat(entry + 48)},
                            .actor = (int32_t)get32(entry + 52), .target = (int32_t)get32(entry + 56), .weapon = get32(entry + 60)}};
                    if (current.id <= net->received_event_id || current.map >= world_map_count ||
                        current.event.kind > EVENT_FLAG_CAPTURE || current.event.weapon >= WEAPON_COUNT ||
                        current.event.actor < -1 || current.event.actor >= ACTOR_COUNT) continue;
                    size_t position = 0;
                    while (position < net->journal_count && net->journal[position].id < current.id) ++position;
                    if (position < net->journal_count && net->journal[position].id == current.id) continue;
                    if (net->journal_count == net->journal_capacity) {
                        net->journal_capacity = net->journal_capacity ? net->journal_capacity * 2 : 64;
                        net->journal = realloc(net->journal, net->journal_capacity * sizeof(*net->journal));
                        if (!net->journal) abort();
                    }
                    memmove(net->journal + position + 1, net->journal + position, (net->journal_count - position) * sizeof(*net->journal));
                    net->journal[position] = current;
                    ++net->journal_count;
                }
                size_t consumed = 0;
                while (consumed < net->journal_count && net->journal[consumed].id == net->received_event_id + 1) {
                    JournalEvent current = net->journal[consumed++];
                    ++net->received_event_id;
                    int predicted = 0;
                    if (current.event.actor == net->local_actor)
                        for (size_t i = 0; i < net->predicted_event_count; ++i) {
                            const PredictedEvent *saved = &net->predicted_events[i];
                            if (saved->sequence == current.command && saved->weapon == current.event.weapon && saved->kind == current.event.kind) {
                                predicted = 1;
                                break;
                            }
                        }
                    if (predicted) continue;
                    if (net->presentation_count == net->presentation_capacity) {
                        net->presentation_capacity = net->presentation_capacity ? net->presentation_capacity * 2 : 64;
                        net->presentation_events = realloc(net->presentation_events, net->presentation_capacity * sizeof(*net->presentation_events));
                        if (!net->presentation_events) abort();
                    }
                    net->presentation_events[net->presentation_count++] = current;
                }
                net->journal_count -= consumed;
                if (consumed) memmove(net->journal, net->journal + consumed, net->journal_count * sizeof(*net->journal));
                send_packet(net, NULL, &net->server.address, EVENT_ACK, net->server.token, net->received_event_id, net->completed_frame,
                    (uint32_t)net->map, 0, 0, 0, 0, NULL, 0);
                continue;
            }
            if ((kind != STATE && kind != REPAIR) || map >= world_map_count || !total || repairs > 2) continue;
            size_t fragments = ((size_t)total + STATE_DATA - 1) / STATE_DATA;
            size_t groups = (fragments + REPAIR_GROUP - 1) / REPAIR_GROUP;
            if (kind == STATE && (offset >= total || size > total - offset || offset % STATE_DATA != 0 ||
                size != (total - offset < STATE_DATA ? total - offset : STATE_DATA))) continue;
            size_t group_bytes = kind == REPAIR && offset / 2 < groups ? total - (size_t)(offset / 2) * REPAIR_GROUP * STATE_DATA : 0;
            if (kind == REPAIR && (offset >= groups * 2 || offset % 2 >= repairs || size != (group_bytes < STATE_DATA ? group_bytes : STATE_DATA))) continue;
            uint64_t event_watermark = get64(packet + HEADER);
            if (ack > net->input_ack) {
                net->input_ack = ack;
                net->input_ack_events = event_watermark;
            } else if (ack == net->input_ack && event_watermark > net->input_ack_events)
                net->input_ack_events = event_watermark;
            if (ack > net->sequence) net->sequence = ack;
            if (sequence <= net->completed_frame) continue;
            Assembly *assembly = &net->assemblies[sequence % (SRC_MAX_OLDPOS + 1)];
            if (sequence < assembly->sequence) continue;
            if (sequence != assembly->sequence) {
                free(assembly->data); free(assembly->fragments); free(assembly->parity); free(assembly->parity_received);
                *assembly = (Assembly){.data = malloc(total), .size = total, .sequence = sequence, .ack = ack, .map = map,
                    .count = ((size_t)total + STATE_DATA - 1) / STATE_DATA, .repairs = repairs};
                assembly->fragments = calloc(assembly->count, 1);
                assembly->parity = calloc(groups * 2, STATE_DATA);
                assembly->parity_received = calloc(groups, 1);
                if (!assembly->data || !assembly->fragments || !assembly->parity || !assembly->parity_received) abort();
            }
            if (total != assembly->size || ack != assembly->ack || map != assembly->map || repairs != assembly->repairs) continue;
            size_t group;
            if (kind == STATE) {
                size_t fragment = offset / STATE_DATA;
                group = fragment / REPAIR_GROUP;
                memcpy(assembly->data + offset, data, size);
                if (!assembly->fragments[fragment]) { assembly->fragments[fragment] = 1; ++assembly->received; }
            } else {
                group = offset / 2;
                memcpy(assembly->parity + (size_t)offset * STATE_DATA, data, size);
                assembly->parity_received[group] |= 1u << (offset % 2);
            }
            repair_assembly(assembly, group, &net->stats);
            if (assembly->received != assembly->count) continue;
            if (assembly->size < 16) continue;
            uint64_t baseline = get64(assembly->data);
            const StateFrame *base = baseline ? &net->states[baseline % (SRC_MAX_OLDPOS + 1)] : NULL;
            if (base && base->sequence != baseline) continue;
            Snapshot unpacked = {0};
            int decoded = base ? replica_delta_unpack(&unpacked, &base->raw, assembly->data + 16, assembly->size - 16) :
                snapshot_unpack(&unpacked, assembly->data + 16, assembly->size - 16);
            if (!decoded) {
                net->status = NET_REJECTED; strcpy(net->message, "Host sent invalid packed state"); continue;
            }
            Actor previous = game->actors[net->local_actor];
            uint64_t previous_tick = game->tick;
            if (!replica_decode(game, unpacked.data, unpacked.size)) {
                free(unpacked.data);
                net->status = NET_REJECTED; strcpy(net->message, "Host sent invalid state"); continue;
            }
            if (map != net->map || game->tick < previous_tick) {
                for (int i = 0; i <= SRC_MAX_OLDPOS; ++i) { game_free(&net->states[i].game); free(net->states[i].raw.data); net->states[i] = (StateFrame){0}; }
                net->render_time = 0;
                net->pending_count = 0;
                net->predicted_event_count = 0;
            }
            const StateFrame *last_state = &net->states[net->completed_frame % (SRC_MAX_OLDPOS + 1)];
            uint64_t gap = last_state->sequence && game->tick >= last_state->game.tick ? game->tick - last_state->game.tick : 0;
            StateFrame *state = &net->states[sequence % (SRC_MAX_OLDPOS + 1)];
            if (!replica_decode(&state->game, unpacked.data, unpacked.size)) abort();
            free(state->raw.data); state->raw = unpacked;
            state->sequence = sequence; state->gap = gap; state->receipt = now; state->map = map;
            net->completed_frame = sequence;
            net->map = map;
            net->map_first_event = get64(assembly->data + 8);
            size_t acknowledged = 0;
            while (acknowledged < net->pending_count && net->pending[acknowledged].sequence <= ack) ++acknowledged;
            net->pending_count -= acknowledged;
            if (net->pending_count)
                memmove(net->pending, net->pending + acknowledged, net->pending_count * sizeof(*net->pending));
            Actor *local = &game->actors[net->local_actor];
            if (world_map_current == net->map && local->life == ALIVE)
                for (size_t i = 0; i < net->pending_count; ++i)
                    if (net->pending[i].spawn_id == local->spawn_id) {
                        net->local_before = *local;
                        local->controls = net->pending[i].input.held;
                        if (local->spawn_protection_ticks >= 0) --local->spawn_protection_ticks;
                        movement_step(local, net->pending[i].input, local->motion_tick++);
                        game->event_count = 0;
                        combat_predict_actor(game, net->local_actor, net->pending[i].input);
                    }
            if (local->spawn_id == previous.spawn_id && local->life == previous.life && world_map_current == net->map) {
                Vec3 error = sub(previous.position, local->position);
                net->correction = add(net->correction, error);
                net->stats.correction_distance += length(error);
                if (length(error) > net->stats.max_correction) net->stats.max_correction = length(error);
            } else net->correction = v3(0, 0, 0);
            ++net->stats.snapshots;
            updated = 1;
        }
    }
    if (net->role == HOST) {
        uint64_t confirmed = net->next_event_id;
        for (int i = 0; i < ACTOR_COUNT; ++i) {
            net->spawn_ids[i] = game->actors[i].spawn_id;
            if (net->peers[i].joined && net->peers[i].event_ack < confirmed) confirmed = net->peers[i].event_ack;
        }
        size_t consumed = 0;
        while (consumed < net->journal_count && net->journal[consumed].id <= confirmed) ++consumed;
        net->journal_count -= consumed;
        if (consumed) memmove(net->journal, net->journal + consumed, net->journal_count * sizeof(*net->journal));
    } else if (net->received_event_id >= net->input_ack_events) {
        size_t remaining = 0;
        for (size_t i = 0; i < net->predicted_event_count; ++i)
            if (net->predicted_events[i].sequence > net->input_ack) net->predicted_events[remaining++] = net->predicted_events[i];
        net->predicted_event_count = remaining;
    }
    if (updated) game->event_count = 0;
    if (net->role == HOST) {
        for (int i = 0; i < ACTOR_COUNT; ++i) {
            Peer *peer = &net->peers[i];
            if (peer->joined && now - peer->last_seen > (double)SRC_DISCONNECTION_TIME / TICK_RATE) {
                objectives_drop(game, i, 0);
                peer->joined = 0; game->actors[i].life = INACTIVE; game->actors[i].respawn_ticks = 0;
                fprintf(stderr, "%s disconnected after the source 15-second interval\n", game->names[i]);
            }
        }
    } else if ((net->status == NET_CONNECTED || net->status == NET_CONNECTING) &&
        now - net->server.last_seen > (double)SRC_DISCONNECTION_TIME / TICK_RATE) {
        net->status = NET_DISCONNECTED; strcpy(net->message, "Connection timed out after 15 seconds");
    }
    return updated;
}

int network_receive(Network *net, Game *game) { return network_receive_at(net, game, network_time()); }

void network_inputs(Network *net, Input inputs[ACTOR_COUNT]) {
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        Peer *peer = &net->peers[i];
        if (!peer->joined) continue;
        if (peer->spawn_id != net->spawn_ids[i]) {
            peer->input = (Input){0};
            peer->spawn_id = net->spawn_ids[i];
        }
        peer->input.pressed = 0;
        peer->input.rewind = (Rewind){0};
        if (!peer->started && peer->command_count) {
            peer->started = 1;
            peer->applied_sequence = peer->commands[0].sequence - 1;
        }
        if (peer->started) ++peer->applied_sequence;
        if (peer->command_count && peer->commands[0].sequence == peer->applied_sequence) {
            PendingInput command = peer->commands[0];
            --peer->command_count;
            memmove(peer->commands, peer->commands + 1, peer->command_count * sizeof(*peer->commands));
            peer->input = command.spawn_id == net->spawn_ids[i] ? command.input : (Input){.yaw = peer->input.yaw, .pitch = peer->input.pitch};
            for (int button = 0; button < BUTTONS; ++button) {
                if (command.spawn_id == net->spawn_ids[i] && command.presses[button] != peer->presses[button]) peer->input.pressed |= 1u << button;
                peer->presses[button] = command.presses[button];
            }
            const SentFrame *frame = &net->sent[command.view_frame % (SRC_MAX_OLDPOS + 1)];
            const SentFrame *start = &net->sent[command.view_start_frame % (SRC_MAX_OLDPOS + 1)];
            if (command.view_start_frame > peer->joined_frame && command.view_start_frame <= command.view_frame &&
                command.view_frame <= peer->confirmed_frame && frame->sequence == command.view_frame &&
                start->sequence == command.view_start_frame && start->map == net->map && frame->map == net->map && start->tick <= frame->tick) {
                double view = fmin((double)frame->tick, fmax((double)start->tick, command.view_tick));
                uint64_t applied = net->authority_tick + 1;
                if (applied >= frame->tick && applied - start->tick <= SRC_MAX_OLDPOS)
                    peer->input.rewind = (Rewind){REWIND_RENDERED, start->tick, frame->tick, applied,
                        start->tick == frame->tick ? 1 : (float)((view - start->tick) / (frame->tick - start->tick))};
            }
        } else if (peer->started) ++net->stats.extrapolated_inputs;
        peer->input.command_sequence = peer->applied_sequence;
        inputs[i] = peer->input;
        inputs[i].held |= inputs[i].pressed;
    }
}

void network_broadcast(Network *net, const Game *game) {
    int peers = 0;
    for (int i = 0; i < ACTOR_COUNT; ++i) peers += net->peers[i].joined;
    if (!peers) return;
    if (game->tick < net->source_event_tick || game->next_event_id < net->source_event_id || net->journal_map != world_map_current) {
        net->source_event_id = 0;
        net->map_first_event = net->next_event_id + 1;
        net->journal_map = world_map_current;
    }
    net->source_event_tick = game->tick;
    for (size_t i = 0; i < game->event_count; ++i) {
        uint64_t source_id = game->next_event_id - game->event_count + i + 1;
        if (source_id <= net->source_event_id) continue;
        if (net->journal_count == net->journal_capacity) {
            net->journal_capacity = net->journal_capacity ? net->journal_capacity * 2 : 64;
            net->journal = realloc(net->journal, net->journal_capacity * sizeof(*net->journal));
            if (!net->journal) abort();
        }
        GameEvent event = game->events[i];
        net->journal[net->journal_count++] = (JournalEvent){.id = ++net->next_event_id, .tick = game->tick,
            .command = event.actor >= 0 ? game->actors[event.actor].shot_sequence : 0,
            .epoch = net->map_first_event, .map = world_map_current, .event = event};
        net->source_event_id = source_id;
    }
    Snapshot raw = replica_encode(game);
    Snapshot keyframe = snapshot_pack(&raw);
    ++net->frame;
    SentFrame *frame = &net->sent[net->frame % (SRC_MAX_OLDPOS + 1)];
    free(frame->raw.data);
    *frame = (SentFrame){.sequence = net->frame, .tick = game->tick, .map = world_map_current, .raw = raw};
    typedef struct { uint64_t requested; size_t total, fragments, groups; unsigned char *data, *parity; } Payload;
    Payload payloads[ACTOR_COUNT];
    size_t prepared = 0;
    typedef struct { uint64_t requested; size_t first, count; Snapshot *packets; } EventPayload;
    EventPayload event_payloads[ACTOR_COUNT];
    size_t event_prepared = 0;
#if defined(__linux__)
    PacketBatch packets;
    packets.count = 0;
    PacketBatch *batch = &packets;
#else
    PacketBatch *batch = NULL;
#endif
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        Peer *peer = &net->peers[i];
        if (!peer->joined) continue;
        size_t cached = 0;
        while (cached < prepared && payloads[cached].requested != peer->confirmed_frame) ++cached;
        if (cached == prepared) {
            const SentFrame *base = &net->sent[peer->confirmed_frame % (SRC_MAX_OLDPOS + 1)];
            Snapshot delta = {0}, selected = keyframe;
            uint64_t baseline = 0;
            if (peer->confirmed_frame && base->sequence == peer->confirmed_frame && base->map == world_map_current) {
                delta = replica_delta_pack(&raw, &base->raw);
                if (delta.size < keyframe.size) { baseline = base->sequence; selected = delta; }
            }
            size_t total = selected.size + 16;
            if (total > UINT32_MAX) { fprintf(stderr, "Snapshot exceeds the protocol size field\n"); abort(); }
            size_t fragments = (total + STATE_DATA - 1) / STATE_DATA;
            size_t groups = (fragments + REPAIR_GROUP - 1) / REPAIR_GROUP;
            Payload *payload = &payloads[prepared++];
            *payload = (Payload){.requested = peer->confirmed_frame, .total = total, .fragments = fragments, .groups = groups,
                .data = malloc(total)};
            if (!payload->data) abort();
            put64(payload->data, baseline);
            put64(payload->data + 8, net->map_first_event);
            memcpy(payload->data + 16, selected.data, selected.size);
            free(delta.data);
        }
        Payload *payload = &payloads[cached];
        unsigned repairs = peer->delivery == DELIVERY_CLEAN ? 0 : 2;
        if (repairs && !payload->parity) {
            payload->parity = calloc(payload->groups * 2, STATE_DATA);
            if (!payload->parity) abort();
            for (size_t fragment = 0; fragment < payload->fragments; ++fragment) {
                size_t group = fragment / REPAIR_GROUP;
                unsigned char *p = payload->parity + group * 2 * STATE_DATA;
                unsigned char *q = p + STATE_DATA;
                size_t offset = fragment * STATE_DATA, size = payload->total - offset;
                if (size > STATE_DATA) size = STATE_DATA;
                unsigned char coefficient = (unsigned char)(fragment % REPAIR_GROUP + 1);
                for (size_t byte = 0; byte < size; ++byte) {
                    p[byte] ^= payload->data[offset + byte];
                    q[byte] ^= field[coefficient][payload->data[offset + byte]];
                }
            }
        }
        net->stats.raw_snapshot_bytes += raw.size;
        net->stats.packed_snapshot_bytes += payload->total;
        for (size_t offset = 0; offset < payload->total; offset += STATE_DATA) {
            size_t size = payload->total - offset;
            if (size > STATE_DATA) size = STATE_DATA;
            send_packet(net, batch, &peer->address, STATE, peer->token, net->frame, peer->applied_sequence,
                (uint32_t)world_map_current, (uint32_t)payload->total, (uint32_t)offset,
                ++peer->delivery_sequence, repairs, payload->data + offset, size);
        }
        for (size_t group = 0; group < payload->groups; ++group) {
            size_t size = payload->total - group * REPAIR_GROUP * STATE_DATA;
            if (size > STATE_DATA) size = STATE_DATA;
            for (unsigned repair = 0; repair < repairs; ++repair)
                send_packet(net, batch, &peer->address, REPAIR, peer->token, net->frame, peer->applied_sequence,
                    (uint32_t)world_map_current, (uint32_t)payload->total, (uint32_t)(group * 2 + repair),
                    ++peer->delivery_sequence, repairs,
                    payload->parity + (group * 2 + repair) * STATE_DATA, size);
        }
        size_t event_cached = 0;
        while (event_cached < event_prepared && event_payloads[event_cached].requested != peer->event_ack) ++event_cached;
        enum { EVENTS_PER_PACKET = (DATAGRAM - HEADER - 5) / EVENT_SIZE };
        if (event_cached == event_prepared) {
            size_t event = 0;
            while (event < net->journal_count && net->journal[event].id <= peer->event_ack) ++event;
            EventPayload *events = &event_payloads[event_prepared++];
            *events = (EventPayload){.requested = peer->event_ack, .first = event,
                .count = (net->journal_count - event + EVENTS_PER_PACKET - 1) / EVENTS_PER_PACKET};
            events->packets = events->count ? malloc(events->count * sizeof(*events->packets)) : NULL;
            if (events->count && !events->packets) abort();
            for (size_t part = 0; part < events->count; ++part) {
                unsigned char bytes[DATAGRAM - HEADER];
                size_t size = 0;
                while (event < net->journal_count && size + EVENT_SIZE + 5 <= sizeof(bytes)) {
                    const JournalEvent *current = &net->journal[event++];
                    unsigned char *out = bytes + size;
                    put64(out, current->id); put64(out + 8, current->tick); put64(out + 16, current->command);
                    put64(out + 24, current->epoch); put32(out + 32, (uint32_t)current->map); put32(out + 36, current->event.kind);
                    putfloat(out + 40, current->event.position.x); putfloat(out + 44, current->event.position.y); putfloat(out + 48, current->event.position.z);
                    put32(out + 52, (uint32_t)current->event.actor); put32(out + 56, (uint32_t)current->event.target); put32(out + 60, current->event.weapon);
                    size += EVENT_SIZE;
                }
                Snapshot event_bytes = {bytes, size};
                events->packets[part] = snapshot_pack(&event_bytes);
            }
        }
        const EventPayload *events = &event_payloads[event_cached];
        for (size_t part = 0; part < events->count; ++part) {
            size_t last = events->first + (part + 1) * EVENTS_PER_PACKET;
            if (last > net->journal_count) last = net->journal_count;
            Snapshot packet = events->packets[part];
            send_packet(net, batch, &peer->address, EVENTS, peer->token, net->journal[last - 1].id, 0,
                (uint32_t)world_map_current, 0, 0, 0, 0, packet.data, packet.size);
        }
    }
#if defined(__linux__)
    flush_packets(net, batch);
#endif
    for (size_t i = 0; i < prepared; ++i) { free(payloads[i].data); free(payloads[i].parity); }
    for (size_t i = 0; i < event_prepared; ++i) {
        for (size_t part = 0; part < event_payloads[i].count; ++part) free(event_payloads[i].packets[part].data);
        free(event_payloads[i].packets);
    }
    free(keyframe.data);
}

void network_send_input(Network *net, Game *game, Input input, WeaponId primary, WeaponId secondary) {
    game->event_count = 0;
    if (net->status != NET_CONNECTED) return;
    for (int i = 0; i < BUTTONS; ++i) if (input.pressed & (1u << i)) ++net->presses[i];
    if (net->pending_count == net->pending_capacity) {
        net->pending_capacity = net->pending_capacity ? net->pending_capacity * 2 : TICK_RATE;
        net->pending = realloc(net->pending, net->pending_capacity * sizeof(*net->pending));
        if (!net->pending) abort();
    }
    Actor *actor = &game->actors[net->local_actor];
    input.command_sequence = net->sequence + 1;
    PendingInput *next = &net->pending[net->pending_count++];
    *next = (PendingInput){.sequence = ++net->sequence, .input = input, .primary = primary, .secondary = secondary,
        .team = net->team, .spawn_id = actor->spawn_id, .view_frame = net->rendered_frame ? net->rendered_frame : net->completed_frame,
        .view_tick = net->render_time ? net->view_tick : (double)game->tick,
        .view_start_frame = net->rendered_start_frame ? net->rendered_start_frame : net->completed_frame};
    memcpy(next->presses, net->presses, sizeof(net->presses));
    size_t first = 0;
    while (first < net->pending_count && net->pending[first].sequence <= net->input_ack) ++first;
    while (first < net->pending_count) {
        unsigned char data[DATAGRAM - HEADER] = {0};
        size_t size = 0;
        while (first < net->pending_count && size + COMMAND_SIZE + 5 <= sizeof(data)) {
            const PendingInput *command = &net->pending[first++];
            unsigned char *out = data + size;
            put64(out, command->sequence); put64(out + 8, command->view_frame);
            putfloat(out + 16, command->input.right); putfloat(out + 20, command->input.forward);
            putfloat(out + 24, command->input.yaw); putfloat(out + 28, command->input.pitch);
            put32(out + 32, command->input.held); out[36] = command->primary; out[37] = command->secondary; out[38] = command->team;
            put32(out + 40, command->spawn_id);
            uint64_t view_bits; memcpy(&view_bits, &command->view_tick, sizeof(view_bits)); put64(out + 44, view_bits);
            for (int button = 0; button < BUTTONS; ++button) put32(out + 52 + button * 4, command->presses[button]);
            put64(out + 96, command->view_start_frame);
            size += COMMAND_SIZE;
        }
        Snapshot raw = {data, size};
        Snapshot packed = snapshot_pack(&raw);
        send_packet(net, NULL, &net->server.address, COMMAND, net->server.token, net->received_delivery, net->completed_frame,
            (uint32_t)net->map, (uint32_t)(net->delivery_mask >> 32), (uint32_t)net->delivery_mask, 0, 0, packed.data, packed.size);
        free(packed.data);
    }
    game->event_count = 0;
    net->local_before = *actor;
    actor->controls = input.held;
    if (actor->life == ALIVE && game->phase == MATCH_PLAYING) {
        if (actor->spawn_protection_ticks >= 0) --actor->spawn_protection_ticks;
        movement_step(actor, input, actor->motion_tick++);
        combat_predict_actor(game, net->local_actor, input);
    }
    for (size_t event = 0; event < game->event_count; ++event) {
        const GameEvent *current = &game->events[event];
        if (current->kind != EVENT_SHOT && current->kind != EVENT_DROP) continue;
        if (net->predicted_event_count == net->predicted_event_capacity) {
            net->predicted_event_capacity = net->predicted_event_capacity ? net->predicted_event_capacity * 2 : TICK_RATE;
            net->predicted_events = realloc(net->predicted_events, net->predicted_event_capacity * sizeof(*net->predicted_events));
            if (!net->predicted_events) abort();
        }
        net->predicted_events[net->predicted_event_count++] = (PredictedEvent){input.command_sequence, current->weapon, current->kind};
    }
}

void network_close(Network *net) {
    if (net->role == CLIENT && net->status == NET_CONNECTED)
        send_packet(net, NULL, &net->server.address, LEAVE, net->server.token, 0, 0, 0, 0, 0, 0, 0, NULL, 0);
    if (net->role == HOST)
        for (int i = 0; i < ACTOR_COUNT; ++i)
            if (net->peers[i].joined)
                send_packet(net, NULL, &net->peers[i].address, LEAVE, net->peers[i].token, 0, 0, 0, 0, 0, 0, 0, NULL, 0);
    if (close(net->socket) != 0) fail("close socket");
    for (int i = 0; i < ACTOR_COUNT; ++i) free(net->peers[i].commands);
    for (int i = 0; i <= SRC_MAX_OLDPOS; ++i) {
        free(net->assemblies[i].data); free(net->assemblies[i].fragments);
        free(net->assemblies[i].parity); free(net->assemblies[i].parity_received);
        free(net->sent[i].raw.data);
        if (net->states) { game_free(&net->states[i].game); free(net->states[i].raw.data); }
    }
    free(net->states); free(net->render_projectiles); free(net->pending); free(net->predicted_events);
    free(net->presentation_events); free(net->presented_events); free(net->journal); free(net);
}

int network_remote(const Network *net, int actor) { return net->peers[actor].joined; }
uint64_t network_received_sequence(const Network *net, int actor) { return net->peers[actor].received_sequence; }
int network_actor(const Network *net) { return net->local_actor; }
size_t network_map(const Network *net) { return net->map; }
unsigned short network_port(const Network *net) { return net->port; }
NetStatus network_status(const Network *net) { return net->status; }
const char *network_message(const Network *net) { return net->message; }
int network_socket(const Network *net) { return net->socket; }
void network_select_team(Network *net, Team team) { net->team = team; }
void network_set_name(Network *net, const char *name) {
    if (strlen(name) >= sizeof(net->server_name)) { fprintf(stderr, "Server name exceeds 63 characters\n"); exit(EXIT_FAILURE); }
    strcpy(net->server_name, name);
}

NetworkStats network_stats(const Network *net) { return net->stats; }
double network_view_tick(const Network *net) { return net->view_tick; }
const Vec3 *network_actor_pose(const Network *net, int actor) {
    return net->status == NET_CONNECTED && net->completed_frame ? net->rendered_poses[actor] : NULL;
}

const Game *network_render(Network *net, const Game *predicted, double now, float local_alpha) {
    if (net->role != CLIENT || !net->completed_frame || net->status != NET_CONNECTED) {
        net->render = *predicted;
        net->render.events = NULL;
        net->render.event_count = 0;
        return &net->render;
    }
    const StateFrame *latest = &net->states[net->completed_frame % (SRC_MAX_OLDPOS + 1)];
    double elapsed = net->render_time ? now - net->render_time : 0;
    double target = (double)latest->game.tick + (now - latest->receipt) * TICK_RATE - INTERPOLATION_TICKS;
    for (int i = 0; i <= SRC_MAX_OLDPOS; ++i) {
        const StateFrame *state = &net->states[i];
        if (!state->sequence || state->sequence + SRC_MAX_OLDPOS < net->completed_frame || state->map != net->map) continue;
        double reserve = fmax(INTERPOLATION_TICKS, (double)state->gap + 1);
        target = fmin(target, (double)state->game.tick + (now - state->receipt) * TICK_RATE - reserve);
    }
    if (!net->render_time) net->view_tick = fmax(0, target);
    else {
        double error = target - net->view_tick;
        double speed = 1 + fmax(-.2, fmin(.1, error * .1));
        net->view_tick = fmin((double)latest->game.tick, net->view_tick + elapsed * TICK_RATE * speed);
    }
    net->render_time = now;
    const StateFrame *before = NULL, *after = NULL;
    for (int i = 0; i <= SRC_MAX_OLDPOS; ++i) {
        const StateFrame *state = &net->states[i];
        if (!state->sequence || state->map != net->map) continue;
        if (state->game.tick <= net->view_tick && (!before || state->game.tick > before->game.tick)) before = state;
        if (state->game.tick >= net->view_tick && (!after || state->game.tick < after->game.tick)) after = state;
    }
    if (!before) before = after;
    if (!after) after = before;
    if (!before || !after) abort();
    net->rendered_frame = after->sequence;
    net->rendered_start_frame = before->sequence;
    net->view_tick = fmax((double)before->game.tick, fmin((double)after->game.tick, net->view_tick));
    float fraction = before->game.tick == after->game.tick ? 1 :
        (float)((net->view_tick - before->game.tick) / (after->game.tick - before->game.tick));
    net->render = *predicted;
    for (int i = 0; i < ACTOR_COUNT; ++i) {
        const Actor *a = &before->game.actors[i], *b = &after->game.actors[i];
        Actor *out = &net->render.actors[i];
        if (i == net->local_actor) {
            net->correction = scale(net->correction, expf((float)(-18 * elapsed)));
            out->position = add(add(out->previous, scale(sub(out->position, out->previous), local_alpha)), net->correction);
            out->previous = out->position;
            actor_pose_between(&net->local_before, &predicted->actors[i], local_alpha, net->rendered_poses[i]);
            for (int point = 0; point < 21; ++point) net->rendered_poses[i][point] = add(net->rendered_poses[i][point], net->correction);
            continue;
        }
        *out = *b;
        actor_pose_between(a, b, fraction, net->rendered_poses[i]);
        if (a->spawn_id == b->spawn_id && a->life == b->life) {
            out->position = add(a->position, scale(sub(b->position, a->position), fraction));
            out->yaw = a->yaw + atan2f(sinf(b->yaw - a->yaw), cosf(b->yaw - a->yaw)) * fraction;
            out->pitch = a->pitch + (b->pitch - a->pitch) * fraction;
            if (out->life == DEAD)
                for (int part = 0; part <= RAGDOLL_PART_COUNT; ++part) {
                    out->ragdoll.position[part] = add(a->ragdoll.position[part],
                        scale(sub(b->ragdoll.position[part], a->ragdoll.position[part]), fraction));
                    out->ragdoll.previous[part] = out->ragdoll.position[part];
                }
        }
        out->previous = out->position;
    }
    for (int i = 0; i < 3; ++i) {
        const Flag *a = &before->game.flags[i], *b = &after->game.flags[i];
        net->render.flags[i] = *b;
        if (a->state == b->state && a->carrier == b->carrier)
            net->render.flags[i].position = add(a->position, scale(sub(b->position, a->position), fraction));
        net->render.flags[i].previous = net->render.flags[i].position;
    }
    size_t count = after->game.projectile_count;
    if (count > net->render_projectile_capacity) {
        net->render_projectile_capacity = count;
        net->render_projectiles = realloc(net->render_projectiles, count * sizeof(*net->render_projectiles));
        if (!net->render_projectiles) abort();
    }
    size_t previous = 0;
    for (size_t i = 0; i < count; ++i) {
        Projectile projectile = after->game.projectiles[i];
        while (previous < before->game.projectile_count && before->game.projectiles[previous].id < projectile.id) ++previous;
        if (previous < before->game.projectile_count && before->game.projectiles[previous].id == projectile.id) {
            const Projectile *a = &before->game.projectiles[previous];
            projectile.position = add(a->position, scale(sub(projectile.position, a->position), fraction));
        }
        projectile.previous = projectile.position;
        net->render_projectiles[i] = projectile;
    }
    net->render.projectiles = net->render_projectiles;
    net->render.projectile_count = count;
    net->render.pickups = after->game.pickups;
    net->render.pickup_count = after->game.pickup_count;
    if (net->presentation_count > net->presented_capacity) {
        net->presented_capacity = net->presentation_count;
        net->presented_events = realloc(net->presented_events, net->presented_capacity * sizeof(*net->presented_events));
        if (!net->presented_events) abort();
    }
    size_t waiting = 0, presented = 0;
    for (size_t i = 0; i < net->presentation_count; ++i) {
        JournalEvent current = net->presentation_events[i];
        if (current.epoch < net->map_first_event) continue;
        if (current.epoch != net->map_first_event || current.map != net->map) {
            net->presentation_events[waiting++] = current;
            continue;
        }
        int local_confirmation = current.event.actor == net->local_actor &&
            (current.event.kind == EVENT_HIT || current.event.kind == EVENT_KILL);
        if (local_confirmation || current.tick <= net->view_tick) net->presented_events[presented++] = current.event;
        else net->presentation_events[waiting++] = current;
    }
    net->presentation_count = waiting;
    net->render.events = net->presented_events;
    net->render.event_count = presented;
    return &net->render;
}
