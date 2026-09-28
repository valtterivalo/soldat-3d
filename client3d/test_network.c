#include "game.h"
#include "network.h"
#include "snapshot.h"
#include "replica.h"
#include "pose.h"
#include "ragdoll.h"
#include "pickups.h"
#include "objectives.h"
#include "world.h"
#include "generated_rules.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *contract) {
    if (condition) return;
    fprintf(stderr, "Network contract failed: %s\n", contract);
    exit(EXIT_FAILURE);
}

static void connect_client(Network *server, Network *client, Game *host, Game *replica) {
    do {
        network_receive(client, replica);
        network_receive(server, host);
        network_receive(client, replica);
    } while (network_status(client) == NET_CONNECTING);
    if (network_status(client) != NET_CONNECTED) fprintf(stderr, "%s\n", network_message(client));
}

static int receive_state(Network *client, Game *game) {
    while (network_status(client) == NET_CONNECTED)
        if (network_receive(client, game)) return 1;
    return 0;
}

typedef struct {unsigned char *bytes;size_t size;} Datagram;

static Datagram receive_datagram_at(Network *receiver, int line) {
    double started=network_time();
    unsigned char bytes[65536];
    for (;;) {
        ssize_t size=recvfrom(network_socket(receiver),bytes,sizeof(bytes),0,NULL,NULL);
        if (size<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) {
            if (network_time()-started >= (double)SRC_DISCONNECTION_TIME/TICK_RATE) {
                fprintf(stderr, "Network contract failed: fixture receives datagram before source disconnect interval (caller line %d)\n", line);
                exit(EXIT_FAILURE);
            }
            continue;
        }
        check(size>=0,"fixture receives complete UDP datagram");
        if (size >= NETWORK_PACKET_HEADER && bytes[11] == 8) continue;
        Datagram packet={.size=(size_t)size,.bytes=malloc((size_t)size)};
        check(packet.bytes!=NULL,"allocate datagram fixture");memcpy(packet.bytes,bytes,(size_t)size);
        return packet;
    }
}

#define receive_datagram(receiver) receive_datagram_at(receiver, __LINE__)

static void send_datagram(Network *sender,Network *receiver,Datagram packet) {
    struct sockaddr_in6 address={.sin6_family=AF_INET6,.sin6_port=htons(network_port(receiver))};
    check(inet_pton(AF_INET6,"::ffff:127.0.0.1",&address.sin6_addr)==1,"fixture loopback endpoint");
    check(sendto(network_socket(sender),packet.bytes,packet.size,0,(struct sockaddr *)&address,sizeof(address))==(ssize_t)packet.size,"fixture relays original wire datagram");
}

typedef struct { Datagram packet; int due; } RelayPacket;
typedef enum { RELAY_DIRECT, RELAY_IMPAIRED } RelayMode;
typedef struct {
    int input, output;
    struct sockaddr_in6 target;
    RelayPacket *packets;
    size_t count, capacity;
    uint64_t received, delivered;
    unsigned captured, lost;
} Relay;

static int relay_socket(unsigned short *port) {
    int descriptor = socket(AF_INET6, SOCK_DGRAM, 0);
    check(descriptor >= 0, "create separate relay socket");
    int dual_stack = 0;
    check(setsockopt(descriptor, IPPROTO_IPV6, IPV6_V6ONLY, &dual_stack, sizeof(dual_stack)) == 0,
        "relay accepts mapped IPv4 endpoints");
    struct sockaddr_in6 address = {.sin6_family = AF_INET6, .sin6_addr = IN6ADDR_ANY_INIT};
    check(bind(descriptor, (struct sockaddr *)&address, sizeof(address)) == 0, "bind relay endpoint");
    socklen_t size = sizeof(address);
    check(getsockname(descriptor, (struct sockaddr *)&address, &size) == 0, "read relay endpoint");
    *port = ntohs(address.sin6_port);
    int flags = fcntl(descriptor, F_GETFL);
    check(flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0, "relay socket is nonblocking");
    return descriptor;
}

static void capture_relay(Relay *relay, Network *sender, int tick, RelayMode mode) {
    uint64_t expected = network_stats(sender).sent_packets;
    double started = network_time();
    while (relay->received < expected) {
        unsigned char bytes[65536];
        ssize_t size = recvfrom(relay->input, bytes, sizeof(bytes), 0, NULL, NULL);
        if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            check(network_time() - started < (double)SRC_DISCONNECTION_TIME / TICK_RATE,
                "every emitted loopback packet reaches the relay before source disconnect interval");
            continue;
        }
        check(size > 0, "latency fixture captures a real UDP datagram");
        ++relay->received;
        if (mode == RELAY_IMPAIRED) {
            ++relay->captured;
            if (relay->captured % 7 == 0) { ++relay->lost; continue; }
        }
        if (relay->count == relay->capacity) {
            relay->capacity = relay->capacity ? relay->capacity * 2 : 64;
            relay->packets = realloc(relay->packets, relay->capacity * sizeof(*relay->packets));
            check(relay->packets != NULL, "allocate latency fixture queue");
        }
        Datagram packet = {.bytes = malloc((size_t)size), .size = (size_t)size};
        check(packet.bytes != NULL, "allocate delayed datagram");
        memcpy(packet.bytes, bytes, (size_t)size);
        int delay = mode == RELAY_IMPAIRED ? 6 + (int)(relay->captured % 3) - 1 : 0;
        relay->packets[relay->count++] = (RelayPacket){packet, tick + delay};
    }
}

static void deliver_relay(Relay *relay, int tick, Network *receiver, Game *state, double now) {
    for (size_t i = relay->count; i > 0; --i) {
        RelayPacket packet = relay->packets[i - 1];
        if (packet.due > tick) continue;
        check(sendto(relay->output, packet.packet.bytes, packet.packet.size, 0,
            (struct sockaddr *)&relay->target, sizeof(relay->target)) == (ssize_t)packet.packet.size,
            "relay forwards its delayed datagram to the real destination");
        ++relay->delivered;
        free(packet.packet.bytes);
        relay->packets[i - 1] = relay->packets[--relay->count];
    }
    double started = network_time();
    do {
        network_receive_at(receiver, state, now);
        check(network_time() - started < (double)SRC_DISCONNECTION_TIME / TICK_RATE,
            "every forwarded packet reaches the protocol before source disconnect interval");
    } while (network_stats(receiver).received_packets < relay->delivered);
}

