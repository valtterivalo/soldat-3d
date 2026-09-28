#define _POSIX_C_SOURCE 200809L
#include "lobby.h"
#include "network.h"
#include "world.h"
#include "generated_rules.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum { MAGIC=0x534c4233, HEADER=16, INFO_SIZE=141, ENDPOINT_SIZE=22, PACKET_SIZE=HEADER+8+ENDPOINT_SIZE+INFO_SIZE };
typedef enum { REGISTER=1, QUERY, INFO, LIST, ROW, UNREGISTER } Message;
typedef enum { ENDPOINT_PENDING, ENDPOINT_VERIFIED } EndpointState;
static const unsigned char query_padding[INFO_SIZE];
typedef struct {
    struct sockaddr_in6 address;
    LobbyEntry entry;
    uint64_t nonce;
    double sent,seen;
} Probe;
typedef struct {
    struct sockaddr_in6 address;
    LobbyEntry entry;
    char name[LOBBY_NAME_LENGTH+1];
    uint64_t challenge,token,pending_token;
    double seen;
    EndpointState state;
} Registration;
struct Lobby {
    int socket;
    struct sockaddr_in6 directory;
    unsigned short directory_port;
    Probe *entries;
    size_t count;
    uint32_t *received;
    size_t received_count;
    uint32_t expected;
    uint64_t request,lan_request;
    double started,retried;
    LobbyStatus status;
};
struct LobbyHost {
    int socket;
    struct sockaddr_in6 directory;
    unsigned short directory_port;
    char name[LOBBY_NAME_LENGTH+1];
    uint64_t token;
    double advertised;
};
struct LobbyDirectory {
    int socket;
    unsigned short port;
    Registration *entries;
    size_t count;
};

