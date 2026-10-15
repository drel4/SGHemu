/* Optional real-ROM test. Reads a completed snapshot; never saves NVM/SIM. */
#include "machine.h"
#include "nvram.h"
#include <stdlib.h>
int main(int argc,char **argv)
{
    Machine *m;unsigned i,percent;int result=1;char path[1024];
    if(argc!=5){fprintf(stderr,"Usage: c100_charger_test MODEL.cfg READ_ONLY.snv PERCENT OUTPUT_PREFIX\n");return 2;}
    percent=(unsigned)strtoul(argv[3],NULL,10);if(percent>100)return 2;
    m=calloc(1,sizeof(*m));if(!m)return 1;
    if(!machine_load(m,argv[1],NULL,NULL)||!nvram_load(m,argv[2])||!m->battery.complete_port)goto done;
    battery_set(&m->battery,percent,1);
    for(i=0;i<400000000;){unsigned boundary=i<150000000?150000000:400000000;
        unsigned n=boundary-i;if(n>256)n=256;
        if(machine_run(m,n,1)!=n)goto done;i+=n;
        if(i==150000000){snprintf(path,sizeof(path),"%s-before.bmp",argv[4]);if(!lcd_save_bmp(&m->lcd,path))goto done;}}
    snprintf(path,sizeof(path),"%s-after.bmp",argv[4]);if(!lcd_save_bmp(&m->lcd,path))goto done;
    /* Firmware stopping the active-low charger output acknowledges completion.
       No PC, native mode variable, animation resource or pixels are patched. */
    if(m->cpu.halted||!m->intc.events||!m->dsp_mailbox.conversions||m->lcd.writes<128*128)goto done;
    if(percent==100){if(!m->battery.complete||!(m->battery.complete_port->output&(1u<<m->battery.enable_bit)))goto done;}
    else if(m->battery.complete||(m->battery.complete_port->output&(1u<<m->battery.enable_bit)))goto done;
    printf("PASS %s battery=%u done=%u charger_output=%02x wakeups=%llu LCD=%llu\n",m->name,percent,m->battery.complete,m->battery.complete_port->output,(unsigned long long)m->lp_timer.wake_events,(unsigned long long)m->lcd.writes);result=0;
done:
    if(result)fprintf(stderr,"FAIL PC=%08x done=%u output=%02x IRQ=%llu ADC=%llu LCD=%llu machine=%s CPU=%s\n",m->cpu.r[15],m->battery.complete,m->battery.complete_port?m->battery.complete_port->output:0,(unsigned long long)m->intc.events,(unsigned long long)m->dsp_mailbox.conversions,(unsigned long long)m->lcd.writes,m->error,m->cpu.error?m->cpu.error:"");
    machine_destroy(m);free(m);return result;
}
