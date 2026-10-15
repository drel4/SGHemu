/* Real loopback TCP + asynchronous system DNS, no internet/ROM/Yamaha needed. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "java_net.h"
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Java network line %d: %s (WSA=%d)\n",__LINE__,#x,WSAGetLastError());exit(1);}}while(0)
static JavaNetwork net;static Arm7 cpu;static Cellular cell;static Bus *bus;
static uint32_t call(unsigned op,uint32_t a,uint32_t b,uint32_t c,uint32_t d,int enabled)
{
    cpu.r[15]=net.entry[op];cpu.r[14]=0x3001;cpu.cpsr=0x33;
    cpu.r[0]=a;cpu.r[1]=b;cpu.r[2]=c;cpu.r[3]=d;
    CHECK(java_net_hle(&net,&cell,&cpu,enabled));CHECK(cpu.r[15]==0x3000);return cpu.r[0];
}
static unsigned event(unsigned wanted)
{
    unsigned i;for(i=0;i<cell.count;++i){CellEvent *e=&cell.events[(cell.head+i)%CELL_EVENTS];
        unsigned value=(unsigned)e->data[0]|((unsigned)e->data[1]<<8);
        CHECK(e->target==CELL_CALLBACK&&e->message==net.entry[JN_NOTIFY]);if(value==wanted)return 1;
    }return 0;
}
static void clear_events(void){cell.head=cell.count=0;}
static void pump(void){cpu.cycles+=65536;java_net_advance(&net,&cell,&cpu,1);Sleep(1);}
int main(void)
{
    WSADATA data;SOCKET listener,peer=INVALID_SOCKET;struct sockaddr_in address={0};int length=sizeof(address);
    u_long nonblocking=1;uint8_t *ram=calloc(1,65536);unsigned i,handle,second;char reply[]="HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhello",bytes[256]={0};
    bus=calloc(1,sizeof(*bus));CHECK(bus&&ram);CHECK(bus_map(bus,0,65536,ram,0,1));
    for(i=0;i<JN_COUNT;++i)net.entry[i]=0x1000+i*0x100;net.first=0x1000;net.last=0x1000+(JN_COUNT-1)*0x100;
    arm7_reset(&cpu,bus,0);cell.enabled=1;CHECK(call(JN_OPEN,0,0,0,0x800,0)==UINT32_MAX&&!net.runtime);
    CHECK(!ram[0x800]);CHECK(call(JN_OPEN,0,0,0,0,1)==0&&net.runtime);
    strcpy((char *)ram+0x4000,"localhost");memset(ram+0x4100,0xee,18);
    CHECK(call(JN_LOOKUP,0x4000,0x4100,1,0x4200,1)==UINT32_MAX&&bus_read(bus,0x4200,2)==0x19);
    for(i=0;i<2000&&!event(0x220);++i)pump();CHECK(event(0x220));
    CHECK(call(JN_LOOKUP,0x4000,0x4100,0,0x4200,1)==0);CHECK(!strcmp((char *)ram+0x4100,"127.0.0.1"));CHECK(ram[0x4110]==0xee);
    clear_events();CHECK(call(JN_LOOKUP,0xffff0000,0x4100,1,0x4200,1)==UINT32_MAX&&!bus->fault);
    CHECK(call(JN_SOCKET,0,1,17,0x4200,1)==UINT32_MAX); /* UDP not silently faked */
    CHECK(!WSAStartup(MAKEWORD(2,2),&data));listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);CHECK(listener!=INVALID_SOCKET);
    address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    CHECK(!bind(listener,(struct sockaddr *)&address,sizeof(address)));CHECK(!listen(listener,1));
    CHECK(!getsockname(listener,(struct sockaddr *)&address,&length));CHECK(!ioctlsocket(listener,FIONBIO,&nonblocking));
    handle=call(JN_SOCKET,0,0,6,0x4200,1);CHECK(handle>=0x100&&handle<0x8000);
    CHECK(call(JN_BIND,handle,0x4300,16,0x4200,1)==0);
    CHECK(call(JN_LOCAL_PORT,handle,0x4200,0,0,1)==0&&bus_read(bus,0x4200,2));
    memcpy(ram+0x4302,&address.sin_port,2);memcpy(ram+0x4304,&address.sin_addr.s_addr,4);
    CHECK(call(JN_CONNECT,handle,0x4300,16,0x4200,1)==0);
    for(i=0;i<2000;++i){if(peer==INVALID_SOCKET)peer=accept(listener,NULL,NULL);pump();if(peer!=INVALID_SOCKET&&event(0x222))break;}
    CHECK(peer!=INVALID_SOCKET&&event(0x222));CHECK(!ioctlsocket(peer,FIONBIO,&nonblocking));clear_events();
    CHECK(call(JN_READ,handle,0x4400,128,0x4200,1)==0); /* wait, not EOF */
    CHECK(!bus_read(bus,0x4200,2)||bus_read(bus,0x4200,2)==0x14);
    strcpy((char *)ram+0x4500,"GET / HTTP/1.0\r\n\r\n");
    CHECK(call(JN_WRITE,handle,0x4500,18,0x4200,1)==18);memset(ram+0x4500,'x',18);
    {int got=0;for(i=0;i<2000&&got<=0;++i){got=recv(peer,bytes,sizeof(bytes),0);if(got<=0)pump();}
     CHECK(got==18&&!memcmp(bytes,"GET / HTTP/1.0\r\n\r\n",18));}
    CHECK(send(peer,reply,(int)strlen(reply),0)==(int)strlen(reply));
    for(i=0;i<2000&&!event(0x1fa);++i)pump();CHECK(event(0x1fa));clear_events();
    CHECK(call(JN_AVAILABLE,handle,0x4200,0,0,1)==strlen(reply));
    CHECK(call(JN_READ,handle,0x4400,128,0x4200,1)==strlen(reply));CHECK(!memcmp(ram+0x4400,reply,strlen(reply)));
    CHECK(call(JN_REMOTE_PORT,handle,0x4200,0,0,1)==0&&bus_read(bus,0x4200,2)==address.sin_port);
    CHECK(call(JN_REMOTE_IP,handle,0x4200,0,0,1)==0&&bus_read(bus,0x4200,4)==address.sin_addr.s_addr);
    closesocket(peer);for(i=0;i<100;++i){unsigned result=call(JN_READ,handle,0x4400,128,0x4200,1);if(result==UINT32_MAX)break;pump();}
    CHECK(bus_read(bus,0x4200,2)==0x4d);CHECK(call(JN_CLOSE,handle,0x4200,0,0,1)==0);
    second=call(JN_SOCKET,0,0,6,0x4200,1);CHECK(second!=handle&&second!=UINT32_MAX);
    CHECK(call(JN_READ,handle,0x4400,128,0x4200,1)==UINT32_MAX);
    memset(ram+0x4200,0xaa,2);CHECK(call(JN_AVAILABLE,handle,0x4200,0,0x4210,1)==UINT32_MAX);
    CHECK(bus_read(bus,0x4200,2)==0x13); /* Error is r1, not stale r3. */
    ram[0x4210]=0xaa;CHECK(call(JN_REMOTE_IP,handle,0x4200,0,0x4210,1)==UINT32_MAX);
    CHECK(ram[0x4210]==0xaa); /* Getters have no error-pointer parameter. */
    clear_events();strcpy((char *)ram+0x4000,"invalid host !");
    CHECK(call(JN_LOOKUP,0x4000,0x4100,1,0x4200,1)==UINT32_MAX);
    for(i=0;i<2000&&!event(0x220);++i)pump();CHECK(event(0x220));
    CHECK(call(JN_LOOKUP,0x4000,0x4100,0,0x4200,1)==UINT32_MAX&&net.error[0]);
    strcpy((char *)ram+0x4000,"localhost");CHECK(call(JN_LOOKUP,0x4000,0x4100,1,0x4200,1)==UINT32_MAX);
    java_net_close(&net);CHECK(!net.runtime);Sleep(20); /* abandoned DNS cannot touch freed guest/runtime */
    closesocket(listener);WSACleanup();bus_destroy(bus);free(bus);
    printf("Java DNS/TCP bridge: %u checks passed.\n",checks);return 0;
}
