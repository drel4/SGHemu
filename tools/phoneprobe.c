#include "machine.h"
#include "nvram.h"
#include "content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int run(Machine *m,ContentImport *im,unsigned n)
{
    while(n--){if(im->active&&!im->done&&content_import_step(im,m)==0)return 0;
        if(!machine_step(m,1))return 0;}return 1;
}
int main(int argc,char **argv)
{
    Machine *m;ContentImport im={0};unsigned i,start=4,settle=8000000;int java=0;char path[1024];
    if(argc<4){fprintf(stderr,"Usage: phoneprobe MODEL.cfg SNAPSHOT.snv OUTPUT_PREFIX [--java-menu] [key IDs]\n"
        "Read-only snapshot test. Keys: 0 left-soft, 1 up, 2 right-soft, 4 OK, 7 down, 9..17 digits 1..9.\n");return 2;}
    m=calloc(1,sizeof(*m));if(!m)return 1;
    if(!machine_load(m,argv[1],NULL,NULL)||!nvram_load(m,argv[2]))goto failed;
    while(start<(unsigned)argc){
        if(!strcmp(argv[start],"--java-menu")){java=1;settle=100000000;++start;}
        else if(!strcmp(argv[start],"--settle")&&start+1<(unsigned)argc){char *end;
            unsigned long value=strtoul(argv[start+1],&end,10);if(*end||!value||value>1000000000)goto failed;
            settle=(unsigned)value;start+=2;}
        else break;}
    if(java&&!content_java_menu(&im,m))goto failed;
    if(!run(m,&im,600000000))goto failed;
    snprintf(path,sizeof(path),"%s-0.bmp",argv[3]);if(!lcd_save_bmp(&m->lcd,path))goto failed;
    for(i=start;i<(unsigned)argc;++i){char *end;unsigned long key=strtoul(argv[i],&end,10);
        if(*end||key>=SGH_MAX_KEYS)goto failed;
        machine_key(m,(unsigned)key,1);if(!run(m,&im,2000000))goto failed;
        machine_key(m,(unsigned)key,0);if(!run(m,&im,settle))goto failed;
        snprintf(path,sizeof(path),"%s-%u.bmp",argv[3],i-start+1);if(!lcd_save_bmp(&m->lcd,path))goto failed;}
    printf("Probe complete: PC=%08x; snapshot unchanged.\n",m->cpu.r[15]);
    content_import_release(&im);machine_destroy(m);free(m);return 0;
failed:
    fprintf(stderr,"Probe failed: PC=%08x %s %s %s\n",m->cpu.r[15],m->cpu.error?m->cpu.error:"",m->error,im.error);
    content_import_release(&im);machine_destroy(m);free(m);return 1;
}
