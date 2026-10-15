/* Firmware DS socket boundary. No Java bytecode/HTTP/installer replacement. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include "java_net.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define STREAMS 8
#define TX_BYTES 65536u
typedef struct {volatile LONG refs,done;int success;char host[256],address[16];} DnsJob;
typedef struct {
    SOCKET socket;unsigned handle,connecting,read_wait,event;
    unsigned tx_head,tx_count;uint8_t tx[TX_BYTES];
} Stream;
struct JavaNetRuntime {Stream stream[STREAMS];DnsJob *dns;unsigned dns_notified;};
static void dns_release(DnsJob *job){if(job&&!InterlockedDecrement(&job->refs))free(job);}
static unsigned __stdcall resolve(void *context)
{
    DnsJob *job=context;WSADATA data;struct addrinfo hints={0},*result=NULL,*p;
    if(!WSAStartup(MAKEWORD(2,2),&data)){
        hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
        if(!getaddrinfo(job->host,NULL,&hints,&result)){
            for(p=result;p;p=p->ai_next)if(p->ai_family==AF_INET&&p->ai_addrlen>=sizeof(struct sockaddr_in)){
                const uint8_t *ip=(const uint8_t *)&((struct sockaddr_in *)p->ai_addr)->sin_addr;
                snprintf(job->address,sizeof(job->address),"%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);job->success=1;break;
            }
            freeaddrinfo(result);
        }WSACleanup();
    }
    InterlockedExchange(&job->done,1);dns_release(job);return 0;
}
static int open_runtime(JavaNetwork *n)
{
    WSADATA data;unsigned i;
    if(n->runtime)return 1;
    if(WSAStartup(MAKEWORD(2,2),&data))return 0;
    n->runtime=calloc(1,sizeof(*n->runtime));if(!n->runtime){WSACleanup();return 0;}
    for(i=0;i<STREAMS;++i)n->runtime->stream[i].socket=INVALID_SOCKET;
    return 1;
}
void java_net_close(JavaNetwork *n)
{
    JavaNetRuntime *r=n->runtime;unsigned i;if(!r)return;
    for(i=0;i<STREAMS;++i)if(r->stream[i].socket!=INVALID_SOCKET)closesocket(r->stream[i].socket);
    /* A slow resolver owns its own job/ref/WSA lifetime, not this runtime or
       guest RAM. Reset/close never waits for DNS or accepts a stale result. */
    dns_release(r->dns);free(r);n->runtime=NULL;n->next_poll=0;WSACleanup();
}
static Stream *stream(JavaNetwork *n,unsigned handle)
{
    unsigned i;if(n->runtime)for(i=0;i<STREAMS;++i){Stream *s=&n->runtime->stream[i];
        if(s->socket!=INVALID_SOCKET&&s->handle==handle)return s;}
    return NULL;
}
static int notify(Cellular *cell,uint32_t address,unsigned event,unsigned handle)
{
    uint8_t args[8];unsigned i;if(cell->count==CELL_EVENTS)return 0;
    for(i=0;i<4;++i){args[i]=(uint8_t)(event>>(8*i));args[i+4]=(uint8_t)(handle>>(8*i));}
    return cellular_queue(cell,CELL_CALLBACK,address,args,sizeof(args));
}
static void flush(Stream *s)
{
    if(s->connecting||!s->tx_count)return;
    {unsigned count=TX_BYTES-s->tx_head;int sent;
        if(count>s->tx_count)count=s->tx_count;
        sent=send(s->socket,(const char *)s->tx+s->tx_head,(int)count,0);
        if(sent>0){s->tx_head=(s->tx_head+(unsigned)sent)%TX_BYTES;s->tx_count-=(unsigned)sent;}
        else if(sent==SOCKET_ERROR&&WSAGetLastError()!=WSAEWOULDBLOCK)s->event=0x223;
    }
}
void java_net_advance(JavaNetwork *n,Cellular *cell,Arm7 *cpu,int enabled)
{
    JavaNetRuntime *r=n->runtime;unsigned i;
    if(!r)return;
    if(!enabled){java_net_close(n);return;}
    if(cpu->cycles<n->next_poll)return;n->next_poll=cpu->cycles+65536;
    if(r->dns&&!r->dns_notified&&InterlockedCompareExchange(&r->dns->done,0,0)){
        if(notify(cell,n->entry[JN_NOTIFY],0x220,0))r->dns_notified=1;
    }
    for(i=0;i<STREAMS;++i){Stream *s=&r->stream[i];
        if(s->socket==INVALID_SOCKET)continue;
        if(s->connecting){fd_set write_set,error_set;struct timeval timeout={0};
            FD_ZERO(&write_set);FD_ZERO(&error_set);FD_SET(s->socket,&write_set);FD_SET(s->socket,&error_set);
            if(select(0,NULL,&write_set,&error_set,&timeout)>0){int error=0,len=sizeof(error);
                if(getsockopt(s->socket,SOL_SOCKET,SO_ERROR,(char *)&error,&len))error=WSAGetLastError();
                s->connecting=0;s->event=error?0x223:0x222;
            }
        }
        flush(s);
        if(s->read_wait&&!s->event&&!s->connecting){fd_set read_set;struct timeval timeout={0};
            FD_ZERO(&read_set);FD_SET(s->socket,&read_set);
            if(select(0,&read_set,NULL,NULL,&timeout)>0)s->event=0x1fa;
        }
        if(s->event&&notify(cell,n->entry[JN_NOTIFY],s->event,s->handle)){
            if(s->event==0x1fa)s->read_wait=0;s->event=0;
        }
    }
}
static int memory(Bus *b,uint32_t address,unsigned size,int write)
{
    unsigned i;if(!size)return 1;
    for(i=0;i<b->count;++i){Region *r=&b->regions[i];
        if(r->data&&!r->read&&(!write||!r->readonly)&&address>=r->base&&
            (uint64_t)address+size<=(uint64_t)r->base+r->size)return 1;
    }return 0;
}
static int returned(Arm7 *cpu,uint32_t value)
{
    uint32_t lr=cpu->r[14];cpu->r[0]=value;cpu->cpsr=(cpu->cpsr&~ARM_T)|((lr&1)?ARM_T:0);
    cpu->r[15]=lr&((lr&1)?~1u:~3u);++cpu->instructions;++cpu->cycles;return 1;
}
static int error_return(Arm7 *cpu,uint32_t error_pointer,unsigned error,uint32_t value)
{
    if(error_pointer&&memory(cpu->bus,error_pointer,2,1))bus_write(cpu->bus,error_pointer,2,error);
    return returned(cpu,value);
}
int java_net_hle(JavaNetwork *n,Cellular *cell,Arm7 *c,int enabled)
{
    unsigned op,a=c->r[0],b=c->r[1],length=c->r[2];uint32_t err=c->r[3];Stream *s;
    (void)cell;
    if(!(c->cpsr&ARM_T)||!java_net_may_call(n,c->r[15]))return 0;
    for(op=0;op<JN_COUNT;++op)if(op!=JN_NOTIFY&&n->entry[op]==c->r[15])break;
    if(op==JN_COUNT)return 0;
    if(op==JN_SHUTDOWN){java_net_close(n);return returned(c,0);}
    if(!enabled||!open_runtime(n)){
        uint32_t pointer=op==JN_CLOSE||op==JN_AVAILABLE?b:
            op==JN_SOCKET||op==JN_CONNECT||op==JN_BIND||op==JN_WRITE||op==JN_READ||op==JN_LOOKUP?err:0;
        return error_return(c,pointer,0x78,UINT32_MAX);}
    if(op==JN_OPEN)return returned(c,0);
    if(op==JN_LOOKUP){JavaNetRuntime *r=n->runtime;unsigned i;char name[256]={0};int terminated=0;
        if(!memory(c->bus,b,16,1)||!memory(c->bus,err,2,1))return error_return(c,err,0x13,UINT32_MAX);
        for(i=0;i<sizeof(name);++i){uint32_t ch;if(!bus_peek(c->bus,a+i,1,&ch))break;
            name[i]=(char)ch;if(!ch){terminated=1;break;}}
        if(!i||!terminated)return error_return(c,err,0x13,UINT32_MAX);
        if(length){uintptr_t thread;DnsJob *job;
            dns_release(r->dns);r->dns=NULL;r->dns_notified=0;
            job=calloc(1,sizeof(*job));if(!job)return error_return(c,err,0x7c,UINT32_MAX);
            job->refs=2;memcpy(job->host,name,i+1);thread=_beginthreadex(NULL,0,resolve,job,0,NULL);
            if(!thread){free(job);return error_return(c,err,0x7c,UINT32_MAX);}
            CloseHandle((HANDLE)thread);r->dns=job;++n->lookups;n->next_poll=0;
            n->error[0]=0;if(c->bus->log)fprintf(c->bus->log,"Java DNS request host=%s\n",name);
        }else if(r->dns&&!strcmp(r->dns->host,name)&&InterlockedCompareExchange(&r->dns->done,0,0)){
            if(r->dns->success){for(i=0;i<16;++i)bus_write(c->bus,b+i,1,(uint8_t)r->dns->address[i]);
                if(c->bus->log)fprintf(c->bus->log,"Java DNS reply host=%s address=%s\n",name,r->dns->address);
                return error_return(c,err,0,0);}
            snprintf(n->error,sizeof(n->error),"Java DNS could not resolve %.100s",name);
        }
        return error_return(c,err,0x19,UINT32_MAX);
    }
    if(op==JN_SOCKET){unsigned i;u_long nonblocking=1;JavaNetRuntime *r=n->runtime;
        if(a||b||length!=6||!memory(c->bus,err,2,1))return error_return(c,err,0x6a,UINT32_MAX);
        for(i=0;i<STREAMS;++i)if(r->stream[i].socket==INVALID_SOCKET)break;
        if(i==STREAMS)return error_return(c,err,0x7d,UINT32_MAX);s=&r->stream[i];memset(s,0,sizeof(*s));
        s->socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if(s->socket==INVALID_SOCKET)return error_return(c,err,0x7d,UINT32_MAX);
        if(ioctlsocket(s->socket,FIONBIO,&nonblocking)){closesocket(s->socket);s->socket=INVALID_SOCKET;return error_return(c,err,0x7d,UINT32_MAX);}
        s->handle=0x100+(++n->serial%0x7e00);return error_return(c,err,0,s->handle);
    }
    s=stream(n,a);if(!s){uint32_t pointer=op==JN_CLOSE||op==JN_AVAILABLE?b:
            op==JN_CONNECT||op==JN_BIND||op==JN_READ||op==JN_WRITE?err:0;
        return error_return(c,pointer,0x13,UINT32_MAX);}
    if(op==JN_CLOSE){closesocket(s->socket);s->socket=INVALID_SOCKET;return error_return(c,b,0,0);}
    if(op==JN_CONNECT||op==JN_BIND){struct sockaddr_in address={0};int result;
        if(length!=16||!memory(c->bus,b,16,0))return error_return(c,err,0x46,UINT32_MAX);
        address.sin_family=AF_INET;address.sin_port=(u_short)bus_read(c->bus,b+2,2);
        address.sin_addr.s_addr=bus_read(c->bus,b+4,4);
        if(op==JN_BIND){result=bind(s->socket,(struct sockaddr *)&address,sizeof(address));
            return error_return(c,err,result?0x47:0,result?UINT32_MAX:0);}
        result=connect(s->socket,(struct sockaddr *)&address,sizeof(address));
        if(result==SOCKET_ERROR&&WSAGetLastError()!=WSAEWOULDBLOCK)return error_return(c,err,0x47,UINT32_MAX);
        s->connecting=result==SOCKET_ERROR;s->event=s->connecting?0:0x222;
        n->next_poll=0;++n->connects;return error_return(c,err,0,0);
    }
    if(op==JN_AVAILABLE){u_long available=0;
        if(!memory(c->bus,b,2,1)||ioctlsocket(s->socket,FIONREAD,&available))return returned(c,UINT32_MAX);
        bus_write(c->bus,b,2,0);return returned(c,available>32767?32767:available);
    }
    if(op==JN_LOCAL_IP||op==JN_REMOTE_IP||op==JN_LOCAL_PORT||op==JN_REMOTE_PORT){
        struct sockaddr_in address={0};int bytes=sizeof(address),result;
        unsigned width=op==JN_LOCAL_IP||op==JN_REMOTE_IP?4:2;
        if(!memory(c->bus,b,width,1))return returned(c,UINT32_MAX);
        result=op==JN_LOCAL_IP||op==JN_LOCAL_PORT?getsockname(s->socket,(struct sockaddr *)&address,&bytes):
            getpeername(s->socket,(struct sockaddr *)&address,&bytes);
        if(result)return returned(c,UINT32_MAX);
        bus_write(c->bus,b,width,width==4?address.sin_addr.s_addr:address.sin_port);return returned(c,0);
    }
    if(op==JN_READ||op==JN_WRITE){unsigned i;
        if(!length||length>32767||!memory(c->bus,b,length,op==JN_READ)||!memory(c->bus,err,2,1))
            return error_return(c,err,0x13,UINT32_MAX);
        if(op==JN_WRITE){if(length>TX_BYTES-s->tx_count)return error_return(c,err,0x19,UINT32_MAX);
            for(i=0;i<length;++i)s->tx[(s->tx_head+s->tx_count+i)%TX_BYTES]=(uint8_t)bus_read(c->bus,b+i,1);
            s->tx_count+=length;n->sent+=length;flush(s);n->next_poll=0;return error_return(c,err,0,length);
        }else {uint8_t bytes[32767];int got=recv(s->socket,(char *)bytes,(int)length,0);
            if(got>0){for(i=0;i<(unsigned)got;++i)bus_write(c->bus,b+i,1,bytes[i]);n->received+=(unsigned)got;return error_return(c,err,0,(uint32_t)got);}
            if(got==0)return error_return(c,err,0x4d,UINT32_MAX);
            if(WSAGetLastError()==WSAEWOULDBLOCK){s->read_wait=1;n->next_poll=0;return error_return(c,err,0x14,0);}
            return error_return(c,err,0x47,UINT32_MAX);
        }
    }
    return 0;
}