static void fail(const char *operation) {perror(operation);exit(EXIT_FAILURE);}
static unsigned read16(const unsigned char *p) {return (unsigned)p[0]<<8|p[1];}
static uint32_t read32(const unsigned char *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t read64(const unsigned char *p) {return (uint64_t)read32(p)<<32|read32(p+4);}
static void write16(unsigned char *p,unsigned value) {p[0]=value>>8;p[1]=value;}
static void write32(unsigned char *p,uint32_t value) {p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;}
static void write64(unsigned char *p,uint64_t value) {write32(p,value>>32);write32(p+4,value);}
static double expiry(void) {return (double)SRC_DISCONNECTION_TIME/TICK_RATE;}
static uint64_t nonce(void) {
    uint64_t value;
    int file=open("/dev/urandom",O_RDONLY);
    if (file<0) fail("open /dev/urandom");
    if (read(file,&value,sizeof(value))!=sizeof(value)) fail("read /dev/urandom");
    if (close(file)!=0) fail("close /dev/urandom");
    return value;
}
static int same_address(const struct sockaddr_in6 *a,const struct sockaddr_in6 *b) {
    return a->sin6_port==b->sin6_port && a->sin6_scope_id==b->sin6_scope_id &&
        !memcmp(&a->sin6_addr,&b->sin6_addr,sizeof(a->sin6_addr));
}
static int socket_open(unsigned short port) {
    int fd=socket(AF_INET6,SOCK_DGRAM,0),dual=0,broadcast=1;
    if (fd<0) fail("lobby socket");
    if (setsockopt(fd,IPPROTO_IPV6,IPV6_V6ONLY,&dual,sizeof(dual))!=0 ||
        setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&broadcast,sizeof(broadcast))!=0) fail("lobby setsockopt");
    struct sockaddr_in6 address={.sin6_family=AF_INET6,.sin6_port=htons(port),.sin6_addr=IN6ADDR_ANY_INIT};
    if (bind(fd,(struct sockaddr *)&address,sizeof(address))!=0) fail("lobby bind");
    int flags=fcntl(fd,F_GETFL);
    if (flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)!=0) fail("lobby fcntl");
    return fd;
}
static struct sockaddr_in6 resolve(const char *host,unsigned short port) {
    struct addrinfo hints={.ai_family=AF_INET6,.ai_socktype=SOCK_DGRAM,.ai_flags=AI_V4MAPPED},*result;
    char service[6];snprintf(service,sizeof(service),"%u",port);
    int error=getaddrinfo(host,service,&hints,&result);
    if (error) {fprintf(stderr,"Resolve lobby %s: %s\n",host,gai_strerror(error));exit(EXIT_FAILURE);}
    struct sockaddr_in6 address;
    memcpy(&address,result->ai_addr,sizeof(address));freeaddrinfo(result);
    return address;
}
static void send_message(int fd,const struct sockaddr_in6 *address,Message kind,uint64_t id,const void *data,size_t size) {
    unsigned char packet[PACKET_SIZE];
    write32(packet,MAGIC);write16(packet+4,LOBBY_VERSION);write16(packet+6,kind);write64(packet+8,id);
    if (size) memcpy(packet+HEADER,data,size);
    ssize_t written=sendto(fd,packet,HEADER+size,0,(const struct sockaddr *)address,sizeof(*address));
    if (written<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==ENOBUFS)) return;
    if (written<0) fail("lobby sendto");
    if ((size_t)written!=HEADER+size) abort();
}
static int header(const unsigned char *packet,size_t size) {
    return size>=HEADER && read32(packet)==MAGIC && read16(packet+4)==LOBBY_VERSION;
}
static ssize_t receive_message(int fd,unsigned char *packet,struct sockaddr_in6 *source) {
    socklen_t size=sizeof(*source);
    ssize_t received;
    do {received=recvfrom(fd,packet,65536,0,(struct sockaddr *)source,&size);} while(received<0 && errno==EINTR);
    if (received<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) return -1;
    if (received<0) fail("lobby recvfrom");
    return received;
}
static void encode_info(unsigned char bytes[INFO_SIZE],const LobbyEntry *entry) {
    memset(bytes,0,INFO_SIZE);
    memcpy(bytes,entry->name,strlen(entry->name));memcpy(bytes+64,entry->map,strlen(entry->map));
    write16(bytes+129,entry->players);write16(bytes+131,entry->bots);write16(bytes+133,entry->max_players);
    write16(bytes+135,entry->mode);write32(bytes+137,entry->version);
}
static int decode_info(LobbyEntry *entry,const unsigned char *bytes,size_t size) {
    if (size!=INFO_SIZE || !memchr(bytes,0,64) || !memchr(bytes+64,0,65)) return 0;
    memcpy(entry->name,bytes,64);memcpy(entry->map,bytes+64,65);
    entry->players=read16(bytes+129);entry->bots=read16(bytes+131);entry->max_players=read16(bytes+133);
    entry->mode=(GameMode)read16(bytes+135);entry->version=read32(bytes+137);
    return entry->mode<=MODE_HTF && entry->max_players==ACTOR_COUNT && entry->players<=ACTOR_COUNT && entry->bots<=ACTOR_COUNT-entry->players;
}
static void set_endpoint(LobbyEntry *entry,const struct sockaddr_in6 *address) {
    if (IN6_IS_ADDR_V4MAPPED(&address->sin6_addr)) {
        if (!inet_ntop(AF_INET,&address->sin6_addr.s6_addr[12],entry->address,sizeof(entry->address))) fail("inet_ntop");
    } else {
        char host[INET6_ADDRSTRLEN];
        if (!inet_ntop(AF_INET6,&address->sin6_addr,host,sizeof(host))) fail("inet_ntop");
        if (address->sin6_scope_id) snprintf(entry->address,sizeof(entry->address),"%s%%%u",host,address->sin6_scope_id);
        else strcpy(entry->address,host);
    }
    entry->port=ntohs(address->sin6_port);
}

