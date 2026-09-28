#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1
#endif
#include "game.h"
#include "network.h"
#include "world.h"
#include "pose.h"
#include "ragdoll.h"

#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct { Input inputs[ACTOR_COUNT]; WeaponId primary[ACTOR_COUNT], secondary[ACTOR_COUNT]; } Commands;
typedef enum { PIPE_SEND, PIPE_RECEIVE } PipeTransfer;

static void check(int condition,const char *message) {
    if(condition)return;
    fprintf(stderr,"Network benchmark failed: %s\n",message);exit(EXIT_FAILURE);
}

static void transfer(int fd,void *bytes,size_t size,PipeTransfer direction) {
    size_t done=0;
    while(done<size) {
        ssize_t count=direction==PIPE_SEND ? write(fd,(char *)bytes+done,size-done) : read(fd,(char *)bytes+done,size-done);
        if(count<0 && errno==EINTR)continue;
        check(count>0,"client synchronization pipe");done+=(size_t)count;
    }
}

static double cpu(void) {
    struct timespec time;
    check(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&time)==0,"process CPU clock");
    return time.tv_sec+time.tv_nsec*1e-9;
}

static int compare(const void *a,const void *b) {
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}
int main(int argc,char **argv) {
    int population=ACTOR_COUNT;
    uint64_t warmup=600,measured=1800;
    const char *map="Arena2";
    for(int arg=1;arg<argc;++arg) {
        if(!strcmp(argv[arg],"--help")) {
            puts("bench_network [--players 1..32] [--ticks 1800] [--warmup 600] [--map Arena2]\nReal UDP loopback clients. Server CPU excludes automated input generation and client processing. Memory is server peak RSS. UDP bytes exclude IP and UDP headers.");
            return EXIT_SUCCESS;
        }
        check(arg+1<argc,"each option requires a value");
        const char *option=argv[arg],*value=argv[++arg];
        if(!strcmp(option,"--map")){map=value;continue;}
        char *end;errno=0;unsigned long long number=strtoull(value,&end,10);
        check(*value && *value!='-' && !*end && !errno,"numeric option is an unsigned integer");
        if(!strcmp(option,"--players")){check(number>0 && number<=ACTOR_COUNT,"players fit protocol slots");population=(int)number;}
        else if(!strcmp(option,"--ticks")){check(number>0,"measured ticks are positive");measured=number;}
        else if(!strcmp(option,"--warmup"))warmup=number;
        else check(0,"unknown option");
    }
    check(warmup<UINT64_MAX-measured && measured<=SIZE_MAX/sizeof(double),"requested sample fits tick and allocation sizes");
    uint64_t ticks=warmup+measured;
    size_t map_index=world_map_index(map);check(map_index!=SIZE_MAX,"known map name");
    poses_init();ragdolls_init();world_load(map_index);
    Game host;game_init(&host,0x501da7,MODE_DEATHMATCH);
    for(int i=0;i<ACTOR_COUNT;++i)host.actors[i].life=INACTIVE;
    Network *server=network_host(0,-1,&host);
    int commands[2],replies[2];check(pipe(commands)==0 && pipe(replies)==0,"create client synchronization pipes");
    pid_t child=fork();check(child>=0,"start separate client process");
    if(!child) {
        check(close(commands[1])==0 && close(replies[0])==0 && close(network_socket(server))==0,"close inherited host descriptors");
        Game *replicas=calloc((size_t)population,sizeof(*replicas));check(replicas!=NULL,"allocate client states");
        Network *peers[ACTOR_COUNT];
        for(int i=0;i<population;++i) {
            game_init(&replicas[i],(uint32_t)i+1,MODE_DEATHMATCH);
            peers[i]=network_join("127.0.0.1",network_port(server),"Hosting benchmark");
            do {network_receive(peers[i],&replicas[i]);} while(network_status(peers[i])==NET_CONNECTING);
            check(network_status(peers[i])==NET_CONNECTED,"client joins server");
        }
        uint64_t packets=0;transfer(replies[1],&packets,sizeof(packets),PIPE_SEND);
        for(uint64_t tick=0;;++tick) {
            struct pollfd command={.fd=commands[0],.events=POLLIN};
            for(;;) {
                int ready=poll(&command,1,(1000+TICK_RATE-1)/TICK_RATE);
                if(ready<0 && errno==EINTR)continue;
                check(ready>=0,"poll host synchronization pipe");
                if(ready){check(command.revents&POLLIN,"host synchronization pipe remains open");break;}
                for(int i=0;i<population;++i) {
                    network_receive(peers[i],&replicas[i]);
                    check(network_status(peers[i])==NET_CONNECTED,"client remains connected while waiting for host");
                }
            }
            uint64_t snapshot;transfer(commands[0],&snapshot,sizeof(snapshot),PIPE_RECEIVE);
            for(int i=0;i<population;++i)while(network_stats(peers[i]).snapshots<snapshot) {
                network_receive(peers[i],&replicas[i]);
                check(network_status(peers[i])==NET_CONNECTED,"client receives complete snapshot");
            }
            if(tick==ticks)break;
            Commands actions;transfer(commands[0],&actions,sizeof(actions),PIPE_RECEIVE);
            packets=0;
            for(int i=0;i<population;++i) {
                int actor=network_actor(peers[i]);
                network_send_input(peers[i],&replicas[i],actions.inputs[actor],actions.primary[actor],actions.secondary[actor]);
                packets+=network_stats(peers[i]).sent_packets;
            }
            transfer(replies[1],&packets,sizeof(packets),PIPE_SEND);
        }
        for(int i=0;i<population;++i){network_close(peers[i]);game_free(&replicas[i]);}
        free(replicas);_exit(EXIT_SUCCESS);
    }
    check(close(commands[0])==0 && close(replies[1])==0,"close unused pipe descriptors");
    int connected;
    do {
        check(waitpid(child,NULL,WNOHANG)==0,"client process remains alive during join");
        network_receive(server,&host);connected=0;
        for(int i=0;i<ACTOR_COUNT;++i)connected+=network_remote(server,i);
    } while(connected<population);
    uint64_t ready;transfer(replies[0],&ready,sizeof(ready),PIPE_RECEIVE);
    host.score_limit=host.time_limit_ticks=0;
    network_broadcast(server,&host);
    uint64_t snapshot=1;transfer(commands[1],&snapshot,sizeof(snapshot),PIPE_SEND);
    double *samples=malloc((size_t)measured*sizeof(*samples));check(samples!=NULL,"allocate tick measurements");
    double total=0,receive_step=0,broadcast=0,input_generation=0;
    NetworkStats start={0};uint64_t shots=0,kills=0;
    size_t max_projectiles=0;int max_dead=0;
    double next=network_time(),begin_wall=0;
    for(uint64_t tick=0;tick<ticks;++tick) {
        if(tick==warmup){start=network_stats(server);begin_wall=network_time();}
        Commands actions={0};double bot_begin=cpu();
        for(int i=0;i<ACTOR_COUNT;++i) {
            actions.inputs[i]=bot_input(&host,i);
            actions.inputs[i].pressed=actions.inputs[i].held&~host.actors[i].controls;
            actions.primary[i]=host.actors[i].loadout[0];actions.secondary[i]=host.actors[i].loadout[1];
        }
        double bot_seconds=cpu()-bot_begin;
        transfer(commands[1],&actions,sizeof(actions),PIPE_SEND);
        uint64_t incoming;transfer(replies[0],&incoming,sizeof(incoming),PIPE_RECEIVE);
        double began=cpu();
        do {
            check(waitpid(child,NULL,WNOHANG)==0,"client process remains alive during input delivery");
            network_receive(server,&host);
        } while(network_stats(server).received_packets<incoming);
        Input inputs[ACTOR_COUNT]={0};network_inputs(server,inputs);game_step(&host,inputs);
        double stepped=cpu();network_broadcast(server,&host);double done=cpu();
        if(tick>=warmup) {
            samples[tick-warmup]=done-began;total+=done-began;
            receive_step+=stepped-began;broadcast+=done-stepped;input_generation+=bot_seconds;
            if(host.projectile_count>max_projectiles)max_projectiles=host.projectile_count;
            int dead=0;for(int i=0;i<ACTOR_COUNT;++i)dead+=host.actors[i].life==DEAD;
            if(dead>max_dead)max_dead=dead;
            for(size_t i=0;i<host.event_count;++i){shots+=host.events[i].kind==EVENT_SHOT;kills+=host.events[i].kind==EVENT_KILL;}
        }
        ++snapshot;transfer(commands[1],&snapshot,sizeof(snapshot),PIPE_SEND);
        next+=1.0/TICK_RATE;double delay=next-network_time();
        if(delay>0) {
            struct timespec pause={.tv_sec=(time_t)delay,.tv_nsec=(long)((delay-(time_t)delay)*1e9)};
            while(nanosleep(&pause,&pause)!=0)check(errno==EINTR,"tick pacing sleep");
        }
    }
    double wall=network_time()-begin_wall;NetworkStats end=network_stats(server);
    struct rusage usage;check(getrusage(RUSAGE_SELF,&usage)==0,"read server peak RSS");
#ifdef __APPLE__
    double rss_mib=usage.ru_maxrss/1048576.0;
#else
    double rss_mib=usage.ru_maxrss/1024.0;
#endif
    qsort(samples,(size_t)measured,sizeof(*samples),compare);
    double seconds=(double)measured/TICK_RATE;
    uint64_t bytes=end.sent_bytes-start.sent_bytes,packets=end.sent_packets-start.sent_packets;
    printf("{\"protocol\":%d,\"map\":\"%s\",\"clients\":%d,\"server_bots\":0,\"ticks\":%" PRIu64 ",\"warmup_ticks\":%" PRIu64 ",\"wall_seconds\":%.3f,\"host_ms_mean\":%.4f,\"host_ms_p99\":%.4f,\"host_ms_max\":%.4f,\"one_core_percent\":%.3f,\"receive_step_ms\":%.4f,\"pack_send_ms\":%.4f,\"input_generation_ms\":%.4f,\"server_peak_rss_mib\":%.3f,\"udp_egress_mbps\":%.3f,\"udp_ingress_mbps\":%.3f,\"ipv4_egress_mbps\":%.3f,\"egress_kib_per_client_s\":%.3f,\"datagrams_per_client_tick\":%.3f,\"udp_sent_bytes\":%" PRIu64 ",\"udp_sent_packets\":%" PRIu64 ",\"shots\":%" PRIu64 ",\"kills\":%" PRIu64 ",\"peak_projectiles\":%zu,\"peak_dead\":%d}\n",
        NETWORK_PROTOCOL_VERSION,map,population,measured,warmup,wall,total*1000/measured,samples[measured-measured/100-1]*1000,samples[measured-1]*1000,total/seconds*100,
        receive_step*1000/measured,broadcast*1000/measured,input_generation*1000/measured,rss_mib,
        bytes*8/seconds/1e6,(end.received_bytes-start.received_bytes)*8/seconds/1e6,((double)bytes+28*(double)packets)*8/seconds/1e6,
        bytes/seconds/population/1024,(double)packets/measured/population,bytes,packets,shots,kills,max_projectiles,max_dead);
    fflush(stdout);free(samples);
    int status;check(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status),"client process completes benchmark");
    check(close(commands[1])==0 && close(replies[0])==0,"close synchronization pipes");
    network_close(server);game_free(&host);world_free();ragdolls_free();poses_free();return EXIT_SUCCESS;
}
