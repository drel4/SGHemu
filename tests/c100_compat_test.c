/* Optional real-ROM integration test. All writes use a NEW, explicitly supplied
   private state directory; never opens the user's model-local NVM. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "machine.h"
#include "nvram.h"
#include "userfs.h"
#include <stdlib.h>
#include <string.h>
static Machine *m;
static NvramStore storage;
static const char *config,*state_dir,*prefix;
static unsigned resets;
static int load(void)
{
    return machine_load(m,config,NULL,NULL)&&nvram_store_open(&storage,m,state_dir)&&
        cellular_set_network(&m->cellular,&m->bus,1,"Tele2");
}
static int step(void)
{
    int provision=nvram_store_step(&storage,m);
    if(!provision)return 0;
    if(provision==2){int defaults=storage.provision&&storage.stage==3;
        nvram_store_release(&storage);if(!load())return 0;
        storage.defaults_boot=defaults;++resets;return 1;}
    return battery_step(&m->battery,&m->cpu)&&machine_step(m,1);
}
static int run(unsigned count)
{
    while(count--)if(!step())return 0;return 1;
}
static int picture(const char *name)
{
    char path[1024];snprintf(path,sizeof(path),"%s-%s.bmp",prefix,name);
    return lcd_save_bmp(&m->lcd,path);
}
static int key(unsigned index)
{
    machine_key(m,index,1);if(!run(2000000))return 0;
    machine_key(m,index,0);return run(20000000);
}
int main(int argc,char **argv)
{
    unsigned i;uint64_t before;UserDirectory directory;
    static const uint16_t text[]={'C','o','m','p','a','t',' ','S','M','S'};
    static const uint8_t timestamp[]={0x62,0x01,0x51,0x21,0,0,0};
    int result=1;
    if(argc!=4){fprintf(stderr,"Usage: c100_compat_test MODEL.cfg NEW_STATE_DIR OUTPUT_PREFIX\n");return 2;}
    config=argv[1];state_dir=argv[2];prefix=argv[3];
    if(!CreateDirectoryA(state_dir,NULL)){fprintf(stderr,"State directory must be new; existing files are never overwritten\n");return 2;}
    m=calloc(1,sizeof(*m));if(!m)return 1;
    if(!load())goto done;
    if(!m->battery.adc||!m->battery.charger_port||!m->lp_timer.attached||!m->cellular.ready)goto done;
    for(i=0;i<1200000000;++i){if(!step())goto done;
        if((i+1)%100000000==0){printf("boot steps=%u resets=%u PC=%08x guest=%llu events=%llu ADC=%llu\n",i+1,resets,m->cpu.r[15],(unsigned long long)m->cpu.instructions,(unsigned long long)m->cellular.delivered_events,(unsigned long long)m->dsp_mailbox.conversions);fflush(stdout);}
        if(m->cpu.instructions>=350000000&&!storage.provision&&!storage.defaults_boot&&m->cellular.name_ptr&&m->cellular.delivered_events>=3)break;}
    if(i==1200000000||m->cpu.halted||!m->dsp_mailbox.conversions||!picture("idle")||!userfs_directory(m,&directory))goto done;
    printf("Booted %s, resets=%u, user files=%u\n",m->name,resets,directory.count);
    if(!key(8)||!key(9)||!key(10)||!key(11)||!key(6)||!run(50000000)||!picture("outgoing"))goto done;
    if(!m->cellular.outgoing||m->cellular.call_state!=CELL_FROM_PHONE||
        !cellular_answer(&m->cellular,1)||!run(50000000)||m->cellular.call_state!=CELL_CONNECTED)goto done;
    if(!cellular_answer(&m->cellular,0)||!run(50000000))goto done;
    before=m->cellular.delivered_events;
    if(!cellular_sms(&m->cellular,"+79990001122",text,sizeof(text)/sizeof(text[0]),timestamp)||!run(100000000)||!picture("sms"))goto done;
    if(m->cellular.delivered_events<=before||m->cellular.count||m->cellular.stage)goto done;
    if(!cellular_call(&m->cellular,&m->bus,"+79990001234")||!run(50000000)||!picture("call"))goto done;
    if(m->cellular.count||m->cellular.stage||!cellular_answer(&m->cellular,0)||!run(50000000))goto done;
    before=m->dsp_mailbox.conversions;battery_set(&m->battery,25,1);
    if(!run(100000000)||!picture("charging")||m->dsp_mailbox.conversions<=before)goto done;
    printf("PASS SMS=%llu events=%llu ADC=%llu tones=%llu LCD=%llu\n",(unsigned long long)m->cellular.received_sms,(unsigned long long)m->cellular.delivered_events,(unsigned long long)m->dsp_mailbox.conversions,(unsigned long long)m->phone_io.tone_changes,(unsigned long long)m->lcd.writes);
    result=0;
done:
    if(result){(void)picture("failure");fprintf(stderr,"FAIL PC=%08x r0=%08x r1=%08x r2=%08x LR=%08x hz=%u halted=%d machine=%s CPU=%s cellular=%s provision=%d/%d defaults=%d resets=%u\n",m->cpu.r[15],m->cpu.r[0],m->cpu.r[1],m->cpu.r[2],m->cpu.r[14],m->clock.arm_hz,m->cpu.halted,m->error,m->cpu.error?m->cpu.error:"",m->cellular.error,storage.provision,storage.stage,storage.defaults_boot,resets);}
    nvram_store_release(&storage);machine_destroy(m);free(m);return result;
}
