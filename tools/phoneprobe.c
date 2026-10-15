#include "machine.h"
#include "nvram.h"
#include "content.h"
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int run(Machine *m,ContentImport *im,unsigned n)
{
    while(n--){if(m->rtc.host_sync&&!(m->cpu.instructions&65535u))(void)host_sync_rtc(&m->rtc);
        if(!m->cellular.stage&&(m->battery.stage||!(im->active&&!im->done))&&!battery_step(&m->battery,&m->cpu))return 0;
        if(!m->cellular.stage&&!m->battery.stage&&im->active&&!im->done&&content_import_step(im,m)==0)return 0;
        if(!machine_step(m,1))return 0;}return 1;
}
int main(int argc,char **argv)
{
    Machine *m;ContentImport im={0};unsigned i,start=4,settle=8000000;int java=0,sync=0;char path[1024];
    const char *incoming=NULL,*sms=NULL;
    if(argc<4){fprintf(stderr,"Usage: phoneprobe MODEL.cfg SNAPSHOT.snv OUTPUT_PREFIX [--java-menu] [key IDs]\n"
        "Read-only snapshot test. Keys: 0 left-soft, 1 up, 2 right-soft, 4 OK, 7 down, 9..17 digits 1..9.\n");return 2;}
    m=calloc(1,sizeof(*m));if(!m)return 1;
    if(!machine_load(m,argv[1],NULL,NULL)||!nvram_load(m,argv[2]))goto failed;
    while(start<(unsigned)argc){
        if(!strcmp(argv[start],"--java-menu")){java=1;settle=100000000;++start;}
        else if(!strcmp(argv[start],"--extended-java")){m->java_extended=1;++start;}
        else if(!strcmp(argv[start],"--cellular")){if(!cellular_set_network(&m->cellular,&m->bus,1,"Tele2"))goto failed;++start;}
        else if(!strcmp(argv[start],"--incoming-call")&&start+1<(unsigned)argc){incoming=argv[start+1];start+=2;}
        else if(!strcmp(argv[start],"--sms")&&start+1<(unsigned)argc){sms=argv[start+1];start+=2;}
        else if(!strcmp(argv[start],"--cpu-audio-wav")&&start+1<(unsigned)argc){
            if(!phone_io_open(&m->phone_io,0,argv[start+1]))goto failed;start+=2;}
        else if(!strcmp(argv[start],"--rtc-sync")){sync=1;++start;}
        else if(!strcmp(argv[start],"--charging")){battery_set(&m->battery,m->battery.percent,1);++start;}
        else if(!strcmp(argv[start],"--battery")&&start+1<(unsigned)argc){char *end;unsigned long n=strtoul(argv[start+1],&end,10);
            if(*end||n>100)goto failed;battery_set(&m->battery,(unsigned)n,(int)m->battery.charging);start+=2;}
        else if(!strcmp(argv[start],"--settle")&&start+1<(unsigned)argc){char *end;
            unsigned long value=strtoul(argv[start+1],&end,10);if(*end||!value||value>1000000000)goto failed;
            settle=(unsigned)value;start+=2;}
        else break;}
    if(java&&!content_java_menu(&im,m))goto failed;
    if(sync){if(!host_sync_rtc(&m->rtc))goto failed;rtc_set_sync(&m->rtc,1);}
    if(!run(m,&im,600000000))goto failed;
    if(incoming){if(!cellular_call(&m->cellular,&m->bus,incoming)||!run(m,&im,100000000))goto failed;}
    if(sms){uint16_t text[70];static const uint8_t timestamp[]={0x62,0x01,0x41,0x21,0x00,0x00,0x00};size_t n=strlen(sms);
        if(n>70)goto failed;for(i=0;i<n;++i)text[i]=(uint8_t)sms[i];
        if(!cellular_sms(&m->cellular,"+79990001122",text,(unsigned)n,timestamp)||!run(m,&im,100000000))goto failed;}
    snprintf(path,sizeof(path),"%s-0.bmp",argv[3]);if(!lcd_save_bmp(&m->lcd,path))goto failed;
    for(i=start;i<(unsigned)argc;++i){char *end;unsigned long key=strtoul(argv[i],&end,10);
        if(*end||key>=SGH_MAX_KEYS)goto failed;
        machine_key(m,(unsigned)key,1);if(!run(m,&im,2000000))goto failed;
        machine_key(m,(unsigned)key,0);if(!run(m,&im,settle))goto failed;
        snprintf(path,sizeof(path),"%s-%u.bmp",argv[3],i-start+1);if(!lcd_save_bmp(&m->lcd,path))goto failed;}
    printf("CPU tones: changes=%llu frames=%llu nonzero=%llu; LED changes=%llu state=%u\n",
        (unsigned long long)m->phone_io.tone_changes,(unsigned long long)m->phone_io.frames,
        (unsigned long long)m->phone_io.nonzero,(unsigned long long)m->phone_io.led_changes,m->phone_io.led);
    printf("Probe complete: PC=%08x; snapshot unchanged.\n",m->cpu.r[15]);
    printf("Cellular: stage=%u events=%u delivered=%llu state=%u call=%u name=%08x sms-sent=%llu error=%s\n",
        m->cellular.stage,m->cellular.count,(unsigned long long)m->cellular.delivered_events,m->cellular.call_state,
        m->cellular.call_id,m->cellular.name_ptr,(unsigned long long)m->cellular.sent_sms,m->cellular.error);
    content_import_release(&im);machine_destroy(m);free(m);return 0;
failed:
    fprintf(stderr,"Probe failed: PC=%08x %s %s %s\n",m->cpu.r[15],m->cpu.error?m->cpu.error:"",m->error,im.error);
    content_import_release(&im);machine_destroy(m);free(m);return 1;
}