LobbyHost *lobby_host_open(int game_socket,const char *directory,unsigned short port,const char *name) {
    if (!*name || strlen(name)>LOBBY_NAME_LENGTH) {fprintf(stderr,"Server names require 1..%d characters\n",LOBBY_NAME_LENGTH);exit(EXIT_FAILURE);}
    LobbyHost *host=calloc(1,sizeof(*host));if (!host) abort();
    host->socket=game_socket;host->token=nonce();strcpy(host->name,name);
    if (directory) {host->directory=resolve(directory,port);host->directory_port=port;}
    return host;
}
void lobby_host_update(LobbyHost *host,double now) {
    if (!host->directory_port || (host->advertised && now<host->advertised+expiry()/3)) return;
    unsigned char registration[INFO_SIZE]={0};
    memcpy(registration,host->name,strlen(host->name));
    send_message(host->socket,&host->directory,REGISTER,host->token,registration,sizeof(registration));
    host->advertised=now;
}
void lobby_host_close(LobbyHost *host) {
    if (host->directory_port) send_message(host->socket,&host->directory,UNREGISTER,host->token,NULL,0);
    free(host);
}
int lobby_server_packet(int game_socket,const void *data,size_t size,const struct sockaddr_in6 *source,
    const Game *game,const char *name,int players,int bots) {
    const unsigned char *packet=data;
    if (!header(packet,size)) return 0;
    if (read16(packet+6)!=QUERY || size!=HEADER+INFO_SIZE) return 1;
    LobbyEntry entry={.players=(unsigned)players,.bots=(unsigned)bots,.max_players=ACTOR_COUNT,.mode=game->mode,.version=NETWORK_PROTOCOL_VERSION};
    if (strlen(name)>LOBBY_NAME_LENGTH || strlen(world_map_names[world_map_current])>=sizeof(entry.map)) abort();
    strcpy(entry.name,name);strcpy(entry.map,world_map_names[world_map_current]);
    unsigned char info[INFO_SIZE];encode_info(info,&entry);
    send_message(game_socket,source,INFO,read64(packet+8),info,sizeof(info));
    return 1;
}

LobbyDirectory *lobby_directory_open(unsigned short port) {
    LobbyDirectory *directory=calloc(1,sizeof(*directory));if (!directory) abort();
    directory->socket=socket_open(port);
    struct sockaddr_in6 address;socklen_t size=sizeof(address);
    if (getsockname(directory->socket,(struct sockaddr *)&address,&size)!=0) fail("lobby getsockname");
    directory->port=ntohs(address.sin6_port);
    return directory;
}
unsigned short lobby_directory_port(const LobbyDirectory *directory) {return directory->port;}
size_t lobby_directory_count(const LobbyDirectory *directory) {
    size_t count=0;
    for (size_t i=0;i<directory->count;++i) count+=directory->entries[i].state==ENDPOINT_VERIFIED;
    return count;
}
void lobby_directory_pump(LobbyDirectory *directory,double now) {
    for (size_t i=0;i<directory->count;)
        if (now>=directory->entries[i].seen+expiry()) directory->entries[i]=directory->entries[--directory->count];
        else ++i;
    for (;;) {
        unsigned char packet[65536];struct sockaddr_in6 source;
        ssize_t received=receive_message(directory->socket,packet,&source);
        if (received<0) break;
        if (!header(packet,(size_t)received)) continue;
        Message kind=(Message)read16(packet+6);uint64_t id=read64(packet+8);
        unsigned char *payload=packet+HEADER;size_t size=(size_t)received-HEADER;
        if (kind==LIST && !size) {
            size_t total=lobby_directory_count(directory);
            if (total>UINT32_MAX) abort();
            unsigned char row[PACKET_SIZE-HEADER];write32(row,0);write32(row+4,(uint32_t)total);
            if (!total) send_message(directory->socket,&source,ROW,id,row,8);
            unsigned index=0;
            for (size_t i=0;i<directory->count;++i) {
                const Registration *entry=&directory->entries[i];
                if (entry->state!=ENDPOINT_VERIFIED) continue;
                write32(row,index++);
                memcpy(row+8,&entry->address.sin6_addr,16);write16(row+24,ntohs(entry->address.sin6_port));
                write32(row+26,entry->address.sin6_scope_id);encode_info(row+30,&entry->entry);
                send_message(directory->socket,&source,ROW,id,row,sizeof(row));
            }
            continue;
        }
        size_t index=0;
        while (index<directory->count && !same_address(&source,&directory->entries[index].address)) ++index;
        if (kind==REGISTER) {
            if (size!=INFO_SIZE || !payload[0] || !memchr(payload,0,LOBBY_NAME_LENGTH+1)) continue;
            if (index==directory->count) {
                directory->entries=realloc(directory->entries,(directory->count+1)*sizeof(*directory->entries));
                if (!directory->entries) abort();
                directory->entries[directory->count++]=(Registration){.address=source,.seen=now};
            }
            Registration *entry=&directory->entries[index];
            entry->challenge=nonce();entry->pending_token=id;strcpy(entry->name,(char *)payload);
            send_message(directory->socket,&source,QUERY,entry->challenge,query_padding,sizeof(query_padding));
        } else if (index<directory->count) {
            Registration *entry=&directory->entries[index];
            if (kind==UNREGISTER && !size && id==(entry->state==ENDPOINT_VERIFIED?entry->token:entry->pending_token)) directory->entries[index]=directory->entries[--directory->count];
            else if (kind==INFO && id==entry->challenge) {
                LobbyEntry info={0};
                if (!decode_info(&info,payload,size) || strcmp(info.name,entry->name)) continue;
                entry->entry=info;entry->seen=now;entry->token=entry->pending_token;entry->state=ENDPOINT_VERIFIED;
            }
        }
    }
}
void lobby_directory_close(LobbyDirectory *directory) {
    if (close(directory->socket)!=0) fail("lobby close");
    free(directory->entries);free(directory);
}

