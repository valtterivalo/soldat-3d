#include "lobby.h"
#include "network.h"
#include "pose.h"
#include "ragdoll.h"
#include "generated_rules.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition,const char *contract) {
    if (condition) return;
    fprintf(stderr,"Lobby contract failed: %s\n",contract);exit(EXIT_FAILURE);
}

int main(int argc,char **argv) {
    check(argc==1 || (argc==2 && !strcmp(argv[1],"--lan")),"optional --lan fixture");
    world_init();poses_init();ragdolls_init();
    Game game;game_init(&game, 42, MODE_DEATHMATCH);
    Network *server=network_host(0,0,&game);
    network_set_name(server,"Original rules");
    {
        Network *probe=network_join("127.0.0.1",network_port(server),"Query audit");
        struct sockaddr_in6 source={.sin6_family=AF_INET6,.sin6_port=htons(network_port(probe))};
        check(inet_pton(AF_INET6,"::ffff:127.0.0.1",&source.sin6_addr)==1,"query probe endpoint");
        unsigned char query[157]={0x53,0x4c,0x42,0x33,0,LOBBY_VERSION,0,2};
        query[15]=7;
        check(lobby_server_packet(network_socket(server),query,16,&source,&game,"Original rules",1,6),"short query is recognized");
        query[15]=8;
        check(lobby_server_packet(network_socket(server),query,sizeof(query),&source,&game,"Original rules",1,6),"padded query is recognized");
        unsigned char response[65536];
        double started=network_time();
        ssize_t received;
        do {
            received=recvfrom(network_socket(probe),response,sizeof(response),0,NULL,NULL);
            check(received>=0 || errno==EAGAIN || errno==EWOULDBLOCK,"receive query response");
            check(network_time()-started<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"padded query receives response before disconnect interval");
        } while(received<0);
        check(received==(ssize_t)sizeof(query) && response[7]==3 && response[15]==8,
            "unpadded query receives nothing and valid query response cannot amplify bytes");
        LobbyHost *registration=lobby_host_open(network_socket(server),"127.0.0.1",network_port(probe),"Original rules");
        lobby_host_update(registration,network_time());
        do {
            received=recvfrom(network_socket(probe),response,sizeof(response),0,NULL,NULL);
            check(received>=0 || errno==EAGAIN || errno==EWOULDBLOCK,"receive padded registration");
            check(network_time()-started<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"registration reaches directory endpoint");
        } while(received<0);
        check(received==(ssize_t)sizeof(query) && response[7]==1 && !strcmp((char *)response+16,"Original rules"),
            "registration preserves the name and is as large as its ownership challenge");
        lobby_host_close(registration);
        network_close(probe);
    }
    LobbyDirectory *directory=lobby_directory_open(0);
    unsigned short directory_port=lobby_directory_port(directory);
    LobbyHost *host=lobby_host_open(network_socket(server),"127.0.0.1",directory_port,"Original rules");
    double now=network_time();
    lobby_host_update(host,now);
    lobby_directory_pump(directory,now);
    check(lobby_directory_count(directory)==0,"registration is hidden before endpoint proves ownership");
    while (!lobby_directory_count(directory)) {
        network_receive(server,&game);lobby_directory_pump(directory,now);
        check(network_time()-now<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"game socket answers directory challenge");
    }
    Lobby *browser=lobby_open("127.0.0.1",directory_port);
    lobby_refresh(browser,now);
    while (lobby_status(browser)!=LOBBY_READY || lobby_best(browser)<0) {
        double time=network_time();
        lobby_directory_pump(directory,time);lobby_pump(browser,time);network_receive(server,&game);lobby_pump(browser,network_time());
        check(time-now<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"directory list and RTT probe complete");
    }
    check(lobby_count(browser)==1,"verified host appears once");
    const LobbyEntry *entry=lobby_entry(browser,(size_t)lobby_best(browser));
    check(entry->port==network_port(server),"observed source endpoint preserves actual ephemeral game port");
    check(!strcmp(entry->name,"Original rules") && !strcmp(entry->map,"Arena2"),"server metadata comes from game socket");
    check(entry->players==1 && entry->bots==6 && entry->max_players==ACTOR_COUNT,"human players and replaceable bots remain distinct");
    check(entry->ping_ms>=0 && entry->version==NETWORK_PROTOCOL_VERSION,"quick join chooses a measured compatible endpoint");
    printf("Lobby endpoint %s:%u, humans=%u bots=%u, RTT=%.3fms\n",entry->address,entry->port,entry->players,entry->bots,entry->ping_ms);
    Network *client=network_join(entry->address,entry->port,"Quick join");
    Game replica;game_init(&replica, 43, MODE_DEATHMATCH);
    while (network_status(client)==NET_CONNECTING) {
        network_receive(client,&replica);network_receive(server,&game);network_receive(client,&replica);
    }
    check(network_status(client)==NET_CONNECTED,"discovered endpoint accepts a real game connection");
    network_close(client);network_receive(server,&game);game_free(&replica);
    lobby_host_close(host);
    while (lobby_directory_count(directory)) {
        lobby_directory_pump(directory,now);
        check(network_time()-now<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"graceful shutdown removes registration");
    }
    lobby_refresh(browser,now);
    while (lobby_status(browser)!=LOBBY_READY) {lobby_directory_pump(directory,now);lobby_pump(browser,now);}
    check(lobby_count(browser)==0 && lobby_best(browser)==-1,"empty directory completes and cannot quick join");
    host=lobby_host_open(network_socket(server),"127.0.0.1",directory_port,"Original rules");
    lobby_host_update(host,now);
    while (!lobby_directory_count(directory)) {lobby_directory_pump(directory,now);network_receive(server,&game);lobby_directory_pump(directory,now);}
    double heartbeat=now+(double)SRC_DISCONNECTION_TIME/TICK_RATE/3;
    game.mode=MODE_RAMBO;
    lobby_host_update(host,heartbeat);
    for (;;) {
        lobby_directory_pump(directory,heartbeat);
        unsigned char packet[65536];struct sockaddr_in6 source;socklen_t size=sizeof(source);
        ssize_t received=recvfrom(network_socket(server),packet,sizeof(packet),0,(struct sockaddr *)&source,&size);
        if (received<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) continue;
        check(received>=0,"heartbeat query reaches game socket");
        check(lobby_server_packet(network_socket(server),packet,(size_t)received,&source,&game,"Original rules",1,6),"game server answers heartbeat ownership challenge");
        break;
    }
    for (;;) {
        lobby_refresh(browser,heartbeat);
        while (lobby_status(browser)!=LOBBY_READY) {lobby_directory_pump(directory,heartbeat);lobby_pump(browser,heartbeat);}
        if (lobby_count(browser)==1 && lobby_entry(browser,0)->mode==MODE_RAMBO) break;
        check(network_time()-now<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"verified heartbeat updates directory metadata");
    }
    lobby_directory_pump(directory,now+(double)SRC_DISCONNECTION_TIME/TICK_RATE);
    check(lobby_directory_count(directory)==1,"verified heartbeat extends registration lifetime");
    lobby_directory_pump(directory,heartbeat+(double)SRC_DISCONNECTION_TIME/TICK_RATE);
    check(lobby_directory_count(directory)==0,"unresponsive server expires at source disconnect interval");
    lobby_host_close(host);lobby_close(browser);lobby_directory_close(directory);network_close(server);
    if (argc==2) {
        Network *lan_server=network_host(LOBBY_GAME_PORT,0,&game);
        network_set_name(lan_server,"LAN discovery fixture");
        Lobby *lan=lobby_open(NULL,LOBBY_PORT);
        double started=network_time();lobby_refresh(lan,started);
        while (lobby_best(lan)<0) {
            network_receive(lan_server,&game);lobby_pump(lan,network_time());
            check(network_time()-started<(double)SRC_DISCONNECTION_TIME/TICK_RATE,"LAN broadcast discovers a listening game server");
        }
        const LobbyEntry *found=lobby_entry(lan,(size_t)lobby_best(lan));
        check(found->port==LOBBY_GAME_PORT && !strcmp(found->name,"LAN discovery fixture"),"LAN discovery returns verified default-port server metadata");
        printf("LAN broadcast discovered %s:%u in %.3fms\n",found->address,found->port,found->ping_ms);
        lobby_close(lan);network_close(lan_server);
    }
    game_free(&game);ragdolls_free();poses_free();world_free();
    puts("Lobby: challenge, observed endpoint, metadata, RTT, real join, unregister, heartbeat and expiry passed");
    return 0;
}
