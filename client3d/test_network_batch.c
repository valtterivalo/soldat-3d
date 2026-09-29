#define _GNU_SOURCE
#include <sys/socket.h>
static ssize_t captured_sendto(int fd,const void *data,size_t size,int flags,const struct sockaddr *address,socklen_t length);
static int captured_sendmmsg(int fd,struct mmsghdr *messages,unsigned count,int flags);
#define sendto captured_sendto
#define sendmmsg captured_sendmmsg
#include "network.c"
#undef sendto
#undef sendmmsg
#undef NDEBUG
#include <assert.h>
#include <inttypes.h>

typedef struct { unsigned char *data; size_t size, capacity; } Capture;
typedef enum { FULL, PARTIAL, PRESSURE } Policy;
static Capture captured[2];
static int output;
static Policy policy;
static size_t position, calls;

static void record(const struct sockaddr *address,socklen_t length,const void *data,size_t size) {
    Capture *c=&captured[output];
    size_t needed=c->size+sizeof(size)+sizeof(length)+length+size;
    if(needed>c->capacity){c->capacity=needed*2;c->data=realloc(c->data,c->capacity);assert(c->data);}
    memcpy(c->data+c->size,&size,sizeof(size));c->size+=sizeof(size);
    memcpy(c->data+c->size,&length,sizeof(length));c->size+=sizeof(length);
    memcpy(c->data+c->size,address,length);c->size+=length;
    memcpy(c->data+c->size,data,size);c->size+=size;
}

static int pressure(size_t index) {return policy==PRESSURE && (index==0 || index==17 || index==511 || index==1024 || index==2048);}

static ssize_t captured_sendto(int fd,const void *data,size_t size,int flags,const struct sockaddr *address,socklen_t length) {
    (void)fd;(void)flags;++calls;
    if(pressure(position++)){errno=position%2?EAGAIN:ENOBUFS;return -1;}
    record(address,length,data,size);return (ssize_t)size;
}

static int captured_sendmmsg(int fd,struct mmsghdr *messages,unsigned count,int flags) {
    (void)fd;(void)flags;++calls;
    unsigned sent=0;
    while(sent<count){
        if(pressure(position)){
            if(sent)break;
            ++position;errno=position%2?EAGAIN:ENOBUFS;return -1;
        }
        if(policy==PARTIAL && sent==3)break;
        struct msghdr *message=&messages[sent].msg_hdr;
        unsigned char packet[DATAGRAM];size_t size=0;
        assert(message->msg_iovlen==2);
        for(size_t i=0;i<message->msg_iovlen;++i){
            assert(size+message->msg_iov[i].iov_len<=sizeof(packet));
            memcpy(packet+size,message->msg_iov[i].iov_base,message->msg_iov[i].iov_len);
            size+=message->msg_iov[i].iov_len;
        }
        record(message->msg_name,message->msg_namelen,packet,size);
        messages[sent].msg_len=(unsigned)size;++sent;++position;
    }
    return (int)sent;
}

int main(void) {
    enum { COUNT=UIO_MAXIOV*2+17 };
    unsigned char (*payload)[DATAGRAM-HEADER]=malloc(COUNT*sizeof(*payload));assert(payload);
    struct sockaddr_in6 addresses[ACTOR_COUNT]={0};
    for(unsigned i=0;i<ACTOR_COUNT;++i){addresses[i].sin6_family=AF_INET6;addresses[i].sin6_port=(unsigned short)(3000+i);}
    for(unsigned i=0;i<COUNT;++i)for(unsigned j=0;j<DATAGRAM-HEADER;++j)payload[i][j]=(unsigned char)(i*31+j*17);
    for(policy=FULL;policy<=PRESSURE;++policy){
        NetworkStats stats[2]={0};size_t syscalls[2]={0};
        for(output=0;output<2;++output){
            Network net={.next_event_id=9876};PacketBatch batch;batch.count=0;
            position=calls=0;captured[output].size=0;
            for(unsigned i=0;i<COUNT;++i){
                PacketKind kind=(PacketKind[]){STATE,REPAIR,EVENTS}[i%3];
                size_t size=i%5==0?(kind==EVENTS?DATAGRAM-HEADER:STATE_DATA):i%251;
                send_packet(&net,output?&batch:NULL,&addresses[i%ACTOR_COUNT],kind,i+100,i+1,i/2,3,COUNT,i,i+1,i%3,payload[i],size);
            }
            if(output)flush_packets(&net,&batch);
            stats[output]=net.stats;syscalls[output]=calls;assert(position==COUNT);
        }
        assert(captured[0].size==captured[1].size);
        assert(!memcmp(captured[0].data,captured[1].data,captured[0].size));
        assert(stats[0].sent_bytes==stats[1].sent_bytes && stats[0].sent_packets==stats[1].sent_packets);
        printf("policy=%d packets=%"PRIu64" bytes=%"PRIu64" sendto=%zu sendmmsg=%zu exact=yes\n",policy,stats[1].sent_packets,stats[1].sent_bytes,syscalls[0],syscalls[1]);
    }
    free(payload);free(captured[0].data);free(captured[1].data);
}
