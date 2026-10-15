/* Public WTP/WSP requests plus a small independent WBXML structure checker. */
#include "../src/wap_gateway.c"
static unsigned checks,failures,seen[80];
#define CHECK(x) do{++checks;if(!(x)){++failures;printf("FAIL line %u: %s\n",(unsigned)__LINE__,#x);}}while(0)
typedef struct {const uint8_t *body;size_t size;const char *url;unsigned calls,emitted;char next[1024];} PageFixture;
static int page_fetch(void *user,const char *url,uint8_t **body,size_t *size,char mime[96],unsigned *status)
{
    PageFixture *f=user;++f->calls;CHECK(!strcmp(url,f->url));*size=f->size;*body=malloc(*size);
    if(!*body)return 0;memcpy(*body,f->body,*size);strcpy(mime,"text/html");*status=200;return 1;
}
static int page_emit(void *user,const uint8_t *data,size_t length)
{
    PageFixture *f=user;++f->emitted;CHECK(length<=WAP_SEGMENT+3&&data[0]==0x16);return 1;
}
static int valid_utf8(const uint8_t *s,size_t n)
{
    size_t i=0;while(i<n){unsigned c=s[i++],need,j,value,min;
        if(c<128)continue;if(c>=0xc2&&c<=0xdf){need=1;value=c&31;min=128;}
        else if(c>=0xe0&&c<=0xef){need=2;value=c&15;min=2048;}
        else if(c>=0xf0&&c<=0xf4){need=3;value=c&7;min=65536;}else return 0;
        if(n-i<need)return 0;for(j=0;j<need;++j){c=s[i++];if((c&0xc0)!=0x80)return 0;value=(value<<6)|(c&63);}
        if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return 0;
    }return 1;
}
static const char *deck_string(const uint8_t *data,size_t length,size_t *at)
{
    const uint8_t *end;if(*at>=length||data[(*at)++]!=3)return NULL;
    end=memchr(data+*at,0,length-*at);if(!end)return NULL;
    {const char *value=(const char *)(data+*at);if(!valid_utf8(data+*at,(size_t)(end-(data+*at))))return NULL;
     *at=(size_t)(end-data)+1;return value;}
}
static int inspect_deck(PageFixture *f,const uint8_t *data,size_t size)
{
    static const uint8_t start[]={3,10,106,0,0x7f,0x67,0x60};size_t at=sizeof(start);unsigned depth=3;f->next[0]=0;
    if(size<sizeof(start)||memcmp(data,start,sizeof(start)))return 0;
    while(at<size&&depth){unsigned token=data[at++];
        if(token==1){--depth;continue;}if(token==0x26)continue;if(token==0x60){++depth;continue;}
        if(token==3){--at;if(!deck_string(data,size,&at))return 0;continue;}
        if(token==0xdc){const char *url,*label;unsigned item;
            if(at>=size||data[at++]!=0x4a)return 0;url=deck_string(data,size,&at);
            if(!url||at>=size||data[at++]!=1)return 0;label=deck_string(data,size,&at);
            if(!label||at>=size||data[at++]!=1)return 0;
            if(!strcmp(label,"Next")){if(strlen(url)>=sizeof(f->next))return 0;strcpy(f->next,url);}
            if(sscanf(label,"Item%u/",&item)==1&&item<80){char expected[1024];++seen[item];
                snprintf(expected,sizeof(expected),"http://example.test/dir/Item%03u/",item);CHECK(!strcmp(url,expected));}
            /* Do not repeat the complete URL as visible link text. */
            CHECK(strncmp(label,"http://",7)!=0&&strncmp(label,"https://",8)!=0);continue;
        }return 0;
    }return depth==0&&at==size;
}
static size_t page_request(uint8_t *out,unsigned tid,const char *url)
{
    size_t n=strlen(url),at=5;out[0]=0x0a;out[1]=(uint8_t)(tid>>8);out[2]=(uint8_t)tid;out[3]=2;out[4]=0x40;
    at+=put_uintvar(out+at,(uint32_t)n);memcpy(out+at,url,n);return at+n;
}
static unsigned walk_pages(PageFixture *f)
{
    WapGateway g;uint8_t request_data[1200];char url[1024];unsigned page=0;size_t n,at,body_size;
    strcpy(url,f->url);
    do{wap_gateway_init(&g);f->emitted=0;++page;
        n=page_request(request_data,page,url);CHECK(wap_gateway_receive(&g,request_data,n,0,page_fetch,f,page_emit,f));
        CHECK(f->emitted==1);CHECK(g.transactions[0].packets==1);
        {WapTransaction *t=&g.transactions[0];uint32_t headers;at=2;
         CHECK(t->size>8&&t->reply[0]==4&&t->reply[1]==0x20);
         CHECK(uintvar(t->reply,t->size,&at,&headers)&&headers<=t->size-at);
         at+=headers;body_size=t->size-at;CHECK(body_size<=WAP_PAGE_BYTES);
         CHECK(inspect_deck(f,t->reply+at,body_size));
         printf("  page %u: %zu compiled bytes\n",page,body_size);}
        strcpy(url,f->next);wap_gateway_close(&g);
    }while(*url&&page<200);
    CHECK(!*url);CHECK(f->calls==page);return page;
}
int main(int argc,char **argv)
{
    char source[30000],url[1024];size_t n=0;unsigned i,pages;PageFixture f;uint8_t deck[WAP_PAGE_BYTES];
    if(argc==2){uint8_t *body;size_t length;char mime[96];unsigned status;
        if(!wap_http_fetch(NULL,argv[1],&body,&length,mime,&status)||status!=200)return 2;
        memset(&f,0,sizeof(f));f.body=body;f.size=length;f.url=argv[1];pages=walk_pages(&f);
        printf("Live directory: %u pages, %u failures.\n",pages,failures);free(body);return failures?1:0;}
    memset(&f,0,sizeof(f));f.url="http://example.test/dir/?sort=name&keep=a%26b";
    n+=(size_t)snprintf(source+n,sizeof(source)-n,"<html><script>not visible</script><h1>Index</h1><pre><a HREF = '../'>../</a>\n");
    for(i=0;i<80;++i)n+=(size_t)snprintf(source+n,sizeof(source)-n,"<a href='Item%03u/'>Item%03u/</a> 14-Oct-2026 12:00 -\n",i,i);
    /* Big Unicode paragraph must be split on code-point boundaries, not dropped. */
    n+=(size_t)snprintf(source+n,sizeof(source)-n,"</pre><p>");
    for(i=0;i<2000;++i){memcpy(source+n,"\xe2\x98\x83",3);n+=3;}
    n+=(size_t)snprintf(source+n,sizeof(source)-n," END_OF_PAGE</p></html>");
    f.body=(uint8_t *)source;f.size=n;pages=walk_pages(&f);CHECK(pages>2);
    for(i=0;i<80;++i)CHECK(seen[i]==1);
    strcpy(url,"http://example.test/dir/?sort=name&__wapwrap_page=2&keep=a%26b#frag");
    CHECK(page_selector(url,&i)&&i==2&&!strcmp(url,f.url));CHECK(page_url(url,f.url,1)&&!strcmp(url,f.url));
    strcpy(url,"http://example.test/?__wapwrap_page=0");CHECK(!page_selector(url,&i));
    strcpy(url,"http://example.test/?__wapwrap_page=4294967296");CHECK(!page_selector(url,&i));
    strcpy(url,"http://example.test/?__wapwrap_page=2&__wapwrap_page=3");CHECK(!page_selector(url,&i));
    CHECK(html_url(url,"http://example.test/dir/sub/","../Mixed%20Case/?a=1&b=2")&&!strcmp(url,"http://example.test/dir/Mixed%20Case/?a=1&b=2"));
    CHECK(html_url(url,f.url,"?sort=size")&&!strcmp(url,"http://example.test/dir/?sort=size"));
    CHECK(html_url(url,f.url,"//other.test/Folder/")&&!strcmp(url,"http://other.test/Folder/"));
    CHECK(html_pages(deck,(uint8_t *)source,n,1,f.url,999,&i)==0&&i==pages);
    CHECK(html_pages(deck,(const uint8_t *)"<a href='x?a=1&amp;b=2'>A &amp; B</a>",strlen("<a href='x?a=1&amp;b=2'>A &amp; B</a>"),1,f.url,1,&i)>0&&i==1);
    printf("WAP HTML pagination: %u checks, %u failures.\n",checks,failures);return failures?1:0;
}
