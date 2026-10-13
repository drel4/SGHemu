#include "jar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"JAR test failed at line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void put16(unsigned char *p,unsigned v){p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static void put32(unsigned char *p,unsigned v){put16(p,v);put16(p+2,v>>16);}
static unsigned crc(const unsigned char *p,unsigned n)
{
    unsigned c=~0u,k;while(n--){c^=*p++;for(k=0;k<8;++k)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}return ~c;
}
/* Two genuine ZIP entries, stored or using a raw DEFLATE stored block. */
static unsigned fixture(unsigned char *zip,const char *profile,int deflate)
{
    char manifest[512];const char *names[]={"META-INF/MANIFEST.MF","Bobby.class"};
    unsigned char cls[]={0xca,0xfe,0xba,0xbe,0,3,0,45};
    unsigned offsets[2],sizes[2],packed[2],crcs[2],pos=0,central,i;
    const unsigned char *data[2];
    snprintf(manifest,sizeof(manifest),"MIDlet-Name: Bobby C\r\n arrot\r\nMIDlet-Vendor: Test\r\nMIDlet-Version: 1.0\r\nMIDlet-1: Bobby Carrot,,Bobby\r\nMicroEdition-Profile: %s\r\nMicroEdition-Configuration: CLDC-1.0\r\n",profile);
    data[0]=(const unsigned char *)manifest;data[1]=cls;
    sizes[0]=(unsigned)strlen(manifest);sizes[1]=sizeof(cls);
    memset(zip,0,2048);
    for(i=0;i<2;++i){unsigned name=(unsigned)strlen(names[i]);offsets[i]=pos;
        packed[i]=sizes[i]+(deflate?5u:0u);crcs[i]=crc(data[i],sizes[i]);
        put32(zip+pos,0x04034b50);put16(zip+pos+4,20);put16(zip+pos+8,deflate?8:0);
        put32(zip+pos+14,crcs[i]);put32(zip+pos+18,packed[i]);put32(zip+pos+22,sizes[i]);put16(zip+pos+26,name);
        pos+=30;memcpy(zip+pos,names[i],name);pos+=name;
        if(deflate){zip[pos++]=1;put16(zip+pos,sizes[i]);put16(zip+pos+2,~sizes[i]);pos+=4;}
        memcpy(zip+pos,data[i],sizes[i]);pos+=sizes[i];}
    central=pos;
    for(i=0;i<2;++i){unsigned name=(unsigned)strlen(names[i]);put32(zip+pos,0x02014b50);
        put16(zip+pos+6,20);put16(zip+pos+10,deflate?8:0);put32(zip+pos+16,crcs[i]);
        put32(zip+pos+20,packed[i]);put32(zip+pos+24,sizes[i]);put16(zip+pos+28,name);put32(zip+pos+42,offsets[i]);
        pos+=46;memcpy(zip+pos,names[i],name);pos+=name;}
    put32(zip+pos,0x06054b50);put16(zip+pos+8,2);put16(zip+pos+10,2);
    put32(zip+pos+12,pos-central);put32(zip+pos+16,central);return pos+22;
}
static void write_zip(const unsigned char *zip,unsigned n)
{
    FILE *f=fopen("build/jar-test.jar","wb");CHECK(f!=NULL);CHECK(fwrite(zip,1,n,f)==n);CHECK(!fclose(f));
}
int main(void)
{
    unsigned char zip[2048];unsigned n;int method;JarPackage j={0};
    for(method=0;method<2;++method){n=fixture(zip,"MIDP-1.0",method);write_zip(zip,n);
        CHECK(jar_open(&j,"build/jar-test.jar"));CHECK(!strcmp(j.name,"Bobby Carrot"));
        CHECK(!strcmp(j.class_name,"Bobby"));CHECK(strstr(j.jad,"MIDlet-Jar-Size:")!=NULL);
        CHECK(j.size==n);jar_release(&j);
        zip[55]^=1;write_zip(zip,n);CHECK(!jar_open(&j,"build/jar-test.jar"));CHECK(strstr(j.error,"manifest")!=NULL);jar_release(&j);
    }
    n=fixture(zip,"MIDP-2.0",0);write_zip(zip,n);CHECK(!jar_open(&j,"build/jar-test.jar"));CHECK(strstr(j.error,"MIDP 1.0")!=NULL);jar_release(&j);
    n=fixture(zip,"MIDP-1.0",0);write_zip(zip,n-1);CHECK(!jar_open(&j,"build/jar-test.jar"));jar_release(&j);
    n=fixture(zip,"MIDP-1.0",0);put32(zip+n-6,0xfffffff0u);write_zip(zip,n);
    CHECK(!jar_open(&j,"build/jar-test.jar"));CHECK(strstr(j.error,"bounds")!=NULL);jar_release(&j);
    printf("MIDP JAR preparation: %u checks passed.\n",checks);return 0;
}
