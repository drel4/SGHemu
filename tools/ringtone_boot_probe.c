#include "machine.h"
#include "nvram.h"
#include <stdio.h>
#include <stdlib.h>
static int run(Machine *m,uint64_t ns) {
    uint64_t end=m->clock.nanoseconds+ns;
    while(m->clock.nanoseconds<end)if(!machine_step(m,1))return 0;
    return 1;
}
int main(void) {
    Machine *m=calloc(1,sizeof(*m));unsigned i;Ma5HwStats s;char path[128];int first_audible=0;
    if(!m||!machine_load(m,"models/c100/c100xedk1-bringup.cfg",NULL,NULL)||
       !nvram_load(m,"models/c100/state/6063c02a548d23096b91172121d4c4952be8e62dad0461135ab748c819af191c.snv"))return 1;
    if(!ma3_enable(&m->ma3,"MA5libs/M5_EmuHw.dll","build/peer-boot.wav",0,1000000000))return 1;
    ma3_set_block(&m->ma3,96);
    m->ma3.trace=fopen("build/peer-boot-traffic.log","w");
    if(!run(m,15000000000ULL))return 1;
    lcd_save_bmp(&m->lcd,"build/peer-boot-system.bmp");
    for(i=0;i<5;++i) {
        machine_key(m,i?1:5,1);if(!run(m,100000000))return 1;
        machine_key(m,i?1:5,0);if(!run(m,500000000))return 1;
        snprintf(path,sizeof(path),"build/peer-key-%u.bmp",i);lcd_save_bmp(&m->lcd,path);
    }
    for(i=0;i<65;++i) {
        uint64_t before=ma5_hw_stats(m->ma3.hw).nonzero;
        if(!run(m,1000000000))return 1;
        s=ma5_hw_stats(m->ma3.hw);
        if(i==0)first_audible=s.nonzero>before;
        printf("PEER second=%u ns=%llu frames=%llu nonzero=%llu peak=%u\n",i,
           (unsigned long long)m->clock.nanoseconds,(unsigned long long)s.frames,
           (unsigned long long)(s.nonzero-before),s.peak);fflush(stdout);
    }
    lcd_save_bmp(&m->lcd,"build/peer-final.bmp");
    printf("First playback second: %s; error=%s\n",first_audible?"audible":"SILENT",m->ma3.error);
    first_audible=first_audible&&!s.failed&&!*m->ma3.error;
    machine_destroy(m);free(m);return first_audible?0:1;
}