Lobby *lobby_open(const char *directory,unsigned short port) {
    Lobby *lobby=calloc(1,sizeof(*lobby));if (!lobby) abort();
    lobby->socket=socket_open(0);
    if (directory) {lobby->directory=resolve(directory,port);lobby->directory_port=port;}
    return lobby;
}
void lobby_refresh(Lobby *lobby,double now) {
    lobby->count=0;lobby->received_count=0;lobby->expected=UINT32_MAX;
    lobby->request=nonce();lobby->lan_request=nonce();lobby->started=lobby->retried=now;
    lobby->status=lobby->directory_port?LOBBY_LOADING:LOBBY_READY;
    if (lobby->directory_port) send_message(lobby->socket,&lobby->directory,LIST,lobby->request,NULL,0);
    struct sockaddr_in6 lan={.sin6_family=AF_INET6,.sin6_port=htons(LOBBY_GAME_PORT)};
    lan.sin6_addr.s6_addr[10]=lan.sin6_addr.s6_addr[11]=255;
    memset(lan.sin6_addr.s6_addr+12,255,4);
    send_message(lobby->socket,&lan,QUERY,lobby->lan_request,query_padding,sizeof(query_padding));
}
void lobby_pump(Lobby *lobby,double now) {
    if (lobby->status==LOBBY_LOADING && now>=lobby->started+expiry()) lobby->status=LOBBY_UNREACHABLE;
    if (lobby->status==LOBBY_LOADING && now>=lobby->retried+expiry()/3) {
        send_message(lobby->socket,&lobby->directory,LIST,lobby->request,NULL,0);lobby->retried=now;
    }
    for (;;) {
        unsigned char packet[65536];struct sockaddr_in6 source;
        ssize_t received=receive_message(lobby->socket,packet,&source);
        if (received<0) break;
        if (!header(packet,(size_t)received)) continue;
        Message kind=(Message)read16(packet+6);uint64_t id=read64(packet+8);
        unsigned char *payload=packet+HEADER;size_t size=(size_t)received-HEADER;
        if (kind==ROW && id==lobby->request && same_address(&source,&lobby->directory)) {
            if (size<8) continue;
            uint32_t index=read32(payload),total=read32(payload+4);
            if ((!total && size!=8) || (total && (index>=total || size!=8+ENDPOINT_SIZE+INFO_SIZE))) continue;
            if (lobby->expected!=total) {lobby->expected=total;lobby->received_count=0;}
            size_t row=0;while (row<lobby->received_count && lobby->received[row]!=index) ++row;
            if (row<lobby->received_count) continue;
            if (!total) {lobby->status=LOBBY_READY;continue;}
            struct sockaddr_in6 endpoint={.sin6_family=AF_INET6,.sin6_port=htons(read16(payload+24)),.sin6_scope_id=read32(payload+26)};
            memcpy(&endpoint.sin6_addr,payload+8,16);
            LobbyEntry info={.ping_ms=-1};
            if (!decode_info(&info,payload+30,INFO_SIZE)) continue;
            lobby->received=realloc(lobby->received,(lobby->received_count+1)*sizeof(*lobby->received));if (!lobby->received) abort();
            lobby->received[lobby->received_count++]=index;
            size_t existing=0;while (existing<lobby->count && !same_address(&endpoint,&lobby->entries[existing].address)) ++existing;
            if (existing==lobby->count) {
                lobby->entries=realloc(lobby->entries,(lobby->count+1)*sizeof(*lobby->entries));if (!lobby->entries) abort();
                lobby->entries[lobby->count++]=(Probe){.address=endpoint,.entry=info,.seen=now};
            }
            Probe *probe=&lobby->entries[existing];set_endpoint(&probe->entry,&endpoint);
            probe->nonce=nonce();probe->sent=now;
            send_message(lobby->socket,&endpoint,QUERY,probe->nonce,query_padding,sizeof(query_padding));
            if (lobby->received_count==total) lobby->status=LOBBY_READY;
        } else if (kind==INFO) {
            size_t index=0;while (index<lobby->count && !same_address(&source,&lobby->entries[index].address)) ++index;
            if (id!=lobby->lan_request && (index==lobby->count || id!=lobby->entries[index].nonce)) continue;
            LobbyEntry entry={0};if (!decode_info(&entry,payload,size)) continue;
            if (index==lobby->count) {
                lobby->entries=realloc(lobby->entries,(lobby->count+1)*sizeof(*lobby->entries));if (!lobby->entries) abort();
                lobby->entries[lobby->count++]=(Probe){.address=source,.sent=lobby->started};
            }
            Probe *probe=&lobby->entries[index];
            entry.ping_ms=(now-probe->sent)*1000;set_endpoint(&entry,&source);
            probe->entry=entry;probe->seen=now;
        }
    }
    for (size_t i=0;i<lobby->count;) {
        Probe *probe=&lobby->entries[i];
        if (now>=probe->seen+expiry()) {*probe=lobby->entries[--lobby->count];continue;}
        if (now>=probe->sent+expiry()/3) {
            probe->nonce=nonce();probe->sent=now;send_message(lobby->socket,&probe->address,QUERY,probe->nonce,query_padding,sizeof(query_padding));
        }
        ++i;
    }
}
size_t lobby_count(const Lobby *lobby) {return lobby->count;}
const LobbyEntry *lobby_entry(const Lobby *lobby,size_t index) {return &lobby->entries[index].entry;}
int lobby_best(const Lobby *lobby) {
    int best=-1;
    for (size_t i=0;i<lobby->count;++i) {
        const LobbyEntry *entry=&lobby->entries[i].entry;
        if (entry->version!=NETWORK_PROTOCOL_VERSION || entry->players>=entry->max_players || entry->ping_ms<0) continue;
        if (best<0 || entry->ping_ms<lobby->entries[best].entry.ping_ms) best=(int)i;
    }
    return best;
}
LobbyStatus lobby_status(const Lobby *lobby) {return lobby->status;}
void lobby_close(Lobby *lobby) {
    if (close(lobby->socket)!=0) fail("lobby close");
    free(lobby->entries);free(lobby->received);free(lobby);
}
