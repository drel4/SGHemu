/* CPU-only ARM/Thumb mixes; compares the frozen reference and current decoder. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "arm7.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void arm7_reference_reset(Arm7 *,Bus *,uint32_t);
int arm7_reference_step(Arm7 *);
static int benchmark(int reference,int thumb,uint64_t *clocks,uint32_t *hash)
{
    Bus *b=calloc(1,sizeof(*b));Arm7 c;uint8_t *ram=calloc(65536,1);unsigned i;LARGE_INTEGER before,after,freq;
    const uint16_t thumb_program[]={0x6801,0x3101,0x6001,0x4209,0x2207,0xe7f9};
    const uint32_t arm_program[]={0xe5901000,0xe2911001,0xe5801000,0xe1110001,0xe3a02007,0xeafffff9};
    if(!b||!ram){free(b);free(ram);return 0;}if(!bus_map(b,0,65536,ram,0,1)){free(b);free(ram);return 0;}
    if(thumb)memcpy(ram,thumb_program,sizeof(thumb_program));else memcpy(ram,arm_program,sizeof(arm_program));
    arm7_reset(&c,b,0);if(thumb)c.cpsr|=ARM_T;c.r[0]=0x100;
    if(!reference){c.cache=arm7_cache_create();if(!c.cache){bus_destroy(b);free(b);return 0;}}
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&before);
    for(i=0;i<50000000;++i)if(!(reference?arm7_reference_step(&c):arm7_step(&c)))break;
    QueryPerformanceCounter(&after);*hash=2166136261u;
    for(i=0;i<16;++i)*hash=(*hash^c.r[i])*16777619u;
    *hash=(*hash^bus_read(b,0x100,4))*16777619u;*clocks=c.clock_cycles;
    printf("%s %s: %.3f MIPS, %.3f s; state=%08x core_cycles=%llu instructions=%llu\n",thumb?"Thumb":"ARM",reference?"reference":"optimized",
        50.0*freq.QuadPart/(after.QuadPart-before.QuadPart),(double)(after.QuadPart-before.QuadPart)/freq.QuadPart,
        *hash,(unsigned long long)*clocks,(unsigned long long)c.instructions);
    i=c.instructions==50000000&&!b->fault;arm7_cache_destroy(c.cache);bus_destroy(b);free(b);return i!=0;
}
int main(void)
{
    uint64_t ca,cb;uint32_t ha,hb;
    if(!benchmark(1,1,&ca,&ha)||!benchmark(0,1,&cb,&hb)||ca!=cb||ha!=hb)return 1;
    if(!benchmark(1,0,&ca,&ha)||!benchmark(0,0,&cb,&hb)||ca!=cb||ha!=hb)return 1;
    puts("CPU benchmark states and cycle counts match.");return 0;
}
