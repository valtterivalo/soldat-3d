#define _POSIX_C_SOURCE 200809L
#include "lobby.h"
#include "network.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t running=1;
static void stop(int signal_number) {(void)signal_number;running=0;}

int main(int argc,char **argv) {
    unsigned short port=LOBBY_PORT;
    for (int i=1;i<argc;++i) {
        if (!strcmp(argv[i],"--port") && i+1<argc) {
            char *end;errno=0;const char *argument=argv[++i];
            unsigned long value=strtoul(argument,&end,10);
            if (errno || end==argument || *end || value>65535 || !value || argument[0]=='-') {
                fprintf(stderr,"Invalid directory port %s\n",argument);return 2;
            }
            port=(unsigned short)value;
        } else {fprintf(stderr,"Usage: %s [--port %d]\n",argv[0],LOBBY_PORT);return 2;}
    }
    LobbyDirectory *directory=lobby_directory_open(port);
    signal(SIGINT,stop);signal(SIGTERM,stop);
    printf("Soldat 3D directory UDP %u\n",lobby_directory_port(directory));fflush(stdout);
    size_t previous=0;
    while (running) {
        lobby_directory_pump(directory,network_time());
        size_t count=lobby_directory_count(directory);
        if (count!=previous) {printf("Verified servers: %zu\n",count);fflush(stdout);previous=count;}
        struct timespec remaining={.tv_nsec=1000000000/TICK_RATE};
        while (nanosleep(&remaining,&remaining)!=0 && running)
            if (errno!=EINTR) {perror("nanosleep");return 1;}
    }
    lobby_directory_close(directory);
    return 0;
}