static void latency_contract(int population) {
    world_load(world_map_index("Arena2"));
    Game host, client;
    game_init(&host, 919, MODE_DEATHMATCH);
    game_init(&client, 717, MODE_DEATHMATCH);
    for (int i = 1; i < ACTOR_COUNT; ++i) {
        if (i < population) game_respawn(&host, i);
        else host.actors[i].life = INACTIVE;
    }
    Network *server = network_host(0, 0, &host);
    unsigned short front_port, back_port;
    int front = relay_socket(&front_port), back = relay_socket(&back_port);
    Network *peer = network_join("127.0.0.1", front_port, "Latency");
    Relay uplink = {.input = front, .output = back,
        .target = {.sin6_family = AF_INET6, .sin6_port = htons(network_port(server))}};
    Relay downlink = {.input = back, .output = front,
        .target = {.sin6_family = AF_INET6, .sin6_port = htons(network_port(peer))}};
    check(inet_pton(AF_INET6, "::ffff:127.0.0.1", &uplink.target.sin6_addr) == 1 &&
        inet_pton(AF_INET6, "::ffff:127.0.0.1", &downlink.target.sin6_addr) == 1, "relay endpoints use loopback");
    do {
        network_receive(peer, &client);
        capture_relay(&uplink, peer, 0, RELAY_DIRECT);
        deliver_relay(&uplink, 0, server, &host, network_time());
        capture_relay(&downlink, server, 0, RELAY_DIRECT);
        deliver_relay(&downlink, 0, peer, &client, network_time());
    } while (network_status(peer) == NET_CONNECTING);
    check(network_status(peer) == NET_CONNECTED, "client joins through the separate UDP relay");
    int local = network_actor(peer);
    host.actors[local].position = host.actors[local].previous = v3(0, 100000, 0);
    host.actors[0].position = host.actors[0].previous = v3(0, 100000, 100);
    network_broadcast(server, &host);
    capture_relay(&downlink, server, 0, RELAY_DIRECT);
    deliver_relay(&downlink, 0, peer, &client, network_time());
    check(network_stats(peer).snapshots == 1, "latency fixture starts from confirmed state");
    double started = network_time(), previous_view = 0;
    float previous_x = 0, maximum_step = 0;
    int rendered = 0, stalled = 0;
    double maximum_lag = 0, total_lag = 0;
    for (int tick = 0; tick < 600; ++tick) {
        Input input = {.forward = tick < 500 ? 1 : 0, .yaw = .3f};
        network_send_input(peer, &client, input, AK74, COLT);
        capture_relay(&uplink, peer, tick, RELAY_IMPAIRED);
        deliver_relay(&uplink, tick, server, &host, started + (double)tick / TICK_RATE);
        Input inputs[ACTOR_COUNT] = {0};
        for (int i = 1; i < population; ++i) if (i != local) inputs[i] = bot_input(&host, i);
        network_inputs(server, inputs);
        game_step(&host, inputs);
        host.actors[0].position.x = (float)host.tick * .4f;
        network_broadcast(server, &host);
        capture_relay(&downlink, server, tick, RELAY_IMPAIRED);
        deliver_relay(&downlink, tick, peer, &client, started + (double)tick / TICK_RATE);
        for (int subframe = 0; subframe < 4; ++subframe) {
            const Game *render = network_render(peer, &client, started + ((double)tick + subframe * .25) / TICK_RATE, subframe * .25f);
            double view = network_view_tick(peer);
            check(view >= previous_view, "jitter and reordering never reverse the remote render clock");
            if (tick > 120 && view == previous_view) ++stalled;
            if (tick > 120) {
                maximum_lag = fmax(maximum_lag, (double)host.tick - view);
                total_lag += (double)host.tick - view;
            }
            check(fabs(render->actors[0].position.x - (float)view * .4f) < .001f,
                "interpolated remote actor matches its exact rendered view timestamp");
            if (rendered) maximum_step = fmaxf(maximum_step, fabsf(render->actors[0].position.x - previous_x));
            previous_x = render->actors[0].position.x;
            previous_view = view;
            ++rendered;
        }
    }
    int drain_tick = 600;
    while (uplink.count || downlink.count || uplink.received < network_stats(peer).sent_packets ||
        downlink.received < network_stats(server).sent_packets) {
        int tick = drain_tick++;
        capture_relay(&uplink, peer, tick, RELAY_IMPAIRED);
        deliver_relay(&uplink, tick, server, &host, started + (double)tick / TICK_RATE);
        Input inputs[ACTOR_COUNT] = {0};
        network_inputs(server, inputs);
        game_step(&host, inputs);
        capture_relay(&downlink, server, tick, RELAY_IMPAIRED);
        deliver_relay(&downlink, tick, peer, &client, started + (double)tick / TICK_RATE);
    }
    check(!uplink.count && !downlink.count, "finite delay fixture drains all surviving datagrams");
    network_broadcast(server, &host);
    capture_relay(&downlink, server, drain_tick, RELAY_DIRECT);
    deliver_relay(&downlink, drain_tick, peer, &client, started + (double)drain_tick / TICK_RATE);
    while (client.tick < host.tick)
        check(receive_state(peer, &client), "final confirmation reconciles the delayed client");
    Snapshot authority = replica_encode(&host), replica = replica_encode(&client);
    if (authority.size != replica.size || memcmp(authority.data, replica.data, authority.size)) {
        size_t first = 0; while (first < authority.size && first < replica.size && authority.data[first] == replica.data[first]) ++first;
        fprintf(stderr,"DIFF pop=%d sizes=%zu/%zu first=%zu tick=%llu/%llu eventid=%llu/%llu count=%zu/%zu localpos=%.2f/%.2f motion=%llu/%llu received=%llu\n", population,authority.size,replica.size,first,(unsigned long long)host.tick,(unsigned long long)client.tick,(unsigned long long)host.next_event_id,(unsigned long long)client.next_event_id,host.event_count,client.event_count,host.actors[local].position.z,client.actors[local].position.z,(unsigned long long)host.actors[local].motion_tick,(unsigned long long)client.actors[local].motion_tick,(unsigned long long)network_received_sequence(server,local));
    }
    check(authority.size == replica.size && !memcmp(authority.data, replica.data, authority.size),
        "200ms RTT with loss and reordering converges to exact replicated state");
    check(maximum_step <= .112f, "240Hz remote presentation never jumps across late snapshots");
    NetworkStats up = network_stats(peer), down = network_stats(server);
    printf("Latency %d actors: 200ms RTT, loss %.1f%%/%.1f%%, 240Hz max remote step %.4f, correction mean %.4f max %.4f, traffic %.1f/%.1f KiB/s up/down, snapshots %llu/600, datagrams %.2f/update, repaired %llu, stalledframes %d/1916, viewlag mean %.3f max %.3f ticks\n",
        population, 100.0 * uplink.lost / uplink.captured, 100.0 * downlink.lost / downlink.captured, maximum_step,
        up.correction_distance / up.snapshots, up.max_correction, up.sent_bytes / 10240.0, down.sent_bytes / 10240.0, (unsigned long long)up.snapshots, down.sent_packets / 602.0, (unsigned long long)up.repaired_fragments, stalled, total_lag / 1916, maximum_lag);
    check(!stalled, "impaired remote presentation keeps moving");
    check(maximum_lag < 10, "known loss and jitter keep the remote view within ten source ticks");
    free(authority.data); free(replica.data); free(uplink.packets); free(downlink.packets);
    network_close(peer); network_close(server);
    check(close(front) == 0 && close(back) == 0, "close both relay sockets");
    game_free(&host); game_free(&client);
}

