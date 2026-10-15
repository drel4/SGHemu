/* C port of the WAPWarp WTP/WSP framing approach. Protocol constants follow
   WAP-224-WTP, WAP-230-WSP, WAP-191-WML and WAP-192-WBXML. No Python runtime. */
#include "wap_gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
static void clear_transaction(WapTransaction *t){free(t->reply);free(t->request);memset(t,0,sizeof(*t));}
void wap_gateway_init(WapGateway *g){memset(g,0,sizeof(*g));g->session=1;}
void wap_gateway_close(WapGateway *g){unsigned i;for(i=0;i<WAP_TRANSACTIONS;++i)clear_transaction(&g->transactions[i]);}
static int uintvar(const uint8_t *data,size_t n,size_t *at,uint32_t *value)
{
    unsigned i;uint32_t v=0;for(i=0;i<5;++i){uint8_t b;if(*at>=n)return 0;b=data[(*at)++];if(v>0x1ffffffu)return 0;v=(v<<7)|(b&127);if(!(b&128)){*value=v;return 1;}}return 0;
}
static unsigned put_uintvar(uint8_t *out,uint32_t value)
{
    uint8_t tmp[5];unsigned n=0,i;do{tmp[n++]=(uint8_t)(value&127);value>>=7;}while(value);
    for(i=0;i<n;++i)out[i]=(uint8_t)(tmp[n-i-1]|(i+1<n?128:0));return n;
}
static int send_packet(WapTransaction *t,unsigned seq,int retry,WapEmit emit,void *user)
{
    uint8_t packet[WAP_SEGMENT+4];size_t offset,n;unsigned header;uint16_t tid=t->tid^0x8000u;
    if(t->packets==1){offset=0;n=t->size;header=3;packet[0]=0x16;}
    else{offset=(size_t)seq*WAP_SEGMENT;n=t->size-offset;if(n>WAP_SEGMENT)n=WAP_SEGMENT;header=seq?4:3;
        packet[0]=(uint8_t)((seq?6:2)<<3);if(seq+1==t->packets)packet[0]|=2;else if((seq+1)%3==0)packet[0]|=4;
        if(seq)packet[3]=(uint8_t)seq;}
    if(retry)packet[0]|=1;packet[1]=(uint8_t)(tid>>8);packet[2]=(uint8_t)tid;
    /* A single non-SAR Result is kept at <=1300 too: it fits this buffer and
       the old handset's UDP receive path without IP fragmentation. */
    if(n>WAP_SEGMENT)return 0;memcpy(packet+header,t->reply+offset,n);return emit(user,packet,n+header);
}
static int send_group(WapTransaction *t,uint64_t now,int retry,WapEmit emit,void *user)
{
    unsigned i;t->due=now+7000;t->end=t->first+3;if(t->end>t->packets)t->end=t->packets;
    for(i=t->first;i<t->end;++i)if(!send_packet(t,i,retry,emit,user))return 0;t->due=now+7000;return 1;
}
static int append(uint8_t *out,size_t *n,size_t cap,const void *p,size_t count)
{
    if(count>cap-*n)return 0;memcpy(out+*n,p,count);*n+=count;return 1;
}
static int byte(uint8_t *out,size_t *n,size_t cap,uint8_t value){return append(out,n,cap,&value,1);}
static int inline_text(uint8_t *out,size_t *n,size_t cap,const char *text,size_t size)
{
    return byte(out,n,cap,3)&&append(out,n,cap,text,size)&&byte(out,n,cap,0);
}
static size_t plain_wmlc(uint8_t *out,size_t cap,const uint8_t *text,size_t length,int html,const char *base)
{
    static const uint8_t start[]={3,10,106,0,0x7f,0x67,0x60};size_t n=0,i=0;char chunk[2052];size_t used=0;int space=0,skip=0;
    if(!append(out,&n,cap,start,sizeof(start)))return 0;
    while(i<length&&(n+2200<cap||cap<2200)){unsigned char c=text[i++];
        if(html&&c=='<'){size_t begin=i,tag_start=i,end=i;char tag[512];unsigned k=0;
            while(end<length&&text[end]!='>')++end;i=end<length?end+1:length;
            while(begin<end&&k+1<sizeof(tag))tag[k++]=(char)tolower(text[begin++]);tag[k]=0;
            if(!strncmp(tag,"script",6)||!strncmp(tag,"style",5)){skip=1;continue;}
            if(!strncmp(tag,"/script",7)||!strncmp(tag,"/style",6)){skip=0;continue;}if(skip)continue;
            if(!strncmp(tag,"a ",2)){const char *href=strstr(tag,"href=");
                if(href){char url[1024],raw[512];char quote;size_t u=0;href+=5;quote=*href=='\''||*href=='\"'?*href++:0;
                    while(*href&&u+1<sizeof(raw)&&(quote?*href!=quote:!isspace((unsigned char)*href)))raw[u++]=*href++;raw[u]=0;
                    /* Resolve relative links; preserve the original case of the
                       source href (the lower-case tag is only for recognition). */
                    {size_t original=(size_t)(strstr(tag,"href=")-tag)+5;
                     size_t src=tag_start+original;if(src<end&&(text[src]=='\''||text[src]=='\"'))++src;
                     u=0;while(src<end&&u+1<sizeof(raw)&&(quote?text[src]!=(uint8_t)quote:!isspace(text[src])))raw[u++]=(char)text[src++];raw[u]=0;}
                    if(!strncmp(raw,"http://",7)||!strncmp(raw,"https://",8))snprintf(url,sizeof(url),"%s",raw);
                    else {const char *scheme=strstr(base,"://"),*slash=scheme?strchr(scheme+3,'/'):NULL;size_t prefix;
                        if(raw[0]=='/')prefix=slash?(size_t)(slash-base):strlen(base);
                        else {const char *last=strrchr(base,'/');prefix=last&&scheme&&last>scheme+2?(size_t)(last-base+1):strlen(base);}
                        if(prefix+strlen(raw)+2>=sizeof(url))continue;memcpy(url,base,prefix);url[prefix]=0;
                        if(!slash&&raw[0]!='/')strcat(url,"/");strcat(url,raw);}
                    if(strncmp(url,"http://",7)&&strncmp(url,"https://",8))continue;
                    if(used){if(!inline_text(out,&n,cap,chunk,used))return 0;used=0;}
                    {uint8_t open[]={0xdc,0x4a};if(!append(out,&n,cap,open,2)||!inline_text(out,&n,cap,url,strlen(url))||!byte(out,&n,cap,1)||!inline_text(out,&n,cap,url,strlen(url))||!byte(out,&n,cap,1)||!byte(out,&n,cap,0x26))return 0;}
                }
            }
            space=1;continue;
        }
        if(skip)continue;
        if(c=='&'&&html){size_t end=i;while(end<length&&end-i<10&&text[end]!=';'&&!isspace(text[end]))++end;
            if(end<length&&text[end]==';'){
                if(end-i==3&&!memcmp(text+i,"amp",3))c='&';else if(end-i==2&&!memcmp(text+i,"lt",2))c='<';
                else if(end-i==2&&!memcmp(text+i,"gt",2))c='>';else if(end-i==4&&!memcmp(text+i,"nbsp",4))c=' ';
                else if(end-i==4&&!memcmp(text+i,"quot",4))c='\"';else c=' ';i=end+1;}}
        if(c<=32){space=1;continue;}if(space&&used&&used+1<sizeof(chunk))chunk[used++]=' ';space=0;
        if(used+1>=sizeof(chunk)||(used>=2048&&(c&0xc0)!=0x80)){if(!inline_text(out,&n,cap,chunk,used))return 0;used=0;}
        chunk[used++]=(char)c;
    }
    if(used&&!inline_text(out,&n,cap,chunk,used))return 0;
    if(!byte(out,&n,cap,1)||!byte(out,&n,cap,1)||!byte(out,&n,cap,1))return 0;return n;
}
#include "wap_html.inc"
static int make_reply(WapGateway *g,WapTransaction *t,const uint8_t *data,size_t size,WapFetch fetch,void *user)
{
    uint8_t *body=NULL,*converted=NULL,headers[128],prefix[8];size_t length=0,h=0,at=1,n=0;uint32_t uri_size;unsigned status=200,encoded_status=0x20,page=1,pages=0;char url[1024],mime[96]="text/plain";int ok=0;
    t->reply=malloc(WAP_REPLY_MAX);if(!t->reply)return 0;
    if(data[0]==1){/* ConnectReply: server receive SDU 4096; no Push or large-data promise. */
        static const uint8_t reply[]={2,1,4,0,3,0x81,0xa0,0};g->session=1;memcpy(t->reply,reply,sizeof(reply));t->size=sizeof(reply);return 1;}
    if(data[0]==9){static const uint8_t reply[]={2,1,0,0};memcpy(t->reply,reply,sizeof(reply));t->size=sizeof(reply);return 1;}
    if(data[0]!=0x40&&data[0]!=0x42){status=405;goto error_page;}
    if(!uintvar(data,size,&at,&uri_size)||uri_size>=sizeof(url)||uri_size>size-at){status=400;goto error_page;}
    /* The C100 sends GET with a zero-length URI when its homepage is blank.
       Offer the local landing page rather than requiring keypad URL entry. */
    if(!uri_size)strcpy(url,"http://sghemu.local/");
    else {memcpy(url,data+at,uri_size);url[uri_size]=0;if(memchr(url,0,uri_size)){status=400;goto error_page;}}
    if(!page_selector(url,&page)){status=400;goto error_page;}
    if(!strncmp(url,"http://sghemu.local",19)&&(!url[19]||url[19]=='/'||url[19]=='?'||url[19]=='#')){
        static const char home[]="SGHemu built-in WAP gateway is connected. Enter an http:// or https:// address in the phone browser.";
        length=sizeof(home)-1;body=malloc(length);if(!body)return 0;memcpy(body,home,length);strcpy(mime,"text/plain");
    }else if(!fetch||!fetch(user,url,&body,&length,mime,&status)){status=502;goto error_page;}
    if(strstr(mime,"html")||strstr(mime,"text/plain")||strstr(mime,"text/vnd.wap.wml")){
        converted=malloc(WAP_PAGE_BYTES);if(!converted)goto done;
        n=html_pages(converted,body,length,(strstr(mime,"html")!=NULL||strstr(mime,"text/vnd.wap.wml")!=NULL),url,page,&pages);
        if(!n){status=page>pages&&pages?404:413;goto error_page;}
        free(body);body=converted;converted=NULL;length=n;strcpy(mime,"application/vnd.wap.wmlc");
    }
    if(length>WAP_REPLY_MAX-256){status=413;goto error_page;}
    goto encode;
error_page:
    free(body);body=malloc(256);if(!body)goto done;
    {char message[128];snprintf(message,sizeof(message),"SGHemu gateway: HTTP %u. The requested page could not be delivered.",status);
     length=plain_wmlc(body,256,(const uint8_t *)message,strlen(message),0,"");if(!length)goto done;strcpy(mime,"application/vnd.wap.wmlc");}
encode:
    encoded_status=(status>=500?0x60:status>=400?0x40:status>=300?0x30:0x20)+(status%100);if(encoded_status>255)encoded_status=0x60;
    if(!strcmp(mime,"application/vnd.wap.wmlc"))headers[h++]=0x94;
    else if(!strcmp(mime,"image/gif"))headers[h++]=0x9d;
    else if(!strcmp(mime,"image/jpeg"))headers[h++]=0x9e;
    else if(!strcmp(mime,"image/png"))headers[h++]=0xa0;
    else {size_t m=strlen(mime);if(m+1>sizeof(headers))goto done;memcpy(headers,mime,m+1);h=m+1;}
    headers[h++]=0x8d;
    if(length<128)headers[h++]=(uint8_t)(length|128);
    else {uint8_t integer[4];unsigned bytes=0,k;uint32_t value=(uint32_t)length;do{integer[bytes++]=(uint8_t)value;value>>=8;}while(value);
        headers[h++]=(uint8_t)bytes;for(k=0;k<bytes;++k)headers[h++]=integer[bytes-k-1];}
    prefix[0]=4;prefix[1]=(uint8_t)encoded_status;n=2+put_uintvar(prefix+2,(uint32_t)h);
    if(n+h+length>WAP_REPLY_MAX)goto done;memcpy(t->reply,prefix,n);memcpy(t->reply+n,headers,h);
    if(data[0]!=0x42)memcpy(t->reply+n+h,body,length);else length=0;t->size=n+h+length;ok=1;
done:free(body);free(converted);return ok;
}
static int receive_pdu(WapGateway *g,const uint8_t *data,size_t n,uint64_t now,WapFetch fetch,void *fetch_user,WapEmit emit,void *emit_user)
{
    unsigned type,psn=0,have_psn=0,i;size_t at;uint16_t tid;WapTransaction *t=NULL;
    if(n<3||!data[0])goto malformed;type=(data[0]>>3)&15;tid=(uint16_t)((data[1]<<8)|data[2]);at=type==1||type==4||type==5||type==6?4:3;
    if(type==7){if(n<4||data[3]>n-4)goto malformed;at=4+data[3];}if(at>n)goto malformed;
    if(data[0]&128){int more=1;while(more){unsigned header,length,id;if(at>=n)goto malformed;header=data[at++];more=header&128;id=(header>>3)&15;
        if(header&4){if((header&3)||at>=n)goto malformed;length=data[at++];}else length=header&3;
        if(length>n-at)goto malformed;if(id==3&&length==1){have_psn=1;psn=data[at];}at+=length;}}
    for(i=0;i<WAP_TRANSACTIONS;++i)if(g->transactions[i].active&&(g->transactions[i].tid&0x7fff)==(tid&0x7fff)){t=&g->transactions[i];break;}
    if(type==3){if(!t)return 1;if(t->complete)return 1;if(!have_psn)psn=t->end-1;
        if(psn>=t->end||psn+1<t->end)return 1;
        if(t->end==t->packets){t->complete=1;t->expires=now+40000;return 1;}t->first=t->end;t->retries=0;return send_group(t,now,0,emit,emit_user);}
    if(type==7){if(!t)return 1;for(i=0;i<data[3];++i)if(data[4+i]<t->packets&&!send_packet(t,data[4+i],1,emit,emit_user))return 0;return 1;}
    if(type==4){if(t)clear_transaction(t);return 1;}
    if(type!=1||n<4||at>=n)goto malformed;
    if((data[3]&3)==0)return 1;
    if((data[3]&3)==1){uint8_t ack[]={0x18,(uint8_t)((tid^0x8000u)>>8),(uint8_t)tid};return emit(emit_user,ack,sizeof(ack));}
    if((data[3]&3)==3)goto malformed;
    if(t){if(t->request_size!=n-at||memcmp(t->request,data+at,n-at)){uint8_t abort[]={0x21,(uint8_t)((tid^0x8000u)>>8),(uint8_t)tid,0x03};return emit(emit_user,abort,sizeof(abort));}
        t->first=0;t->complete=0;t->retries=0;return send_group(t,now,1,emit,emit_user);}
    for(i=0;i<WAP_TRANSACTIONS;++i)if(!g->transactions[i].active||(g->transactions[i].complete&&now>=g->transactions[i].expires)){t=&g->transactions[i];break;}
    /* Paging can produce many acknowledged requests inside the cache lifetime.
       Reclaim the oldest completed reply; never evict an outstanding transfer. */
    if(!t)for(i=0;i<WAP_TRANSACTIONS;++i)if(g->transactions[i].complete&&(!t||g->transactions[i].expires<t->expires))t=&g->transactions[i];
    if(!t){snprintf(g->error,sizeof(g->error),"WAP transaction capacity reached");return 0;}
    clear_transaction(t);t->tid=tid;t->request=malloc(n-at);if(!t->request)return 0;memcpy(t->request,data+at,n-at);t->request_size=n-at;
    if(!make_reply(g,t,data+at,n-at,fetch,fetch_user)){clear_transaction(t);return 0;}
    t->active=1;t->packets=(unsigned)((t->size+WAP_SEGMENT-1)/WAP_SEGMENT);if(!t->packets)t->packets=1;
    return send_group(t,now,0,emit,emit_user);
malformed:++g->malformed;snprintf(g->error,sizeof(g->error),"Malformed or unsupported WTP PDU");return 1;
}
int wap_gateway_receive(WapGateway *g,const uint8_t *data,size_t n,uint64_t now,WapFetch fetch,void *fetch_user,WapEmit emit,void *emit_user)
{
    size_t at=1;if(!n||!data||!emit)return 0;if(data[0])return receive_pdu(g,data,n,now,fetch,fetch_user,emit,emit_user);
    while(at<n){size_t length=data[at++];if(length&128){if(at>=n)goto malformed;length=((length&127)<<8)|data[at++];}
        if(!length||length>n-at)goto malformed;if(!receive_pdu(g,data+at,length,now,fetch,fetch_user,emit,emit_user))return 0;at+=length;}
    return 1;
malformed:++g->malformed;return 1;
}
int wap_gateway_tick(WapGateway *g,uint64_t now,WapEmit emit,void *user)
{
    unsigned i;for(i=0;i<WAP_TRANSACTIONS;++i){WapTransaction *t=&g->transactions[i];if(!t->active)continue;
        if(t->complete){if(now>=t->expires)clear_transaction(t);continue;}
        if(now>=t->due){if(t->retries++>=8){clear_transaction(t);continue;}if(!send_group(t,now,1,emit,user))return 0;}}
    return 1;
}
