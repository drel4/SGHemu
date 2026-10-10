#include "userfs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"userfs line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    Machine m={0};UserDirectory d;uint8_t data[4096]={0},original[4096];FILE *f;char text[256]={0};
    CHECK(bus_map(&m.bus,0x1000,sizeof(data),data,0,0));m.loaded=1;m.fs_header=0x1000;m.fs_table=0x1100;m.fs_capacity=100;m.fs_stride=28;
    CHECK(!userfs_directory(&m,&d));CHECK(strstr(d.error,"initialized")!=NULL);
    data[0]=100;data[2]=1;memcpy(data+0x100,"test.mmf",9);data[0x10c]=1;data[0x10e]=3;data[0x114]=68;
    memcpy(original,data,sizeof(data));m.bus.fault=1;m.bus.faults=7;
    CHECK(userfs_directory(&m,&d));CHECK(d.count==1&&d.slots==100&&d.listed_bytes==68);
    CHECK(!strcmp(d.files[0].name,"test.mmf")&&d.files[0].id==1&&d.files[0].type==3);
    CHECK(!memcmp(original,data,sizeof(data))&&m.bus.fault==1&&m.bus.faults==7);
    CHECK(userfs_export_directory(&d,"build/userfs-test.tsv"));f=fopen("build/userfs-test.tsv","rb");CHECK(f!=NULL);
    CHECK(fread(text,1,sizeof(text)-1,f)>0);CHECK(fclose(f)==0);CHECK(strstr(text,"test.mmf\t0001\t3\t68")!=NULL);
    data[0]=101;CHECK(!userfs_directory(&m,&d));data[0]=100;data[2]=2;CHECK(!userfs_directory(&m,&d));data[2]=1;
    memset(data+0x100,'X',12);CHECK(!userfs_directory(&m,&d));memcpy(data+0x100,"test.mmf\0\0\0\0",12);
    memcpy(data+0x11c,data+0x100,28);CHECK(!userfs_directory(&m,&d));memset(data+0x11c,0,28);
    m.fs_table=0x1ff0;CHECK(!userfs_directory(&m,&d));m.fs_table=0x1100;m.fs_header=0x100000;
    CHECK(!userfs_directory(&m,&d));CHECK(m.bus.faults==7);CHECK(!userfs_export_directory(&d,"build/userfs-test.tsv"));
    bus_destroy(&m.bus);printf("Phone filesystem: %u checks passed.\n",checks);return 0;
}