static void prediction_contract(void) {
    world_load(world_map_index("Arena2"));
    Game host, client;
    game_init(&host, 121, MODE_DEATHMATCH);
    game_init(&client, 343, MODE_DEATHMATCH);
    for (int i = 1; i < ACTOR_COUNT; ++i) host.actors[i].life = INACTIVE;
    Network *server = network_host(0, 0, &host);
    Network *peer = network_join("127.0.0.1", network_port(server), "Prediction");
    connect_client(server, peer, &host, &client);
    int local = network_actor(peer);
    host.actors[local].position = host.actors[local].previous = v3(0, 10000, 0);
    host.actors[local].spawn_protection_ticks = -1;
    game_equip(&host.actors[local], BARRETT, COLT);
    host.actors[local].slots[0].fire_count = 0;
    network_broadcast(server, &host);
    check(receive_state(peer, &client), "prediction fixture receives authoritative weapon state");
    int ammo = client.actors[local].slots[0].ammo;
    network_send_input(peer, &client, (Input){.held = INPUT_FIRE, .pressed = INPUT_FIRE}, BARRETT, COLT);
    check(client.event_count == 1 && client.events[0].kind == EVENT_SHOT && client.events[0].weapon == BARRETT &&
        client.actors[local].slots[0].ammo == ammo - 1, "local Barrett audio, flash and ammo respond on the first press before a server round trip");
    check(!client.projectile_count, "local weapon feedback never invents authoritative projectile hits");
    while (network_received_sequence(server, local) < 1) network_receive(server, &host);
    Input inputs[ACTOR_COUNT] = {0};
    network_inputs(server, inputs);
    game_step(&host, inputs);
    network_broadcast(server, &host);
    check(receive_state(peer, &client), "server confirms predicted shot");
    check(!client.event_count && client.actors[local].slots[0].ammo == host.actors[local].slots[0].ammo,
        "confirmed shot reconciles ammo without replaying local audio or flash");
    host.event_count = 0;
    ++host.tick;
    combat_event(&host, (GameEvent){EVENT_SHOT, host.actors[0].position, 0, -1, AK74});
    network_broadcast(server, &host);
    check(receive_state(peer, &client), "remote shot event is received authoritatively");
    double now = network_time();
    const Game *scene = network_render(peer, &client, now, 1);
    check(!scene->event_count, "remote effects wait for their rendered snapshot timestamp");
    scene = network_render(peer, &client, now + 4.0 / TICK_RATE, 1);
    check(scene->event_count == 1 && scene->events[0].actor == 0,
        "remote shot becomes visible when its actor reaches the event timestamp");
    scene = network_render(peer, &client, now + 5.0 / TICK_RATE, 1);
    check(!scene->event_count, "presentation events drain exactly once");
    network_receive(server, &host);
    host.event_count = 0;
    ++host.tick;
    host.actors[0].health -= 10;
    uint64_t hit_tick = host.tick;
    combat_event(&host, (GameEvent){EVENT_HIT, host.actors[0].position, local, 0, BARRETT});
    network_broadcast(server, &host);
    for (;;) {
        Datagram packet = receive_datagram(peer);
        unsigned kind = packet.bytes[11];
        free(packet.bytes);
        if (kind == 9) break;
    }
    check(client.actors[0].health != host.actors[0].health, "fixture loses the complete hit snapshot and event datagram");
    host.event_count = 0;
    ++host.tick;
    network_broadcast(server, &host);
    Datagram *retry = NULL;
    size_t retry_count = 0;
    for (;;) {
        Datagram packet = receive_datagram(peer);
        retry = realloc(retry, (retry_count + 1) * sizeof(*retry));
        check(retry != NULL, "allocate captured event retry");
        retry[retry_count++] = packet;
        if (packet.bytes[11] == 9) break;
    }
    for (size_t i = retry_count; i > 0; --i) { send_datagram(server, peer, retry[i - 1]); free(retry[i - 1].bytes); }
    free(retry);
    check(receive_state(peer, &client), "later snapshot recovers canonical health after an entirely lost update");
    scene = network_render(peer, &client, now + 5.0 / TICK_RATE, 1);
    check(client.actors[0].health == host.actors[0].health && scene->event_count == 1 && scene->events[0].kind == EVENT_HIT,
        "unacknowledged hit confirmation survives complete update loss and reordered retransmission");
    check(network_view_tick(peer) < hit_tick, "local hit confirmation bypasses remote interpolation delay");
    Datagram lost_ack = receive_datagram(server);
    check(lost_ack.bytes[11] == 10, "fixture discards the reliable event acknowledgement");
    free(lost_ack.bytes);
    network_broadcast(server, &host);
    check(receive_state(peer, &client), "lost event acknowledgement triggers a retransmission");
    scene = network_render(peer, &client, now + 7.0 / TICK_RATE, 1);
    check(!scene->event_count, "retransmitted hit confirmation never duplicates presentation");
    network_close(server);
    while (network_status(peer) == NET_CONNECTED) network_receive(peer, &client);
    network_send_input(peer, &client, (Input){.held = INPUT_FIRE}, BARRETT, COLT);
    check(!client.event_count && !network_actor_pose(peer, local) &&
        !network_render(peer, &client, now + 8.0 / TICK_RATE, 1)->event_count,
        "disconnected sessions cannot repeat stale effects");
    network_close(peer);
    game_free(&host); game_free(&client);
}

