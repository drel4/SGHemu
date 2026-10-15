#include "bus.h"
#include <stdlib.h>
static unsigned checks,reads,writes;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Data bus line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint32_t device_read(void *p,uint32_t off,unsigned width)
{(void)p;++reads;return 0x12340000u+off+width;}
static void device_write(void *p,uint32_t off,unsigned width,uint32_t value)
{(void)p;(void)off;(void)width;(void)value;++writes;}
int main(void)
{
    Bus *b=calloc(1,sizeof(*b));uint8_t *ram=calloc(1,0x20002),*rom=calloc(1,0x10000);
    unsigned i;uint32_t rng=0x805c100,expected;FILE *log;char line[256];
    CHECK(b&&ram&&rom);CHECK(bus_map(b,0x100000,0x20002,ram,0,1));
    CHECK(bus_map(b,0x200000,0x10000,ram,0,0)); /* alias */
    CHECK(bus_map(b,0x300000,0x10000,rom,1,1));
    CHECK(bus_device(b,0x400000,0x10000,device_read,device_write,NULL));
    for(i=0;i<20000;++i){unsigned width=1u<<(i%3);uint32_t a;
        rng=rng*1664525u+1013904223u;a=0x100000+(rng%(0x20003-width));
        bus_write_data(b,a,width,rng);expected=width==4?rng:rng&((1u<<(width*8))-1);
        CHECK(bus_read(b,a,width)==expected);CHECK(bus_read_data(b,a,width)==expected);
    }
    bus_write_data(b,0x200040,4,0x87654321);CHECK(bus_read_data(b,0x100040,4)==0x87654321);
    bus_write_data(b,0x300000,1,1);CHECK(b->fault&&rom[0]==0&&b->last_write);b->fault=0;
    CHECK(bus_read_data(b,0x400004,2)==0x12340006&&reads==1);
    bus_write_data(b,0x400004,2,7);CHECK(writes==1);
    b->regions[3].data=ram;b->regions[3].fast=1;
    CHECK(bus_read_data(b,0x400040,4)==0x87654321&&reads==1);
    bus_write_data(b,0x400040,4,1);CHECK(writes==2); /* fast flash read != RAM write */
    CHECK(bus_read_data(b,0x120001,2)==0&&b->fault);b->fault=0;
    (void)bus_read_data(b,UINT32_MAX,4);CHECK(b->fault);b->fault=0;
    (void)bus_read_data(b,0x100000,3);CHECK(b->fault);b->fault=0;
    log=tmpfile();CHECK(log);b->access_log=log;CHECK(bus_watch_parse(b,"0x100000,0x100004"));
    bus_write_data(b,0x100000,4,9);CHECK(bus_read_data(b,0x100000,4)==9);
    rewind(log);CHECK(fgets(line,sizeof(line),log)&&strstr(line,"W32"));
    CHECK(fgets(line,sizeof(line),log)&&strstr(line,"R32"));b->access_log=NULL;fclose(log);
    bus_destroy(b);free(b);printf("CPU data bus: %u checks passed.\n",checks);return 0;
}
