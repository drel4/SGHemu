#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include "phone_net.h"
#include "wap_gateway.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define NET_QUEUE 32
#define NET_PACKET 2048
#define NET_CALLBACK 0xffffffffu
/* DevRecvPDU copies a 30-byte WDPAddress container (0120F41A/0120F424).
   IPv4 uses only its first eight bytes. Keep unused bytes zero rather than
   echoing unrelated caller memory or treating padding as a new endpoint. */
#define NET_PEER_BYTES 30u
typedef struct {uint8_t bytes[NET_PACKET],peer[NET_PEER_BYTES];unsigned size,channel,notified;uint32_t cookie;} Datagram;
struct PhoneNet {
    CRITICAL_SECTION lock;HANDLE stop,work,thread;SOCKET sockets[2];
    volatile LONG rx_pending; /* Published under lock; fast idle check on x86/MSVC. */
    Datagram tx[NET_QUEUE],rx[NET_QUEUE],current,gateway_peer[2];unsigned tx_head,tx_count,rx_head,rx_count,mode;
    WapGateway gateway[2];WapHttpContext http;
};
static int emit(void *user,const uint8_t *bytes,size_t length)
{
    PhoneNet *n=user;Datagram *packet;if(length>NET_PACKET)return 0;EnterCriticalSection(&n->lock);
    if(n->rx_count==NET_QUEUE){LeaveCriticalSection(&n->lock);return 0;}
    packet=&n->rx[(n->rx_head+n->rx_count)%NET_QUEUE];*packet=n->current;memcpy(packet->bytes,bytes,length);packet->size=(unsigned)length;packet->notified=0;++n->rx_count;
    InterlockedExchange(&n->rx_pending,!n->rx[n->rx_head].notified);
    LeaveCriticalSection(&n->lock);return 1;
}
static unsigned __stdcall worker(void *user)
{
    PhoneNet *n=user;HANDLE waits[2]={n->stop,n->work};unsigned i;
    while(WaitForMultipleObjects(2,waits,FALSE,25)!=WAIT_OBJECT_0){
        for(;;){Datagram packet={0};int have=0;EnterCriticalSection(&n->lock);
            if(n->tx_count){packet=n->tx[n->tx_head];n->tx_head=(n->tx_head+1)%NET_QUEUE;--n->tx_count;have=1;}LeaveCriticalSection(&n->lock);if(!have)break;
            n->current=packet;
            /* Keep the native connection/port cookie, not a host socket ID. */
            if(n->mode!=2||packet.peer[2]!=0x23||packet.peer[3]!=0xf1)n->gateway_peer[packet.channel]=packet;
            if(n->mode==2&&packet.peer[2]==0x23&&packet.peer[3]==0xf1){
                if(memcmp(n->gateway_peer[packet.channel].peer,packet.peer,8)){wap_gateway_close(&n->gateway[packet.channel]);wap_gateway_init(&n->gateway[packet.channel]);}
                n->gateway_peer[packet.channel]=packet;
                (void)wap_gateway_receive(&n->gateway[packet.channel],packet.bytes,packet.size,GetTickCount64(),wap_http_fetch,&n->http,emit,n);
            }else{struct sockaddr_in addr={0};addr.sin_family=AF_INET;memcpy(&addr.sin_port,packet.peer+2,2);memcpy(&addr.sin_addr.s_addr,packet.peer+4,4);
                (void)sendto(n->sockets[packet.channel],(const char *)packet.bytes,(int)packet.size,0,(struct sockaddr *)&addr,sizeof(addr));}
        }
        for(i=0;i<2;++i){struct sockaddr_in addr;int size=sizeof(addr),got;uint8_t bytes[NET_PACKET];
            while((got=recvfrom(n->sockets[i],(char *)bytes,sizeof(bytes),0,(struct sockaddr *)&addr,&size))>0){
                memset(&n->current,0,sizeof(n->current));n->current.channel=i;n->current.cookie=n->gateway_peer[i].cookie;
                n->current.peer[0]=n->gateway_peer[i].peer[0]?n->gateway_peer[i].peer[0]:4;
                memcpy(n->current.peer+2,&addr.sin_port,2);memcpy(n->current.peer+4,&addr.sin_addr.s_addr,4);(void)emit(n,bytes,(size_t)got);size=sizeof(addr);}
            if(n->mode==2){/* Retry with the original remote endpoint, not whichever socket was last read. */
                WapGateway *g=&n->gateway[i];n->current=n->gateway_peer[i];
                (void)wap_gateway_tick(g,GetTickCount64(),emit,n);
            }
        }
    }
    return 0;
}
static int open_network(PhoneNetwork *network)
{
    PhoneNet *n;WSADATA data;unsigned i;
    if(network->runtime)return 1;if(WSAStartup(MAKEWORD(2,2),&data))return 0;
    n=calloc(1,sizeof(*n));if(!n){WSACleanup();return 0;}n->mode=network->mode;
    for(i=0;i<2;++i)n->sockets[i]=INVALID_SOCKET;
    if(!InitializeCriticalSectionAndSpinCount(&n->lock,4000)){free(n);WSACleanup();return 0;}
    n->stop=CreateEventA(NULL,TRUE,FALSE,NULL);n->work=CreateEventA(NULL,FALSE,FALSE,NULL);
    for(i=0;i<2;++i){u_long nonblocking=1;struct sockaddr_in local={0};local.sin_family=AF_INET;wap_gateway_init(&n->gateway[i]);
        n->sockets[i]=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
        if(n->sockets[i]==INVALID_SOCKET||ioctlsocket(n->sockets[i],FIONBIO,&nonblocking)||bind(n->sockets[i],(struct sockaddr *)&local,sizeof(local)))goto failed;
    }
    if(!n->stop||!n->work)goto failed;n->thread=(HANDLE)_beginthreadex(NULL,0,worker,n,0,NULL);if(!n->thread)goto failed;
    network->runtime=n;return 1;
failed:
    for(i=0;i<2;++i)if(n->sockets[i]!=INVALID_SOCKET)closesocket(n->sockets[i]);
    if(n->stop)CloseHandle(n->stop);if(n->work)CloseHandle(n->work);DeleteCriticalSection(&n->lock);free(n);WSACleanup();return 0;
}
void phone_net_close(PhoneNetwork *network)
{
    PhoneNet *n=network->runtime;unsigned i;if(!n)return;SetEvent(n->stop);wap_http_cancel(&n->http);
    WaitForSingleObject(n->thread,INFINITE);CloseHandle(n->thread);
    for(i=0;i<2;++i){closesocket(n->sockets[i]);wap_gateway_close(&n->gateway[i]);}
    CloseHandle(n->stop);CloseHandle(n->work);DeleteCriticalSection(&n->lock);free(n);network->runtime=NULL;
    network->opened[0]=network->opened[1]=0;WSACleanup();
}
void phone_net_set_mode(PhoneNetwork *n,unsigned mode){if(n->mode!=mode)phone_net_close(n);n->mode=mode<=2?mode:0;}
static int callback(Cellular *cell,uint32_t address,uint32_t a,uint32_t b)
{
    uint8_t args[8];unsigned i;for(i=0;i<4;++i){args[i]=(uint8_t)(a>>(i*8));args[i+4]=(uint8_t)(b>>(i*8));}
    return cellular_queue(cell,NET_CALLBACK,address,args,sizeof(args));
}
static void cpu_queue_lock(PhoneNetwork *n)
{
    LARGE_INTEGER before={0},after={0};int track=n->profiling;
    if(track)QueryPerformanceCounter(&before);
    EnterCriticalSection(&n->runtime->lock);
    if(track){QueryPerformanceCounter(&after);++n->lock_calls;
        n->lock_wait_ticks+=(uint64_t)(after.QuadPart-before.QuadPart);}
}
void phone_net_advance(PhoneNetwork *n,Cellular *cell)
{
    PhoneNet *r=n->runtime;Datagram *p;if(!r||!cell->enabled||!r->rx_pending)return;cpu_queue_lock(n);
    if(r->rx_count){p=&r->rx[r->rx_head];if(!p->notified&&n->opened[p->channel]&&cell->count<CELL_EVENTS){
        if(callback(cell,n->data_arrived,p->channel,p->cookie))p->notified=1;}
        InterlockedExchange(&r->rx_pending,!p->notified);
    }else InterlockedExchange(&r->rx_pending,0);
    LeaveCriticalSection(&r->lock);
}
static int returned(Arm7 *c,uint32_t value)
{
    uint32_t lr=c->r[14];c->r[0]=value;c->cpsr=(c->cpsr&~ARM_T)|((lr&1)?ARM_T:0);c->r[15]=lr&((lr&1)?~1u:~3u);
    ++c->instructions;++c->cycles;return 1;
}
static void trace_packet(PhoneNetwork *n,const char *direction,const Datagram *p)
{
    unsigned i;if(!n->trace)return;
    fprintf(n->trace,"%s bearer=%u cookie=%08x peer=",direction,p->channel,p->cookie);
    for(i=0;i<8;++i)fprintf(n->trace,"%02x",p->peer[i]);fprintf(n->trace," bytes=%u data=",p->size);
    for(i=0;i<p->size;++i)fprintf(n->trace,"%02x",p->bytes[i]);fputc('\n',n->trace);fflush(n->trace);
}
static void retain_url(PhoneNetwork *n,Cellular *cell,Arm7 *c)
{
    uint8_t text[1024];unsigned i;uint32_t value,source=c->r[1],base;
    if(!cell->native_stack||!n->mode||!cell->enabled)return;
    base=cell->native_stack+CELL_NATIVE_STACK_BYTES;
    if(source>=base&&source-base<CELL_NATIVE_URL_BYTES)return;
    for(i=0;i<sizeof(text);++i){if(!bus_peek(c->bus,source+i,1,&value))return;
        text[i]=(uint8_t)value;if(!value)break;}
    if(!i||i==sizeof(text))return;
    base+=(n->url_slot++%4)*1024;
    for(value=0;value<=i;++value)bus_write(c->bus,base+value,1,text[value]);
    c->r[1]=base;
}
int phone_net_hle(PhoneNetwork *n,Cellular *cell,Arm7 *c)
{
    uint32_t pc=c->r[15];unsigned channel=c->r[0];PhoneNet *r=n->runtime;
    if(!n->txn_start||!(c->cpsr&ARM_T))return 0;
    /* The URL scene's edit buffer is cleared while its deferred event is still
       pending during virtual bearer setup. Give that event stable guest-owned
       storage, outside the native-call stack, then execute EventPostEvent itself. */
    if(pc==n->post_event&&c->r[0]==0xb){retain_url(n,cell,c);return 0;}
    if(pc==n->txn_start&&channel<2){int enabled=n->mode&&cell->enabled;
        ++n->start_requests;n->last_bearer=channel;
        if(enabled&&!open_network(n)){snprintf(n->error,sizeof(n->error),"Cannot start the Windows packet-data bridge");enabled=0;}
        n->opened[channel]=enabled?1u:0u;
        bus_write(c->bus,n->wap_base+0x45,1,channel);bus_write(c->bus,n->wap_base+0x1c,4,enabled?1:0);
        bus_write(c->bus,n->wap_base+0x124+channel*8,4,enabled?1:0);
        bus_write(c->bus,n->wap_base+0x128+channel*8,4,enabled?1:0);
        bus_write(c->bus,n->driver_base+4+channel*8,4,enabled?1:0);
        (void)callback(cell,n->post_event,0x11,0);return returned(c,0);
    }
    if(pc==n->bearer_state&&channel<3){unsigned enabled=channel<2&&n->mode&&cell->enabled;
        ++n->state_queries;n->last_bearer=channel;
        /* Native fields +128/+130 are availability; +124/+12c are up state.
           Reversing these makes the browser reject Internet before starting. */
        bus_write(c->bus,c->r[2],4,enabled);bus_write(c->bus,c->r[3],4,channel<2?n->opened[channel]:0);return returned(c,0);}
    if(pc==n->send_pdu&&channel<2){Datagram packet;unsigned i,length=bus_read(c->bus,c->r[13],4)&0xffffu;
        if(!n->mode||!cell->enabled||!r||!n->opened[channel]||!length||length>NET_PACKET)return returned(c,0xffffffffu);
        memset(&packet,0,sizeof(packet));packet.size=length;packet.channel=channel;packet.cookie=c->r[1];
        packet.peer[0]=(uint8_t)bus_read(c->bus,c->r[2],1);if(packet.peer[0]!=4)return returned(c,0xffffffffu);
        for(i=1;i<8;++i)packet.peer[i]=(uint8_t)bus_read(c->bus,c->r[2]+i,1);
        for(i=0;i<length;++i)packet.bytes[i]=(uint8_t)bus_read(c->bus,c->r[3]+i,1);
        trace_packet(n,"TX",&packet);
        cpu_queue_lock(n);if(r->tx_count==NET_QUEUE){LeaveCriticalSection(&r->lock);return returned(c,0xffffffffu);}
        r->tx[(r->tx_head+r->tx_count)%NET_QUEUE]=packet;++r->tx_count;LeaveCriticalSection(&r->lock);SetEvent(r->work);++n->transmitted;
        if(c->bus->log)fprintf(c->bus->log,"WAP TX bearer=%u bytes=%u gateway=%u\n",channel,length,n->mode==2);
        return returned(c,0);
    }
    if(pc==n->recv_pdu&&channel<2){Datagram packet;unsigned i;uint32_t size_pointer=bus_read(c->bus,c->r[13],4),capacity=bus_read(c->bus,size_pointer,4);
        if(!r)return returned(c,0xffffffffu);cpu_queue_lock(n);
        if(!r->rx_count||r->rx[r->rx_head].channel!=channel){LeaveCriticalSection(&r->lock);return returned(c,0xffffffffu);}
        packet=r->rx[r->rx_head];if(packet.size>capacity){LeaveCriticalSection(&r->lock);return returned(c,0xffffffffu);}
        r->rx_head=(r->rx_head+1)%NET_QUEUE;--r->rx_count;
        InterlockedExchange(&r->rx_pending,r->rx_count&&!r->rx[r->rx_head].notified);LeaveCriticalSection(&r->lock);
        trace_packet(n,"RX",&packet);
        for(i=0;i<sizeof(packet.peer);++i)bus_write(c->bus,c->r[2]+i,1,packet.peer[i]);
        for(i=0;i<packet.size;++i)bus_write(c->bus,c->r[3]+i,1,packet.bytes[i]);bus_write(c->bus,size_pointer,4,packet.size);++n->received;
        if(c->bus->log)fprintf(c->bus->log,"WAP RX bearer=%u bytes=%u\n",channel,packet.size);return returned(c,0);
    }
    return 0;
}