static unsigned relay_delivery_frame(Network *server, Network *peer, Game *replica, int lose_data) {
    Datagram first = receive_datagram(peer);
    check(first.bytes[11] == 4, "delivery fixture captures a state datagram");
    uint32_t total;
    memcpy(&total, first.bytes + 40, 4); total = ntohl(total);
    size_t fragments = (total + NETWORK_PACKET_DATA - 1) / NETWORK_PACKET_DATA;
    check(fragments == 1, "compact two-player state fits one datagram");
    unsigned repairs = first.bytes[56];
    size_t count = 1 + repairs;
    Datagram packets[3]; packets[0] = first;
    for (size_t i = 1; i < count; ++i) packets[i] = receive_datagram(peer);
    uint64_t expected = network_stats(peer).received_packets + count - (unsigned)lose_data;
    for (size_t i = count; i > (unsigned)lose_data; --i) send_datagram(server, peer, packets[i - 1]);
    while (network_stats(peer).received_packets < expected) network_receive(peer, replica);
    for (size_t i = 0; i < count; ++i) free(packets[i].bytes);
    return repairs;
}

static void delivery_contract(void) {
    world_load(world_map_index("Arena2"));
    Game host, replica;
    game_init(&host, 731, MODE_DEATHMATCH); game_init(&replica, 849, MODE_DEATHMATCH);
    for (int i = 1; i < ACTOR_COUNT; ++i) host.actors[i].life = INACTIVE;
    host.score_limit = host.time_limit_ticks = 0;
    Network *server = network_host(0, 0, &host);
    Network *peer = network_join("127.0.0.1", network_port(server), "Delivery");
    connect_client(server, peer, &host, &replica);
    int local = network_actor(peer);
    host.actors[0].position = host.actors[0].previous = v3(0, 10000, 100);
    host.actors[local].position = host.actors[local].previous = v3(0, 10000, 0);
    network_broadcast(server, &host);
    check(relay_delivery_frame(server, peer, &replica, 1) == 2 && network_stats(peer).snapshots == 1,
        "startup repairs recover a lost state before delivery quality is known");
    enum { CLEAN_WARMUP, LOSS_STREAM, CLEAN_RECOVERY, COMPLETE } phase = CLEAN_WARMUP;
    unsigned loss_frames = 0, protected_losses = 0;
    uint64_t sequence = 0;
    double started = network_time();
    while (phase != COMPLETE) {
        network_send_input(peer, &replica, (Input){0}, AK74, COLT);
        ++sequence;
        while (network_received_sequence(server, local) < sequence) network_receive(server, &host);
        Input inputs[ACTOR_COUNT] = {0}; network_inputs(server, inputs); game_step(&host, inputs);
        network_broadcast(server, &host);
        int lose = phase == LOSS_STREAM && (loss_frames % 3 == 0 || (loss_frames >= 6 && loss_frames < 9));
        unsigned repairs = relay_delivery_frame(server, peer, &replica, lose);
        if (phase == CLEAN_WARMUP && repairs == 0) phase = LOSS_STREAM;
        else if (phase == LOSS_STREAM) {
            if (lose && repairs) {
                ++protected_losses;
                check(replica.tick == host.tick, "adaptive repair recovers physical loss without waiting for another state");
            }
            if (++loss_frames == 12) {
                check(protected_losses > 0 && repairs > 0,
                    "physical receipt gaps retain protection even when repair concealed the state loss");
                phase = CLEAN_RECOVERY;
            }
        } else if (phase == CLEAN_RECOVERY && repairs == 0) phase = COMPLETE;
        check(network_time() - started < (double)SRC_DISCONNECTION_TIME / TICK_RATE,
            "reordered clean delivery disables redundant repair before the disconnect interval");
    }
    check(replica.tick == host.tick, "clean recovery preserves the current authoritative replica");
    network_close(peer); network_close(server); game_free(&host); game_free(&replica);
}

