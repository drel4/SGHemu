#include "jar.h"
#include "file.h"
#include "../third_party/puff/puff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MANIFEST_LIMIT 65536u
static unsigned u16(const uint8_t *p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p+2) << 16); }
static int bad(JarPackage *j, const char *why)
{
    snprintf(j->error,sizeof(j->error),"Java package: %s",why);return 0;
}
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc=0xffffffffu;unsigned k;
    while(n--){crc^=*p++;for(k=0;k<8;++k)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
    return ~crc;
}
/* Central-directory sizes are authoritative, including ZIP data descriptors. */
static int entry(JarPackage *j,const char *name,uint8_t **out,size_t *length)
{
    size_t e,pos,end,stop,nlen=strlen(name);unsigned count,i;
    if(j->size<22)return bad(j,"truncated ZIP archive");
    stop=j->size>65557?j->size-65557:0;e=j->size-22;
    for(;;){if(u32(j->archive+e)==0x06054b50u&&e+22+u16(j->archive+e+20)==j->size)break;
        if(e==stop)return bad(j,"ZIP end directory not found");--e;}
    if(u16(j->archive+e+4)||u16(j->archive+e+6)||u16(j->archive+e+8)!=u16(j->archive+e+10))
        return bad(j,"split ZIP archives are unsupported");
    pos=u32(j->archive+e+16);end=u32(j->archive+e+12);
    if(pos>e||end>e-pos)return bad(j,"invalid ZIP directory bounds");end+=pos;
    count=u16(j->archive+e+10);
    for(i=0;i<count;++i){const uint8_t *c;size_t next,local,data,packed,size;unsigned method;
        if(pos>end||end-pos<46)return bad(j,"truncated ZIP directory entry");c=j->archive+pos;
        if(u32(c)!=0x02014b50u)return bad(j,"invalid ZIP directory entry");
        next=46u+u16(c+28)+u16(c+30)+u16(c+32);
        if(next>end-pos)return bad(j,"invalid ZIP entry length");
        if(u16(c+28)!=nlen||memcmp(c+46,name,nlen)){pos+=next;continue;}
        if(u16(c+8)&1u)return bad(j,"encrypted ZIP entries are unsupported");
        local=u32(c+42);packed=u32(c+20);size=u32(c+24);method=u16(c+10);
        if(local>j->size||j->size-local<30||u32(j->archive+local)!=0x04034b50u)
            return bad(j,"invalid ZIP local header");
        data=local+30u+u16(j->archive+local+26)+u16(j->archive+local+28);
        if(data>j->size||packed>j->size-data)return bad(j,"truncated ZIP contents");
        if(!out)return 1;
        if(!size||size>MANIFEST_LIMIT)return bad(j,"invalid manifest size");
        *out=malloc(size+1);if(!*out)return bad(j,"out of memory");
        if(method==0&&packed==size)memcpy(*out,j->archive+data,size);
        else if(method==8){unsigned long src=(unsigned long)packed,dst=(unsigned long)size;
            if(puff(*out,&dst,j->archive+data,&src)||dst!=size||src!=packed)goto corrupt;
        }else goto corrupt;
        if(crc32(*out,size)!=u32(c+16))goto corrupt;
        (*out)[size]=0;*length=size;return 1;
corrupt:free(*out);*out=NULL;return bad(j,"corrupt or unsupported compressed manifest");
    }
    return bad(j,"required archive entry is missing");
}
static int attribute(const char *m,const char *key,char *out,size_t capacity)
{
    size_t keylen=strlen(key);const char *p=m;
    while(*p){const char *end=strchr(p,'\n');size_t n=end?(size_t)(end-p):strlen(p);
        if(n>keylen+2&&!memcmp(p,key,keylen)&&p[keylen]==':'&&p[keylen+1]==' '){
            const char *v=p+keylen+2;size_t len=n-keylen-2;
            if(len&&v[len-1]=='\r')--len;
            if(!len||len>=capacity)return 0;memcpy(out,v,len);out[len]=0;return 1;}
        if(!end)break;p=end+1;
    }return 0;
}
int jar_open(JarPackage *j,const char *path)
{
    uint8_t *raw=NULL;char *manifest=NULL,*first,*comma,*last;size_t length=0,i,k=0;
    char profile[64],config[64],midlet[768],class_file[272];int ok=0;
    memset(j,0,sizeof(*j));
    if(!file_read(path,&j->archive,&j->size))return bad(j,"cannot read JAR file");
    if(j->size>16u*1024u*1024u){bad(j,"archive exceeds 16 MiB");goto done;}
    if(!entry(j,"META-INF/MANIFEST.MF",&raw,&length))goto done;
    /* Unfold manifest continuation lines before parsing attributes. */
    manifest=malloc(length+1);if(!manifest){bad(j,"out of memory");goto done;}
    for(i=0;i<length;++i){if(!raw[i]){bad(j,"NUL byte in manifest");goto done;}
        if(raw[i]=='\r'&&i+2<length&&raw[i+1]=='\n'&&raw[i+2]==' '){i+=2;continue;}
        if(raw[i]=='\n'&&i+1<length&&raw[i+1]==' '){++i;continue;}
        manifest[k++]=(char)raw[i];}manifest[k]=0;
    /* Only the main manifest section defines suite attributes. */
    {char *section=strstr(manifest,"\r\n\r\n"),*lf=strstr(manifest,"\n\n");
        if(lf&&(!section||lf<section))section=lf;if(section)*section=0;}
    k=strlen(manifest);
    if(!attribute(manifest,"MicroEdition-Profile",profile,sizeof(profile))||strcmp(profile,"MIDP-1.0")){
        bad(j,"this installer targets MIDP 1.0, not MIDP 2.x");goto done;}
    if(!attribute(manifest,"MicroEdition-Configuration",config,sizeof(config))||strcmp(config,"CLDC-1.0")){
        bad(j,"this handset requires CLDC 1.0");goto done;}
    if(!attribute(manifest,"MIDlet-Name",j->name,sizeof(j->name))||
       !attribute(manifest,"MIDlet-Vendor",j->vendor,sizeof(j->vendor))||
       !attribute(manifest,"MIDlet-Version",j->version,sizeof(j->version))||
       !attribute(manifest,"MIDlet-1",midlet,sizeof(midlet))){bad(j,"missing MIDlet identity attributes");goto done;}
    comma=strchr(midlet,',');last=comma?strchr(comma+1,','):NULL;
    if(!last){bad(j,"invalid MIDlet-1 entry");goto done;}
    first=last+1;while(*first==' ')++first;last=first+strlen(first);
    while(last>first&&last[-1]==' ')--last;
    if(last==first||(size_t)(last-first)>=sizeof(j->class_name)){bad(j,"invalid MIDlet class name");goto done;}
    memcpy(j->class_name,first,(size_t)(last-first));j->class_name[last-first]=0;
    snprintf(class_file,sizeof(class_file),"%s.class",j->class_name);
    for(i=0;class_file[i];++i)if(class_file[i]=='.'&&i<strlen(j->class_name))class_file[i]='/';
    if(!entry(j,class_file,NULL,NULL))goto done;
    j->jad=malloc(k+128);if(!j->jad){bad(j,"out of memory");goto done;}
    /* The guest fetches bytes from its initialized RAM path, not the network. */
    {const char *p=manifest;size_t n=0;
        /* Never retain stale or duplicate download attributes from the JAR. */
        while(*p){const char *end=strchr(p,'\n');size_t len=end?(size_t)(end-p)+1:strlen(p);
            if(strncmp(p,"MIDlet-Jar-URL:",15)&&strncmp(p,"MIDlet-Jar-Size:",16)){
                memcpy(j->jad+n,p,len);n+=len;}
            p+=len;}
        snprintf(j->jad+n,k+128-n,"\r\nMIDlet-Jar-URL: http://sghemu.invalid/package.jar\r\nMIDlet-Jar-Size: %lu\r\n",(unsigned long)j->size);}
    ok=1;
done:free(raw);free(manifest);if(!ok){free(j->archive);j->archive=NULL;free(j->jad);j->jad=NULL;}return ok;
}
void jar_release(JarPackage *j){free(j->archive);free(j->jad);memset(j,0,sizeof(*j));}
