/* Interactive real-ROM probe. Input snapshot is never overwritten. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "machine.h"
#include "nvram.h"
#include "key_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int run(Machine *m,unsigned count)
{
    while(count--){if(!m->cellular.stage&&!battery_step(&m->battery,&m->cpu))return 0;
        if(!machine_step(m,1))return 0;}return 1;
}
int main(int argc,char **argv)
{
    Machine *m=calloc(1,sizeof(*m));char line[1200],image[1024];unsigned key,count;int result=0;FILE *trace=NULL;
    if(argc!=4||!m){fprintf(stderr,"Usage: phonedrive MODEL.cfg READ_ONLY.snv LCD.bmp\n");free(m);return 2;}
    if(!machine_load(m,argv[1],NULL,NULL)||!nvram_load(m,argv[2])||
        !cellular_set_network(&m->cellular,&m->bus,1,"Tele2")){result=1;goto done;}
    snprintf(image,sizeof(image),"%s.sim",argv[2]);
    if(GetFileAttributesA(image)!=INVALID_FILE_ATTRIBUTES&&!sim_card_load_storage(&m->sim.card,image)){result=1;goto done;}
    phone_net_set_mode(&m->network,2);snprintf(image,sizeof(image),"%s",argv[3]);
    if(!run(m,100000000)){result=1;goto done;}
    puts("Commands: key ID, tap ID (short multi-tap), text ENGLISH_TEXT, run INSTRUCTIONS, peek HEX_ADDRESS, trace FILE, export NEW.snv, quit. Input snapshot is read-only.");
    for(;;){(void)lcd_save_bmp(&m->lcd,image);
        printf("PC=%08x mode=%u queries=%llu starts=%llu bearer=%u TX=%llu RX=%llu queue=%u stage=%u error=%s\n> ",m->cpu.r[15],m->network.mode,
            (unsigned long long)m->network.state_queries,(unsigned long long)m->network.start_requests,m->network.last_bearer,
            (unsigned long long)m->network.transmitted,(unsigned long long)m->network.received,m->cellular.count,m->cellular.stage,m->network.error);fflush(stdout);
        if(!fgets(line,sizeof(line),stdin)||!strncmp(line,"quit",4))break;
        if(sscanf(line,"key %u",&key)==1&&key<SGH_MAX_KEYS){machine_key(m,key,1);if(!run(m,2000000)){result=1;break;}
            machine_key(m,key,0);if(!run(m,40000000)){result=1;break;}}
        else if(sscanf(line,"tap %u",&key)==1&&key<SGH_MAX_KEYS){machine_key(m,key,1);if(!run(m,500000)){result=1;break;}
            machine_key(m,key,0);if(!run(m,500000)){result=1;break;}}
        else if(!strncmp(line,"text ",5)){KeyText input;size_t n=strcspn(line+5,"\r\n"),j;int down;unsigned steps=0;
            key_text_reset(&input);
            for(j=0;j<n;++j)if(!key_text_char(&input,(unsigned char)line[5+j]))break;
            if(j!=n){puts("Unsupported text or queue full; no keys sent.");continue;}
            while(input.count||input.pressed){
                if(key_text_tick(&input,m->clock.nanoseconds,&key,&down))machine_key(m,key,down);
                if(!run(m,1000)||++steps>500000){result=1;break;}
            }
            if(result)break;if(!run(m,4000000)){result=1;break;}}
        else if(sscanf(line,"run %u",&count)==1&&count<=1000000000){if(!run(m,count)){result=1;break;}}
        else if(sscanf(line,"peek %x",&key)==1){printf("%08x = %08x\n",key,bus_read(&m->bus,key,4));}
        else if(!strncmp(line,"export ",7)){HANDLE file;size_t n=strcspn(line+7,"\r\n");line[7+n]=0;
            file=CreateFileA(line+7,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
            if(file==INVALID_HANDLE_VALUE)puts("Export refused: output must be a new file in an existing folder.");
            else {CloseHandle(file);if(!nvram_save(m,line+7)){remove(line+7);puts(m->error);}else puts("Exported a separate snapshot; original input unchanged.");}}
        else if(!strncmp(line,"trace ",6)){size_t n=strcspn(line+6,"\r\n");line[6+n]=0;if(trace)fclose(trace);
            trace=fopen(line+6,"a");m->network.trace=trace;if(!trace)puts("Could not open packet trace.");}
        else puts("Unknown command or out-of-range key/instruction count.");
    }
done:
    if(result)fprintf(stderr,"Probe failed: %s %s %s\n",m->error,m->cpu.error?m->cpu.error:"",m->cellular.error);
    machine_destroy(m);if(trace)fclose(trace);free(m);return result;
}
