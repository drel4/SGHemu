/* Bounded HTML/text -> independently fetchable WML decks. Inspired by
   WAPWarp's html2wml.py pagination; all conversion here is plain C. */
#define WAP_PAGE_BYTES 1200u
#define WAP_PAGE_PARAMETER "__wapwrap_page"
typedef struct {
    uint8_t *out;size_t n,used,budget;unsigned page,wanted;
} HtmlPages;
static int page_selector(char url[1024],unsigned *page)
{
    char clean[1024],*query,*fragment,*part;size_t n;int first=1,seen=0;
    *page=1;fragment=strchr(url,'#');if(fragment)*fragment=0;
    query=strchr(url,'?');if(!query)return 1;
    n=(size_t)(query-url);memcpy(clean,url,n);part=query+1;
    while(*part){char *end=strchr(part,'&');size_t length=end?(size_t)(end-part):strlen(part);
        size_t prefix=sizeof(WAP_PAGE_PARAMETER)-1;
        if(length>=prefix&&!memcmp(part,WAP_PAGE_PARAMETER,prefix)&&(length==prefix||part[prefix]=='=')){
            size_t i;unsigned value=0;if(seen++||length<=prefix+1)return 0;
            for(i=prefix+1;i<length;++i){unsigned digit=(unsigned char)part[i]-'0';
                if(digit>9||value>(UINT32_MAX-digit)/10)return 0;value=value*10+digit;}
            if(!value)return 0;*page=value;
        }else if(length){clean[n++]=first?'?':'&';first=0;memcpy(clean+n,part,length);n+=length;}
        if(!end)break;part=end+1;
    }
    clean[n]=0;strcpy(url,clean);return 1;
}
static int page_url(char out[1024],const char *base,unsigned page)
{
    int n=page==1?snprintf(out,1024,"%s",base):snprintf(out,1024,"%s%c%s=%u",base,strchr(base,'?')?'&':'?',WAP_PAGE_PARAMETER,page);
    return n>=0&&n<1024;
}
static int html_block(HtmlPages *p,const uint8_t *data,size_t size)
{
    if(size>p->budget)return 0;
    if(p->used+size>p->budget){++p->page;p->used=0;}
    if(p->page==p->wanted&&!append(p->out,&p->n,WAP_PAGE_BYTES,data,size))return 0;
    p->used+=size;return 1;
}
static size_t utf8_cut(const char *s,size_t length,size_t limit)
{
    size_t n=length<limit?length:limit;
    if(n<length)while(n&&((unsigned char)s[n]&0xc0)==0x80)--n;
    return n;
}
static int html_text(HtmlPages *p,const char *text,size_t length)
{
    uint8_t block[260];size_t at=0,limit=p->budget>2?p->budget-2:0;
    if(limit>256)limit=256;
    while(at<length){size_t n=utf8_cut(text+at,length-at,limit);if(!n)return 0;
        block[0]=3;memcpy(block+1,text+at,n);block[n+1]=0;
        if(!html_block(p,block,n+2))return 0;at+=n;
    }return 1;
}
static int html_link(HtmlPages *p,const char *url,const char *label,size_t length)
{
    uint8_t block[WAP_PAGE_BYTES];size_t at=0,url_size=strlen(url),limit;
    if(url_size+9>=p->budget)return 0;limit=p->budget-url_size-9;
    do{size_t n=0,take=utf8_cut(label+at,length-at,limit);if(length>at&&!take)return 0;
        if(!byte(block,&n,sizeof(block),0xdc)||!byte(block,&n,sizeof(block),0x4a)||
           !inline_text(block,&n,sizeof(block),url,url_size)||!byte(block,&n,sizeof(block),1)||
           !inline_text(block,&n,sizeof(block),label+at,take)||!byte(block,&n,sizeof(block),1)||
           !byte(block,&n,sizeof(block),0x26)||!html_block(p,block,n))return 0;
        at+=take;
    }while(at<length);return 1;
}
static int tag_is(const uint8_t *text,size_t length,const char *name)
{
    size_t i,n=strlen(name);if(length<n)return 0;
    for(i=0;i<n;++i)if(tolower(text[i])!=name[i])return 0;
    return length==n||isspace(text[n])||text[n]=='/';
}
static size_t tag_end(const uint8_t *text,size_t length,size_t at)
{
    unsigned quote=0;while(at<length){unsigned c=text[at];
        if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='>')break;++at;
    }return at;
}
static unsigned entity(const uint8_t *s,size_t n,size_t *used)
{
    size_t end=1,i;unsigned value=0,radix=10;*used=1;
    while(end<n&&end<16&&s[end]!=';'&&!isspace(s[end]))++end;
    if(end>=n||s[end]!=';')return '&';*used=end+1;
    if(end==4&&!memcmp(s,"&amp",4))return '&';if(end==3&&!memcmp(s,"&lt",3))return '<';
    if(end==3&&!memcmp(s,"&gt",3))return '>';if(end==5&&!memcmp(s,"&quot",5))return '"';
    if(end==5&&!memcmp(s,"&apos",5))return '\'';if(end==5&&!memcmp(s,"&nbsp",5))return ' ';
    if(end<3||s[1]!='#')return ' ';i=2;if(s[i]=='x'||s[i]=='X'){radix=16;++i;}
    if(i==end)return ' ';
    for(;i<end;++i){unsigned d=s[i]>='0'&&s[i]<='9'?s[i]-'0':s[i]>='a'&&s[i]<='f'?s[i]-'a'+10:s[i]>='A'&&s[i]<='F'?s[i]-'A'+10:99;
        if(d>=radix||value>(0x10ffffu-d)/radix)return ' ';value=value*radix+d;}
    return !value||(value>=0xd800&&value<=0xdfff)?' ':value;
}
static size_t encode_utf8(char *out,unsigned value)
{
    if(value<128){out[0]=(char)value;return 1;}
    if(value<2048){out[0]=(char)(0xc0|(value>>6));out[1]=(char)(0x80|(value&63));return 2;}
    if(value<65536){out[0]=(char)(0xe0|(value>>12));out[1]=(char)(0x80|((value>>6)&63));out[2]=(char)(0x80|(value&63));return 3;}
    out[0]=(char)(0xf0|(value>>18));out[1]=(char)(0x80|((value>>12)&63));out[2]=(char)(0x80|((value>>6)&63));out[3]=(char)(0x80|(value&63));return 4;
}
static size_t html_label(char *out,size_t cap,const uint8_t *text,size_t length,int tags)
{
    size_t i=0,n=0;int space=0;
    while(i<length){unsigned c=text[i++];char encoded[4];size_t bytes=1;
        if(tags&&c=='<'){size_t end=tag_end(text,length,i);i=end<length?end+1:length;space=1;continue;}
        if(c=='&'){size_t consumed;c=entity(text+i-1,length-i+1,&consumed);i+=consumed-1;bytes=encode_utf8(encoded,c);}
        else encoded[0]=(char)c;
        if(c<=32){space=1;continue;}
        if(space&&n){if(n+1>=cap)break;out[n++]=' ';}space=0;
        if(n+bytes>=cap)break;memcpy(out+n,encoded,bytes);n+=bytes;
    }
    /* Trim only an incomplete final UTF-8 character, not a complete one. */
    if(n){size_t lead=n-1,need;unsigned first;
        while(lead&&((unsigned char)out[lead]&0xc0)==0x80)--lead;first=(unsigned char)out[lead];
        need=first<128?1:first>=0xf0?4:first>=0xe0?3:first>=0xc0?2:1;
        if(n-lead<need)n=lead;
    }
    out[n]=0;return n;
}
static int html_href(char out[1024],const uint8_t *tag,size_t length)
{
    size_t i=0;while(i<length){size_t name,begin,end;unsigned quote=0;
        while(i<length&&(isspace(tag[i])||tag[i]=='/'))++i;name=i;
        while(i<length&&!isspace(tag[i])&&tag[i]!='=')++i;
        end=i;while(i<length&&isspace(tag[i]))++i;
        if(i==length||tag[i]!='=')continue;++i;while(i<length&&isspace(tag[i]))++i;
        if(i<length&&(tag[i]=='\''||tag[i]=='"'))quote=tag[i++];begin=i;
        while(i<length&&(quote?tag[i]!=quote:!isspace(tag[i])))++i;
        if(end-name==4&&tolower(tag[name])=='h'&&tolower(tag[name+1])=='r'&&tolower(tag[name+2])=='e'&&tolower(tag[name+3])=='f'){
            if(i-begin>=1024)return -1;(void)html_label(out,1024,tag+begin,i-begin,0);return 1;
        }if(quote&&i<length)++i;
    }return 0;
}
static int html_url(char out[1024],const char *base,const char *raw)
{
    const char *scheme=strstr(base,"://"),*path;size_t prefix;char joined[2048];int n;
    if(!strncmp(raw,"http://",7)||!strncmp(raw,"https://",8))n=snprintf(joined,sizeof(joined),"%s",raw);
    else {if(!scheme||strchr(raw,':'))return 0;path=strchr(scheme+3,'/');
        if(raw[0]=='/'&&raw[1]=='/')n=snprintf(joined,sizeof(joined),"%.*s%s",(int)(scheme-base+1),base,raw);
        else {
            const char *q=strchr(base,'?'),*end=q?q:base+strlen(base),*last=end;
            if(raw[0]=='/')prefix=path?(size_t)(path-base):(size_t)(end-base);
            else if(raw[0]=='?'||raw[0]=='#')prefix=(size_t)(end-base);
            else {while(last>base&&last[-1]!='/')--last;prefix=path?(size_t)(last-base):(size_t)(end-base);}
            n=snprintf(joined,sizeof(joined),"%.*s%s%s",(int)prefix,base,!path&&raw[0]!='/'&&raw[0]!='?'&&raw[0]!='#'?"/":"",raw);
        }
    }
    if(n<0||(size_t)n>=sizeof(joined))return 0;
    /* Resolve dot segments without altering escaped bytes or query data. */
    {const char *start=strstr(joined,"://"),*s,*suffix;size_t used;unsigned marks[512],depth=0;
        if(!start)return 0;s=strchr(start+3,'/');suffix=strpbrk(start+3,"?#");
        if(!s||(suffix&&s>suffix)){if(strlen(joined)>=1024)return 0;strcpy(out,joined);return 1;}
        used=(size_t)(s-joined);memcpy(out,joined,used);
        while(*s&&*s!='?'&&*s!='#'){const char *part;size_t len;int slash;
            ++s;part=s;while(*s&&*s!='/'&&*s!='?'&&*s!='#')++s;len=(size_t)(s-part);slash=*s=='/';
            if(len==1&&part[0]=='.'){if(!slash){if(used+1>=1024)return 0;out[used++]='/';}continue;}
            if(len==2&&part[0]=='.'&&part[1]=='.'){if(depth)used=marks[--depth];if(!slash){if(used+1>=1024)return 0;out[used++]='/';}continue;}
            if(used+len+1>=1024||depth==512)return 0;marks[depth++]=(unsigned)used;out[used++]='/';memcpy(out+used,part,len);used+=len;
        }
        if(used+strlen(s)>=1024)return 0;strcpy(out+used,s);
    }return 1;
}
static size_t html_pages(uint8_t *out,const uint8_t *text,size_t length,int html,const char *base,unsigned wanted,unsigned *pages)
{
    static const uint8_t start[]={3,10,106,0,0x7f,0x67,0x60};HtmlPages p;size_t i=0,used=0,reserve;
    char chunk[260],nav[1024];int space=0,skip=0;
    if(!page_url(nav,base,UINT32_MAX))return 0;
    reserve=2*(strlen(nav)+16)+64;if(reserve+sizeof(start)+5>=WAP_PAGE_BYTES)return 0;
    memset(&p,0,sizeof(p));p.out=out;p.n=sizeof(start);p.budget=WAP_PAGE_BYTES-reserve-sizeof(start)-5;p.page=1;p.wanted=wanted;
    memcpy(out,start,sizeof(start));
    while(i<length){unsigned c=text[i++];
        if(html&&c=='<'){size_t begin=i,end=tag_end(text,length,i);i=end<length?end+1:length;
            if(tag_is(text+begin,end-begin,"script")||tag_is(text+begin,end-begin,"style")){skip=1;continue;}
            if(tag_is(text+begin,end-begin,"/script")||tag_is(text+begin,end-begin,"/style")){skip=0;continue;}if(skip)continue;
            if(tag_is(text+begin,end-begin,"a")){char raw[1024],url[1024];int href=html_href(raw,text+begin,end-begin);
                if(href<0)return 0;if(href&&html_url(url,base,raw)){size_t label_start=i,label_end=i,after=i;char *label;
                    while(after<length){if(text[after]=='<'){size_t stop=tag_end(text,length,after+1);
                        if(tag_is(text+after+1,stop-after-1,"/a")){label_end=after;after=stop<length?stop+1:length;break;}after=stop<length?stop+1:length;
                    }else ++after;}
                    if(after==length&&label_end==label_start)label_end=length;
                    label=malloc(label_end-label_start+1);if(!label)return 0;
                    {size_t label_size=html_label(label,label_end-label_start+1,text+label_start,label_end-label_start,1);
                     if(used&&!html_text(&p,chunk,used)){free(label);return 0;}used=0;
                     if(!html_link(&p,url,label_size?label:raw,label_size?label_size:strlen(raw))){free(label);return 0;}}
                    free(label);i=after;space=0;continue;
                }
            }
            if(tag_is(text+begin,end-begin,"br")||tag_is(text+begin,end-begin,"hr")||tag_is(text+begin,end-begin,"/p")||
               tag_is(text+begin,end-begin,"/div")||tag_is(text+begin,end-begin,"/h1")||tag_is(text+begin,end-begin,"/li")){
                static const uint8_t br=0x26;if(used&&!html_text(&p,chunk,used))return 0;used=0;if(!html_block(&p,&br,1))return 0;
            }space=1;continue;
        }
        if(skip)continue;
        if(html&&c=='&'){size_t consumed;char encoded[4];size_t bytes;
            c=entity(text+i-1,length-i+1,&consumed);i+=consumed-1;bytes=encode_utf8(encoded,c);
            if(c>32){if(used+bytes+1>256){if(!html_text(&p,chunk,used))return 0;used=0;}
                if(space&&used)chunk[used++]=' ';space=0;memcpy(chunk+used,encoded,bytes);used+=bytes;continue;}}
        if(c<=32){space=1;continue;}
        if(used>=252&&(c&0xc0)!=0x80){if(!html_text(&p,chunk,used))return 0;used=0;}
        if(used+2>=sizeof(chunk))return 0;
        if(space&&used)chunk[used++]=' ';space=0;chunk[used++]=(char)c;
    }
    if(used&&!html_text(&p,chunk,used))return 0;*pages=p.page;if(wanted>p.page)return 0;
    if(p.page>1){size_t n=p.n;char label[64];uint8_t br=0x26;
        snprintf(label,sizeof(label),"Page %u/%u",wanted,p.page);
        /* Navigation is a separate paragraph, like WAPWarp's decks. */
        if(!byte(out,&n,WAP_PAGE_BYTES,1)||!byte(out,&n,WAP_PAGE_BYTES,0x60)||
           !inline_text(out,&n,WAP_PAGE_BYTES,label,strlen(label))||!byte(out,&n,WAP_PAGE_BYTES,br))return 0;
        p.n=n;p.used=0;p.budget=reserve;p.page=wanted;
        if(wanted>1){if(!page_url(nav,base,wanted-1)||!html_link(&p,nav,"Prev",4))return 0;}
        if(wanted<*pages){if(!page_url(nav,base,wanted+1)||!html_link(&p,nav,"Next",4))return 0;}
    }
    if(!byte(out,&p.n,WAP_PAGE_BYTES,1)||!byte(out,&p.n,WAP_PAGE_BYTES,1)||!byte(out,&p.n,WAP_PAGE_BYTES,1))return 0;
    return p.n;
}
