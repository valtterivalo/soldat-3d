#ifndef SOLDAT3D_LOBBY_H
#define SOLDAT3D_LOBBY_H

#include "game.h"
#include <netinet/in.h>

enum { LOBBY_PORT=23074, LOBBY_GAME_PORT=23073, LOBBY_NAME_LENGTH=63, LOBBY_VERSION=2 };
typedef enum { LOBBY_IDLE, LOBBY_LOADING, LOBBY_READY, LOBBY_UNREACHABLE } LobbyStatus;
typedef struct {
    char address[64],name[LOBBY_NAME_LENGTH+1],map[65];
    unsigned short port;
    unsigned players,bots,max_players,version;
    GameMode mode;
    double ping_ms;
} LobbyEntry;
typedef struct Lobby Lobby;
typedef struct LobbyHost LobbyHost;
typedef struct LobbyDirectory LobbyDirectory;

LobbyHost *lobby_host_open(int game_socket,const char *directory,unsigned short port,const char *name);
void lobby_host_update(LobbyHost *host,double now);
void lobby_host_close(LobbyHost *host);
int lobby_server_packet(int game_socket,const void *packet,size_t size,const struct sockaddr_in6 *source,
    const Game *game,const char *name,int players,int bots);
Lobby *lobby_open(const char *directory,unsigned short port);
void lobby_refresh(Lobby *lobby,double now);
void lobby_pump(Lobby *lobby,double now);
size_t lobby_count(const Lobby *lobby);
const LobbyEntry *lobby_entry(const Lobby *lobby,size_t index);
int lobby_best(const Lobby *lobby);
LobbyStatus lobby_status(const Lobby *lobby);
void lobby_close(Lobby *lobby);
LobbyDirectory *lobby_directory_open(unsigned short port);
unsigned short lobby_directory_port(const LobbyDirectory *directory);
void lobby_directory_pump(LobbyDirectory *directory,double now);
size_t lobby_directory_count(const LobbyDirectory *directory);
void lobby_directory_close(LobbyDirectory *directory);

#endif
