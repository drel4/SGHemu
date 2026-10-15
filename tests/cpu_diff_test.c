/* Optimization regression oracle; CPU architecture programs remain cpu_test.c. */
#include "arm7.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void arm7_reference_reset(Arm7 *,Bus *,uint32_t);
int arm7_reference_step(Arm7 *);
static uint32_t random_state=0x739ca51u;
static uint32_t next_random(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static int equal(const Arm7 *a,const Arm7 *b)
{
    return !memcmp(a->r,b->r,sizeof(a->r))&&a->cpsr==b->cpsr&&
        !memcmp(a->bank,b->bank,sizeof(a->bank))&&!memcmp(a->spsr,b->spsr,sizeof(a->spsr))&&
        !memcmp(a->user_hi,b->user_hi,sizeof(a->user_hi))&&!memcmp(a->fiq_hi,b->fiq_hi,sizeof(a->fiq_hi))&&
        a->instructions==b->instructions&&a->cycles==b->cycles&&a->clock_cycles==b->clock_cycles&&
        a->last_pc==b->last_pc&&a->last_opcode==b->last_opcode&&a->halted==b->halted&&
        ((!a->error&&!b->error)||(a->error&&b->error&&!strcmp(a->error,b->error)));
}
static int compare(uint32_t op,unsigned seed,int thumb,Bus *a,Bus *b,const uint8_t *image,Arm7Cache *cache)
{
    Arm7 x,y;unsigned i;int sx,sy;uint32_t flags[]={0,ARM_N|ARM_C,ARM_Z|ARM_V};
    memcpy(a->regions[0].data,image,65536);memcpy(b->regions[0].data,image,65536);
    a->fault=b->fault=0;a->faults=b->faults=0;
    a->last_address=b->last_address=a->last_value=b->last_value=0;
    a->last_width=b->last_width=0;a->last_write=b->last_write=0;
    arm7_reset(&x,a,0x100);arm7_reference_reset(&y,b,0x100);
    x.cpsr=(seed==1?0x11u:seed==2?0x1fu:0x13u)|flags[seed]|(thumb?ARM_T:0);
    for(i=0;i<15;++i)x.r[i]=seed==1?next_random():0x2000+i*0x100+(seed?1u:0u);
    x.r[13]=seed==2?0xfffcu:0x8000;x.r[14]=0x301;
    for(i=0;i<6;++i){x.bank[i][0]=0x8000+i*0x100;x.bank[i][1]=0x201;x.spsr[i]=0x1f;}
    y=x;y.bus=b;
    x.cache=cache;y.cache=NULL;
    bus_write(a,0x100,thumb?2:4,op);bus_write(b,0x100,thumb?2:4,op);
    sx=arm7_step(&x);sy=arm7_reference_step(&y);
    if(sx!=sy||!equal(&x,&y)||a->fault!=b->fault||a->faults!=b->faults||
        a->last_address!=b->last_address||a->last_value!=b->last_value||a->last_width!=b->last_width||a->last_write!=b->last_write||
        memcmp(a->regions[0].data,b->regions[0].data,65536)){
        fprintf(stderr,"Differential mismatch: %s opcode=%08x seed=%u cycles=%llu/%llu PC=%08x/%08x\n",
            thumb?"Thumb":"ARM",op,seed,(unsigned long long)x.clock_cycles,(unsigned long long)y.clock_cycles,x.r[15],y.r[15]);return 0;
    }return 1;
}
int main(void)
{
    Bus *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));uint8_t *image=malloc(65536),*ma=malloc(65536),*mb=malloc(65536);
    Arm7Cache *cache=arm7_cache_create();unsigned i,seed,cases=0;int result=1;
    if(!a||!b||!image||!ma||!mb||!cache){free(a);free(b);free(image);free(ma);free(mb);arm7_cache_destroy(cache);return 2;}
    if(!bus_map(a,0,65536,ma,0,1)||!bus_map(b,0,65536,mb,0,1))return 2;
    for(i=0;i<65536;++i)image[i]=(uint8_t)(i*37u+(i>>8));
    for(seed=0;seed<3;++seed)for(i=0;i<65536;++i){
        if(!compare(i,seed,1,a,b,image,NULL)||!compare(i,seed,1,a,b,image,cache)||!compare(i,seed,1,a,b,image,cache))goto done;cases+=3;}
    for(i=0;i<50000;++i){uint32_t op=next_random();
        if(!compare(op,i%3,0,a,b,image,NULL)||!compare(op,i%3,0,a,b,image,cache)||!compare(op,i%3,0,a,b,image,cache))goto done;cases+=3;}
    printf("CPU differential: %u cases passed (uncached/cached/warm, all Thumb encodings x3 states, randomized ARM).\n",cases);result=0;
done:arm7_cache_destroy(cache);bus_destroy(a);bus_destroy(b);free(a);free(b);free(image);return result;
}
