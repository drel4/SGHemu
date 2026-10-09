#include "machine.h"
#include "nvram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv)
{
    Machine *m=calloc(1,sizeof(*m));int present;
    if(argc!=5||!m||(strcmp(argv[3],"present")&&strcmp(argv[3],"none"))) {
        fprintf(stderr,"Usage: c100_sim_test MODEL.cfg COMPLETED.snv present|none LCD.bmp\n");free(m);return 2;
    }
    present=!strcmp(argv[3],"present");
    if(!machine_load(m,argv[1],NULL,NULL)||!nvram_load(m,argv[2])||!m->sim.attached) {
        fprintf(stderr,"SIM fixture load: %s\n",m->error);machine_destroy(m);free(m);return 1;
    }
    m->sim.present=present;
    while(m->cpu.instructions<100000000)if(!machine_step(m,1))break;
    if(m->cpu.halted||m->cpu.instructions<100000000||m->sim.card.overflow||
        (present ? (m->sim.resets!=1||m->sim.card.commands<30||m->sim.card.response_size) : (m->sim.resets||m->sim.card.commands))||
        !m->lcd.enabled||!lcd_save_bmp(&m->lcd,argv[4])) {
        fprintf(stderr,"SIM boot regression: PC=%08x resets=%llu APDUs=%llu\n",m->cpu.r[15],
            (unsigned long long)m->sim.resets,(unsigned long long)m->sim.card.commands);machine_destroy(m);free(m);return 1;
    }
    printf("Guest SIM boot %s: %s; %llu APDUs, %llu DMA bytes, %llu LCD writes.\n",present?"inserted":"removed",argv[1],
        (unsigned long long)m->sim.card.commands,(unsigned long long)m->sim.dma_bytes,(unsigned long long)m->lcd.writes);
    machine_destroy(m);free(m);return 0;
}
