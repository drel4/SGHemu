#include "wap_gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,failures;
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
typedef struct {unsigned count,calls;size_t length[64];uint8_t packet[64][1304];size_t body_size;} Fixture;
static int emit(void *user,const uint8_t *p,size_t n)
{
    Fixture *f=user;if(f->count==64||n>1304)return 0;
    memcpy(f->packet[f->count],p,n);f->length[f->count++]=n;return 1;
}
static int fetch(void *user,const char *url,uint8_t **body,size_t *n,char mime[96],unsigned *status)
{
    Fixture *f=user;size_t i;++f->calls;CHECK(!strcmp(url,"http://example.test/test"));
    *n=f->body_size;*body=malloc(*n);if(!*body)return 0;
    for(i=0;i<*n;++i)(*body)[i]=(uint8_t)i;
    strcpy(mime,"application/octet-stream");*status=200;return 1;
}
static size_t request(uint8_t *out,unsigned tid,const char *url)
{
    size_t n=strlen(url);out[0]=0x0a;out[1]=(uint8_t)(tid>>8);out[2]=(uint8_t)tid;
    out[3]=2;out[4]=0x40;out[5]=(uint8_t)n;memcpy(out+6,url,n);return n+6;
}
int main(void)
{
    WapGateway g;Fixture *f=calloc(1,sizeof(*f));uint8_t p[512],ack[]={0x18,0,1};size_t n;unsigned i;
    if(!f)return 1;wap_gateway_init(&g);
    {const uint8_t connect[]={0x0a,0,1,2,1,0x10,0,0};
     CHECK(wap_gateway_receive(&g,connect,sizeof(connect),0,NULL,NULL,emit,f));
     CHECK(f->count==1&&f->packet[0][0]==0x16&&f->packet[0][1]==0x80&&f->packet[0][3]==2);
     CHECK(wap_gateway_receive(&g,ack,sizeof(ack),1,NULL,NULL,emit,f));CHECK(g.transactions[0].complete);}
    n=request(p,2,"http://sghemu.local/");
    CHECK(wap_gateway_receive(&g,p,n,2,NULL,NULL,emit,f));CHECK(f->count==2);
    CHECK(f->packet[1][3]==4&&f->packet[1][4]==0x20&&f->packet[1][6]==0x94);
    n=request(p,3,"http://example.test/test");f->body_size=6000;f->count=0;
    CHECK(wap_gateway_receive(&g,p,n,100,fetch,f,emit,f));CHECK(f->calls==1&&f->count==3);
    CHECK(f->packet[0][0]==0x10&&f->packet[1][0]==0x30&&f->packet[2][0]==0x34);
    CHECK(f->packet[1][3]==1&&f->packet[2][3]==2);CHECK(f->length[0]==1303&&f->length[1]==1304);
    CHECK(wap_gateway_receive(&g,p,n,200,fetch,f,emit,f));CHECK(f->calls==1&&f->count==6&&f->packet[3][0]==0x11);
    ack[2]=3;CHECK(wap_gateway_receive(&g,ack,3,300,fetch,f,emit,f));CHECK(f->count==8);
    CHECK(f->packet[6][3]==3&&f->packet[7][3]==4&&f->packet[7][0]==0x32);
    CHECK(wap_gateway_tick(&g,7300,emit,f));CHECK(f->count>=10&&f->packet[f->count-1][0]==0x33);
    CHECK(wap_gateway_receive(&g,ack,3,7400,fetch,f,emit,f));
    CHECK(wap_gateway_tick(&g,47400,emit,f));CHECK(!g.transactions[2].active);
    /* Captured native C100 blank-homepage GET, not a synthetic API request. */
    {const uint8_t empty_home[]={0x0a,0x3b,0xb6,0x12,0x40,0};f->count=0;
     CHECK(wap_gateway_receive(&g,empty_home,sizeof(empty_home),48000,NULL,NULL,emit,f));
     CHECK(f->count==1&&f->packet[0][4]==0x20);}
    /* A reused transaction ID with different data must not repeat HTTP. */
    n=request(p,4,"http://example.test/test");f->count=0;f->body_size=10;
    CHECK(wap_gateway_receive(&g,p,n,50000,fetch,f,emit,f));p[n-1]='X';
    CHECK(wap_gateway_receive(&g,p,n,50001,fetch,f,emit,f));CHECK(f->calls==2&&f->packet[1][0]==0x21);
    /* Malformed length/TPI/concatenation inputs are bounded and never fetch. */
    for(i=0;i<sizeof(p);++i){size_t j;for(j=0;j<i;++j)p[j]=(uint8_t)(j*37+i);
        f->count=0;(void)wap_gateway_receive(&g,p,i,50002,NULL,NULL,emit,f);}
    CHECK(g.malformed>0);wap_gateway_close(&g);wap_gateway_init(&g);
    /* Rapid paging must not exhaust the completed-reply cache. */
    for(i=0;i<24;++i){unsigned tid=100+i;f->count=0;n=request(p,tid,"http://sghemu.local/");
        CHECK(wap_gateway_receive(&g,p,n,i*2,NULL,NULL,emit,f));CHECK(f->count==1);
        ack[1]=(uint8_t)(tid>>8);ack[2]=(uint8_t)tid;
        CHECK(wap_gateway_receive(&g,ack,3,i*2+1,NULL,NULL,emit,f));}
    wap_gateway_close(&g);wap_gateway_init(&g);f->count=0;
    {const uint8_t connect[]={0x0a,0,1,2,1,0x10,0,0};
     uint8_t resume[]={0x0a,0,2,0x12,9,1,0};
     CHECK(wap_gateway_receive(&g,connect,sizeof(connect),0,NULL,NULL,emit,f));
     ack[1]=0;ack[2]=1;CHECK(wap_gateway_receive(&g,ack,3,1,NULL,NULL,emit,f));
     CHECK(wap_gateway_receive(&g,resume,sizeof(resume),2,NULL,NULL,emit,f));
     CHECK(f->count==2&&f->length[1]==9&&f->packet[1][3]==4&&f->packet[1][4]==0x20);
     ack[2]=2;CHECK(wap_gateway_receive(&g,ack,3,3,NULL,NULL,emit,f));
     n=request(p,3,"http://sghemu.local/");CHECK(wap_gateway_receive(&g,p,n,4,NULL,NULL,emit,f));
     CHECK(f->packet[2][3]==4&&f->packet[2][4]==0x20);
     resume[2]=4;resume[5]=2;CHECK(wap_gateway_receive(&g,resume,sizeof(resume),5,NULL,NULL,emit,f));
     CHECK(f->packet[3][4]==0x40); /* Unknown session, not a spurious new connection. */
     resume[2]=5;resume[5]=1;resume[6]=1;
     CHECK(wap_gateway_receive(&g,resume,sizeof(resume),6,NULL,NULL,emit,f));CHECK(f->packet[4][4]==0x40);}
    wap_gateway_close(&g);free(f);
    printf("WAP gateway: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
