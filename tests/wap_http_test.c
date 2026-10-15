#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include "wap_gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct {SOCKET socket;volatile LONG stop;} Server;
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
static unsigned __stdcall serve(void *user)
{
    Server *s=user;SOCKET client=INVALID_SOCKET;char request[4096];DWORD timeout=1000;
    const char reply[]="HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 15\r\nConnection: close\r\n\r\ngateway-http-ok";
    while(!InterlockedCompareExchange(&s->stop,0,0)){client=accept(s->socket,NULL,NULL);if(client!=INVALID_SOCKET)break;Sleep(5);}
    if(client!=INVALID_SOCKET){size_t sent=0;u_long blocking=0;ioctlsocket(client,FIONBIO,&blocking);setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(const char *)&timeout,sizeof(timeout));
        if(recv(client,request,sizeof(request),0)>0)while(sent<sizeof(reply)-1){int n=send(client,reply+sent,(int)(sizeof(reply)-1-sent),0);if(n<=0)break;sent+=(unsigned)n;}
        closesocket(client);}
    return 0;
}
int main(void)
{
    WSADATA wsa;Server server={INVALID_SOCKET,0};struct sockaddr_in address={0};int size=sizeof(address);u_long nonblocking=1;
    HANDLE thread;WapHttpContext ctx={0};char url[100],mime[96];uint8_t *body=NULL;size_t length=0;unsigned status=0;
    CHECK(!WSAStartup(MAKEWORD(2,2),&wsa));server.socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    CHECK(server.socket!=INVALID_SOCKET);address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    CHECK(!bind(server.socket,(struct sockaddr *)&address,sizeof(address)));CHECK(!listen(server.socket,1));
    CHECK(!getsockname(server.socket,(struct sockaddr *)&address,&size));CHECK(!ioctlsocket(server.socket,FIONBIO,&nonblocking));
    thread=(HANDLE)_beginthreadex(NULL,0,serve,&server,0,NULL);CHECK(thread!=NULL);
    snprintf(url,sizeof(url),"http://127.0.0.1:%u/test",(unsigned)ntohs(address.sin_port));
    CHECK(wap_http_fetch(&ctx,url,&body,&length,mime,&status));
    CHECK(status==200&&length==15&&!strcmp(mime,"text/plain"));
    CHECK(body&&!memcmp(body,"gateway-http-ok",15));free(body);body=NULL;
    InterlockedExchange(&server.stop,1);if(thread){CHECK(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0);CloseHandle(thread);}
    CHECK(!wap_http_fetch(&ctx,"file:///not-http",&body,&length,mime,&status)&&!body);
    wap_http_cancel(&ctx);CHECK(!wap_http_fetch(&ctx,url,&body,&length,mime,&status)&&!body);
    closesocket(server.socket);WSACleanup();printf("WAP HTTP worker: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
