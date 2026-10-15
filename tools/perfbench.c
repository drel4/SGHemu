/* Deterministic CPU/peripheral throughput probe; never writes input storage. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "machine.h"
#include "nvram.h"
#include <stdio.h>
#include <stdlib.h>
static int run(Machine *m,unsigned count)
{
    while(count--){if(!m->cellular.stage&&!battery_step(&m->battery,&m->cpu))return 0;
        if(!machine_step(m,1))return 0;}return 1;
}
int main(int argc,char **argv)
{
    Machine *m=calloc(1,sizeof(*m));LARGE_INTEGER before,after,freq;unsigned i;uint32_t hash=2166136261u;int result=1;
    if(!m||argc!=3){fprintf(stderr,"Usage: perfbench MODEL.cfg READ_ONLY.snv\n");free(m);return 2;}
    if(!machine_load(m,argv[1],NULL,NULL)||!nvram_load(m,argv[2])||!cellular_set_network(&m->cellular,&m->bus,1,"Tele2"))goto done;
    phone_net_set_mode(&m->network,2);if(!run(m,250000000))goto done;
    QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&before);if(!run(m,50000000))goto done;QueryPerformanceCounter(&after);
    for(i=0;i<16;++i)hash=(hash^m->cpu.r[i])*16777619u;
    for(i=0;i<m->width*m->height;++i)hash=(hash^m->pixels[i])*16777619u;
    printf("50 million steps: %.3f s, %.3f Msteps/s; state=%08x CPSR=%08x instructions=%llu cycles=%llu clock_ns=%llu\n",
        (double)(after.QuadPart-before.QuadPart)/freq.QuadPart,50.0*freq.QuadPart/(after.QuadPart-before.QuadPart),hash,m->cpu.cpsr,
        (unsigned long long)m->cpu.instructions,(unsigned long long)m->cpu.cycles,(unsigned long long)m->clock.nanoseconds);
    result=0;
done:if(result)fprintf(stderr,"Probe failed: %s\n",m->error);machine_destroy(m);free(m);return result;
}