static void blocked_spawn_contract(void) {
    world_load(world_map_index("Arena"));
    Game game;
    game_init(&game, 837, MODE_TEAMMATCH);
    for (int i = 0; i < ACTOR_COUNT; ++i) game.actors[i].life = INACTIVE;
    Actor *waiting = &game.actors[0];
    waiting->team = TEAM_DELTA;
    check(game_respawn(&game, 0) == SPAWN_READY, "blocked-spawn fixture starts with a valid soldier");
    ragdoll_start(waiting);
    waiting->life = DEAD;
    size_t count = world_team_spawn_count(TEAM_DELTA);
    unsigned char covered[count];
    memset(covered, 0, count);
    size_t remaining = count;
    int blockers = 0;
    while (remaining) {
        size_t best = 0;
        Vec3 position = {0};
        for (size_t a = 0; a < count; ++a)
            for (size_t b = a; b < count; ++b) {
                Vec3 midpoint = scale(add(world_team_spawn(TEAM_DELTA, a), world_team_spawn(TEAM_DELTA, b)), .5f);
                size_t blocked = 0;
                for (size_t c = 0; c < count; ++c) {
                    Vec3 delta = sub(midpoint, world_team_spawn(TEAM_DELTA, c));
                    blocked += !covered[c] && fabsf(delta.y) < actor_height(STANDING) &&
                        delta.x * delta.x + delta.z * delta.z < 4 * SRC_PART_RADIUS * SRC_PART_RADIUS;
                }
                if (blocked > best) { best = blocked; position = midpoint; }
            }
        check(best > 0 && blockers + 1 < ACTOR_COUNT, "31 living soldiers can occupy every candidate in the smallest team base");
        Actor *blocker = &game.actors[++blockers];
        blocker->life = ALIVE;
        blocker->team = TEAM_DELTA;
        blocker->position = blocker->previous = position;
        blocker->health = SRC_DEFAULT_HEALTH;
        blocker->pose = STANDING;
        blocker->spawn_protection_ticks = SRC_DEFAULT_CEASEFIRE_TIME;
        for (size_t c = 0; c < count; ++c) {
            Vec3 delta = sub(position, world_team_spawn(TEAM_DELTA, c));
            if (!covered[c] && fabsf(delta.y) < actor_height(STANDING) &&
                delta.x * delta.x + delta.z * delta.z < 4 * SRC_PART_RADIUS * SRC_PART_RADIUS) {
                covered[c] = 1;
                --remaining;
            }
        }
    }
    uint32_t spawn = waiting->spawn_id, random = game.random;
    Vec3 bone = waiting->ragdoll.position[1];
    check(game_respawn(&game, 0) == SPAWN_BLOCKED && waiting->spawn_id == spawn && game.random == random &&
        length(sub(waiting->ragdoll.position[1], bone)) == 0 && waiting->respawn_ticks == 1,
        "occupied base defers respawn without changing corpse, generation or random stream");
    Input inputs[ACTOR_COUNT] = {0};
    game_step(&game, inputs);
    check(waiting->life == DEAD && waiting->spawn_id == spawn && waiting->respawn_ticks == 1,
        "blocked corpse retries on the next simulation tick");
    game_select_team(&game, 0, TEAM_SPECTATOR);
    game_step(&game, inputs);
    check(waiting->life == INACTIVE && waiting->respawn_ticks == 0 && waiting->spawn_id == spawn,
        "spectator selection cancels a queued respawn");
    game_select_team(&game, 0, TEAM_DELTA);
    check(waiting->life == INACTIVE && waiting->respawn_ticks == 1, "joining an occupied team queues its first spawn");
    for (int i = 1; i < ACTOR_COUNT; ++i) game.actors[i].life = INACTIVE;
    game_step(&game, inputs);
    check(waiting->life == ALIVE && waiting->spawn_id == spawn + 1 && world_pose_clear(waiting->position, waiting->pose),
        "queued team join spawns on the first tick with legal free space");
    game_free(&game);
}

int main(void) {
    world_init(); poses_init(); ragdolls_init();
    {
        Game host, replica;
        game_init(&host, 73, MODE_DEATHMATCH); game_init(&replica, 84, MODE_DEATHMATCH);
        Network *server = network_host(0, 0, &host);
        Network *client = network_join("127.0.0.1", network_port(server), "Admission");
        Network *other = network_join("127.0.0.1", network_port(server), "Other endpoint");
        Snapshot before = snapshot_encode(&host);
        double now = network_time();
        network_receive_at(client, &replica, now);
        Datagram hello = receive_datagram(server);
        send_datagram(client, server, hello);
        while (!network_stats(server).sent_packets) network_receive(server, &host);
        Datagram challenge = receive_datagram(client);
        check(challenge.size == NETWORK_PACKET_HEADER && challenge.bytes[11] == 11 && challenge.size <= hello.size,
            "unverified hello receives only a non-amplifying challenge");
        Snapshot after = snapshot_encode(&host);
        check(before.size == after.size && !memcmp(before.data, after.data, before.size),
            "unverified hello does not allocate a player or mutate the match");
        free(before.data); free(after.data);
        NetworkStats sent = network_stats(server);
        network_broadcast(server, &host);
        check(network_stats(server).sent_bytes == sent.sent_bytes,
            "unverified endpoint receives no snapshot stream");
        hello.bytes[11] = 12;
        memcpy(hello.bytes + 12, challenge.bytes + 12, 8);
        uint64_t received = network_stats(server).received_packets;
        send_datagram(other, server, hello);
        while (network_stats(server).received_packets == received) network_receive(server, &host);
        check(network_stats(server).sent_bytes == sent.sent_bytes,
            "challenge cannot be redeemed from a different endpoint");
        hello.bytes[12] ^= 1;
        received = network_stats(server).received_packets;
        send_datagram(client, server, hello);
        while (network_stats(server).received_packets == received) network_receive(server, &host);
        check(network_stats(server).sent_bytes == sent.sent_bytes,
            "incorrect challenge cannot allocate a player");
        hello.bytes[12] ^= 1;
        send_datagram(client, server, hello);
        while (network_stats(server).sent_packets == sent.sent_packets) network_receive(server, &host);
        Datagram lost_welcome = receive_datagram(client);
        check(lost_welcome.bytes[11] == 2, "verified endpoint receives welcome");
        uint32_t slot_bits; memcpy(&slot_bits, lost_welcome.bytes + NETWORK_PACKET_HEADER, 4);
        int slot = (int)ntohl(slot_bits);
        uint32_t spawn = host.actors[slot].spawn_id;
        check(network_remote(server, slot), "verified endpoint owns an authoritative player slot");
        network_receive_at(client, &replica, now + 1);
        connect_client(server, client, &host, &replica);
        check(network_status(client) == NET_CONNECTED && network_actor(client) == slot && host.actors[slot].spawn_id == spawn,
            "lost welcome retries admission without duplicating or respawning the player");
        free(hello.bytes); free(challenge.bytes); free(lost_welcome.bytes);

        network_broadcast(server, &host);
        check(receive_state(client, &replica), "verified endpoint receives authoritative state");
        network_send_input(client, &replica, (Input){.yaw = .4f}, AK74, COLT);
        Datagram command = receive_datagram(server);
        Snapshot raw = {0};
        check(command.bytes[11] == 3 && snapshot_unpack(&raw, command.bytes + NETWORK_PACKET_HEADER, command.size - NETWORK_PACKET_HEADER) && raw.size == 104,
            "fixture intercepts one real encoded player command");
        uint32_t valid_yaw; memcpy(&valid_yaw, raw.data + 24, 4);
        const uint32_t invalid_yaw[] = {0x7fc00000u, 0x7f800000u, 0xff800000u};
        for (size_t i = 0; i < sizeof(invalid_yaw) / sizeof(invalid_yaw[0]); ++i) {
            uint32_t bits = htonl(invalid_yaw[i]); memcpy(raw.data + 24, &bits, 4);
            Snapshot packed = snapshot_pack(&raw);
            command.size = NETWORK_PACKET_HEADER + packed.size;
            command.bytes = realloc(command.bytes, command.size); check(command.bytes != NULL, "encode malformed input fixture");
            memcpy(command.bytes + NETWORK_PACKET_HEADER, packed.data, packed.size); free(packed.data);
            received = network_stats(server).received_packets;
            send_datagram(client, server, command);
            while (network_stats(server).received_packets == received) network_receive(server, &host);
            check(network_received_sequence(server, slot) == 0, "non-finite yaw is rejected before authoritative command acceptance");
            Input inputs[ACTOR_COUNT] = {0}; network_inputs(server, inputs);
            check(inputs[slot].yaw == 0, "malformed yaw never reaches simulation input");
        }
        memcpy(raw.data + 24, &valid_yaw, 4);
        Snapshot packed = snapshot_pack(&raw);
        command.size = NETWORK_PACKET_HEADER + packed.size;
        command.bytes = realloc(command.bytes, command.size); check(command.bytes != NULL, "restore valid command fixture");
        memcpy(command.bytes + NETWORK_PACKET_HEADER, packed.data, packed.size);
        send_datagram(client, server, command);
        while (!network_received_sequence(server, slot)) network_receive(server, &host);
        Input inputs[ACTOR_COUNT] = {0}; network_inputs(server, inputs);
        check(inputs[slot].yaw == .4f, "valid input still works after malformed commands are rejected");
        free(raw.data); free(packed.data); free(command.bytes);
        network_close(client); network_close(other); network_close(server);
        game_free(&host); game_free(&replica);
    }
    Game host, left, right;
    game_init(&host, 123, MODE_DEATHMATCH); game_init(&left, 456, MODE_DEATHMATCH); game_init(&right, 789, MODE_DEATHMATCH);
    host.actors[1].position.y += 40;
    ragdoll_start(&host.actors[1]);
    host.actors[1].life = DEAD;
    host.actors[1].respawn_ticks = 180;
    ragdoll_dismember(&host.actors[1], 12);
    pickups_spawn(&host, PICKUP_WEAPON, AK74, v3(0, 40, 0), v3(1, 2, 3), 17, 0);
    Snapshot encoded = snapshot_encode(&host);
    check(encoded.size > 1200, "state exceeds one internet-safe UDP datagram");
    check(snapshot_decode(&left, encoded.data, encoded.size), "snapshot decodes");
    Snapshot copy = snapshot_encode(&left);
    check(copy.size == encoded.size && !memcmp(copy.data, encoded.data, copy.size), "canonical state round trip includes ragdolls and pickups");
    uint64_t before = left.tick;
    check(!snapshot_decode(&left, encoded.data, encoded.size - 1) && left.tick == before, "truncated state rejected atomically");
    free(encoded.data); free(copy.data);

    Network *server = network_host(0, 0, &host);
    Network *a = network_join("127.0.0.1", network_port(server), "Alice");
    Network *b = network_join("::1", network_port(server), "Bob");
    connect_client(server, a, &host, &left);
    connect_client(server, b, &host, &right);
    check(network_status(a) == NET_CONNECTED && network_status(b) == NET_CONNECTED, "IPv4 and IPv6 clients join");
    check(network_actor(a) != network_actor(b) && network_actor(a) != 0, "players get distinct authoritative slots");
    int alice = network_actor(a), bob = network_actor(b);
    check(!strcmp(host.names[alice], "Alice") && !strcmp(host.names[bob], "Bob"), "player names replicate");
    network_broadcast(server, &host);
    check(receive_state(a, &left) && receive_state(b, &right), "fragmented authoritative state reaches both clients");
    for (int tick = 0; tick < 120; ++tick) {
        Input move_a = {.forward = 1, .yaw = .4f, .held = tick < 40 ? INPUT_JETS : 0};
        Input move_b = {.right = -1, .yaw = -.6f, .held = tick < 60 ? INPUT_FIRE : 0};
        if (tick == 20) move_a.pressed = INPUT_GRENADE;
        network_send_input(a, &left, move_a, AK74, COLT);
        network_send_input(b, &right, move_b, MP5, KNIFE);
        while (network_received_sequence(server, alice) < (uint64_t)tick + 1 ||
            network_received_sequence(server, bob) < (uint64_t)tick + 1) {
            network_receive(server, &host);
            network_receive(a, &left); network_receive(b, &right);
            check(network_status(a) == NET_CONNECTED && network_status(b) == NET_CONNECTED, "input arrives before the source disconnect interval");
        }
        Input inputs[ACTOR_COUNT] = {0};
        network_inputs(server, inputs);
        if (tick == 20) check(inputs[alice].pressed & INPUT_GRENADE, "short button taps survive the network");
        if (tick == 21) check(!(inputs[alice].pressed & INPUT_GRENADE), "acknowledged taps are not repeated");
        game_step(&host, inputs);
        network_broadcast(server, &host);
        check(receive_state(a, &left) && receive_state(b, &right), "state stream advances every server tick");
        Snapshot authority = replica_encode(&host), replica = replica_encode(&left);
        check(authority.size == replica.size && !memcmp(authority.data, replica.data, authority.size), "client reconciles to authoritative combat and movement");
        free(authority.data); free(replica.data);
    }
    game_respawn(&host, alice);
    for (int selection = 0; selection < 2; ++selection) {
        network_send_input(a, &left, (Input){0}, MP5, KNIFE);
        while (network_received_sequence(server, alice) < (uint64_t)121 + selection) {
            network_receive(server, &host);
            network_receive(a, &left);
            check(network_status(a) == NET_CONNECTED, "loadout arrives before disconnect");
        }
        Actor *player = &host.actors[alice];
        check(player->slots[0].id == MP5 && player->slots[1].id == KNIFE,
            "menu selection equips the joined player immediately");
        if (selection == 0) player->slots[0].ammo = 3;
        else check(player->slots[0].ammo == 3, "repeated loadout packets do not refill ammunition");
    }
    Actor *player = &host.actors[alice];
    player->spawn_protection_ticks = -1;
    uint64_t selection_command = network_received_sequence(server, alice) + 1;
    network_send_input(a, &left, (Input){0}, M249, LAW);
    while (network_received_sequence(server, alice) < selection_command) network_receive(server, &host);
    check(player->slots[0].id == MP5 && player->slots[0].ammo == 3 &&
        player->loadout[0] == M249 && player->loadout[1] == LAW,
        "selection after spawn protection queues next loadout without replacing or refilling the current gun");
    player->life = DEAD;
    game_select_loadout(player, M249, LAW);
    check(player->slots[0].id == MP5, "dead player selection waits for respawn");
    game_respawn(&host, alice);
    check(player->slots[0].id == M249 && player->slots[1].id == LAW, "respawn applies the selected loadout");
    pickups_weapon(player, 0, BOW, weapons[BOW].ammo);
    game_select_loadout(player, AK74, COLT);
    check(player->slots[0].id == BOW, "loadout menu preserves an acquired Rambo bow");
    Input drain[ACTOR_COUNT] = {0};
    for (int i = 0; i < 3; ++i) { network_inputs(server, drain); game_step(&host, drain); }
    network_broadcast(server, &host);
    check(receive_state(a, &left) && receive_state(b, &right), "loadout command sequence drains in simulation order");
    uint64_t next_command=network_received_sequence(server,alice)+1;
    network_send_input(a,&left,(Input){.held=INPUT_GRENADE,.pressed=INPUT_GRENADE},AK74,COLT);
    Datagram lost_input=receive_datagram(server);
    network_send_input(a,&left,(Input){0},AK74,COLT);
    while (network_received_sequence(server,alice)<next_command+1) network_receive(server,&host);
    Input recovered[ACTOR_COUNT]={0};network_inputs(server,recovered);
    check(recovered[alice].pressed&INPUT_GRENADE,"later command recovers a button tap from a dropped UDP datagram");
    send_datagram(a,server,lost_input);
    network_receive(server,&host);network_inputs(server,recovered);
    check(!(recovered[alice].pressed&INPUT_GRENADE),"reordered old command does not repeat the recovered tap");
    free(lost_input.bytes);
    game_step(&host,recovered);
    Snapshot loss_state=replica_encode(&host);
    network_broadcast(server,&host);
    Datagram first_loss=receive_datagram(a);
    uint32_t loss_bytes=(uint32_t)first_loss.bytes[40]<<24|(uint32_t)first_loss.bytes[41]<<16|
        (uint32_t)first_loss.bytes[42]<<8|first_loss.bytes[43];
    size_t loss_fragments=(loss_bytes+NETWORK_PACKET_DATA-1)/NETWORK_PACKET_DATA;
    size_t loss_count=loss_fragments+first_loss.bytes[56]*((loss_fragments+254)/255);
    Datagram *loss=malloc(loss_count*sizeof(*loss));check(loss!=NULL,"allocate loss fixture");
    loss[0]=first_loss;
    for (size_t i=1;i<loss_count;++i) loss[i]=receive_datagram(a);
    uint64_t replica_tick=left.tick;
    for (size_t i=1;i<loss_fragments;++i) send_datagram(server,a,loss[i]);
    check(!network_receive(a,&left) && left.tick==replica_tick,"partial snapshot never changes authoritative state");
    game_step(&host,recovered);
    Snapshot complete_state=replica_encode(&host);
    network_broadcast(server,&host);
    Datagram first_complete=receive_datagram(a);
    uint32_t complete_bytes=(uint32_t)first_complete.bytes[40]<<24|(uint32_t)first_complete.bytes[41]<<16|
        (uint32_t)first_complete.bytes[42]<<8|first_complete.bytes[43];
    size_t complete_fragments=(complete_bytes+NETWORK_PACKET_DATA-1)/NETWORK_PACKET_DATA;
    size_t complete_count=complete_fragments+first_complete.bytes[56]*((complete_fragments+254)/255);
    Datagram *complete=malloc(complete_count*sizeof(*complete));check(complete!=NULL,"allocate reorder fixture");
    complete[0]=first_complete;
    for (size_t i=1;i<complete_count;++i) complete[i]=receive_datagram(a);
    send_datagram(server,a,complete[complete_count-1]);
    for (size_t i=complete_count;i>0;--i) send_datagram(server,a,complete[i-1]);
    check(receive_state(a,&left),"next complete snapshot recovers after loss and reverse-order duplicate fragments");
    Snapshot recovered_state=replica_encode(&left);
    check(complete_state.size==recovered_state.size && !memcmp(complete_state.data,recovered_state.data,complete_state.size),"loss recovery produces the exact replicated state");
    send_datagram(server,a,loss[0]);network_receive(a,&left);
    check(left.tick==host.tick,"late fragment from old snapshot cannot roll back state");
    complete[0].bytes[7]=(unsigned char)(NETWORK_PROTOCOL_VERSION-1);
    send_datagram(server,a,complete[0]);network_receive(a,&left);
    check(network_status(a)==NET_CONNECTED && left.tick==host.tick,"incompatible wire version cannot replace state");
    for (size_t i=0;i<loss_count;++i) free(loss[i].bytes);
    for (size_t i=0;i<complete_count;++i) free(complete[i].bytes);
    free(loss);free(complete);free(loss_state.data);free(complete_state.data);free(recovered_state.data);

    world_load(world_map_index("ctf_Ash"));game_set_mode(&host,MODE_CTF);
    network_broadcast(server,&host);
    check(receive_state(a,&left) && receive_state(b,&right),"clients learn map before sending map-scoped commands");
    network_select_team(a,TEAM_ALPHA);network_select_team(b,TEAM_BRAVO);
    next_command=network_received_sequence(server,alice)+1;
    uint64_t bob_command=network_received_sequence(server,bob)+1;
    network_send_input(a,&left,(Input){0},AK74,COLT);network_send_input(b,&right,(Input){0},MP5,KNIFE);
    while (network_received_sequence(server,alice)<next_command || network_received_sequence(server,bob)<bob_command)
        network_receive(server,&host);
    check(host.actors[alice].team==TEAM_ALPHA && host.actors[bob].team==TEAM_BRAVO,"real clients select opposing CTF teams");
    for (int i=0;i<ACTOR_COUNT;++i) if (i!=alice && i!=bob) host.actors[i].life=INACTIVE;
    host.actors[alice].spawn_protection_ticks=0;
    host.actors[alice].position=sub(host.flags[FLAG_BRAVO-1].position,v3(0,8,0));
    host.actors[alice].previous=host.actors[alice].position;
    Input objective_inputs[ACTOR_COUNT]={0};objectives_step(&host,objective_inputs);
    check(host.actors[alice].carried_flag==FLAG_BRAVO,"client actor acquires enemy flag through objective rules");
    host.actors[alice].position=sub(host.flags[FLAG_ALPHA-1].base,v3(0,8,0));
    objectives_step(&host,objective_inputs);
    check(host.team_score[TEAM_ALPHA]==1 && host.actors[alice].captures==1,"CTF capture updates authoritative team and player score");
    host.event_count=0;
    network_inputs(server,objective_inputs);network_broadcast(server,&host);
    check(receive_state(a,&left),"map transition delivers objective state");
    check(network_map(a)==world_map_current && left.mode==MODE_CTF && left.team_score[TEAM_ALPHA]==1 &&
        left.actors[alice].captures==1 && left.actors[bob].team==TEAM_BRAVO,"map, teams, capture and score replicate together");
    Snapshot objective_authority=replica_encode(&host),objective_replica=replica_encode(&left);
    check(objective_authority.size==objective_replica.size && !memcmp(objective_authority.data,objective_replica.data,objective_authority.size),"full objective snapshot matches authority");
    free(objective_authority.data);free(objective_replica.data);
    network_select_team(a,TEAM_SPECTATOR);
    next_command=network_received_sequence(server,alice)+1;
    network_send_input(a,&left,(Input){0},AK74,COLT);
    while (network_received_sequence(server,alice)<next_command) network_receive(server,&host);
    check(host.actors[alice].life==INACTIVE && host.actors[alice].team==TEAM_SPECTATOR,
        "spectator request leaves simulation while preserving connection");
    network_inputs(server,objective_inputs);network_broadcast(server,&host);
    check(receive_state(a,&left) && left.actors[alice].life==INACTIVE && left.actors[alice].team==TEAM_SPECTATOR,
        "spectator state replicates to the connected client");
    network_select_team(a,TEAM_ALPHA);
    next_command=network_received_sequence(server,alice)+1;
    network_send_input(a,&left,(Input){0},AK74,COLT);
    while (network_received_sequence(server,alice)<next_command) network_receive(server,&host);
    check(host.actors[alice].life==ALIVE && host.actors[alice].team==TEAM_ALPHA &&
        host.actors[alice].spawn_protection_ticks==SRC_DEFAULT_CEASEFIRE_TIME && host.actors[alice].captures==1,
        "spectator rejoins selected team with source spawn protection and preserved score");
    host.actors[alice].spawn_protection_ticks=0;
    host.actors[alice].position=sub(host.flags[FLAG_BRAVO-1].base,v3(0,8,0));
    objectives_step(&host,objective_inputs);
    check(host.actors[alice].carried_flag==FLAG_BRAVO,"carrier can acquire flag before disconnect");
    host.actors[bob].respawn_ticks = 1;
    network_close(a); network_close(b);
    while (network_remote(server, alice) || network_remote(server, bob)) network_receive(server, &host);
    check(host.actors[alice].life == INACTIVE && host.actors[bob].life == INACTIVE && host.actors[bob].respawn_ticks == 0, "disconnect releases actor slots and cancels queued respawns");
    check(host.flags[FLAG_BRAVO-1].state==FLAG_DROPPED && host.flags[FLAG_BRAVO-1].carrier==-1 &&
        host.actors[alice].carried_flag==FLAG_NONE,"carrier disconnect drops flag and clears ownership");
    Network *clients[ACTOR_COUNT];
    uint32_t slots = 1;
    for (int i = 0; i < ACTOR_COUNT - 1; ++i) {
        clients[i] = network_join("127.0.0.1", network_port(server), "Player");
        connect_client(server, clients[i], &host, &left);
        int slot = network_actor(clients[i]);
        check(slot >= 0 && !(slots & (1u << slot)), "32-player capacity assigns each slot once");
        slots |= 1u << slot;
    }
    check(slots == UINT32_MAX, "all original 32 slots supported");
    clients[ACTOR_COUNT - 1] = network_join("127.0.0.1", network_port(server), "Overflow");
    connect_client(server, clients[ACTOR_COUNT - 1], &host, &left);
    check(network_status(clients[ACTOR_COUNT - 1]) == NET_REJECTED, "full server rejects an additional player explicitly");
    for (int i = 0; i < ACTOR_COUNT; ++i) network_close(clients[i]);
    network_receive(server, &host); network_close(server);
    game_free(&host); game_free(&left); game_free(&right);
    blocked_spawn_contract();
    prediction_contract();
    delivery_contract();
    latency_contract(2);
    latency_contract(8);
    latency_contract(16);
    latency_contract(32);
    ragdolls_free(); poses_free(); world_free();
    puts("Network: portable snapshots, atomic rejection, IPv4/IPv6, two-client combat, fragmentation, UDP loss/reordering, reconciliation, taps, loadouts, CTF lifecycle, disconnect and 32-player capacity passed");
    return 0;
}
