#include "ma5_hw.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>
/* Read the pinned DLL's FM-engine active voice count: engine pointer at
   this+0x120, count byte at engine+0x1804 (FUN_10015670/FUN_10016198).
   Same pinned-layout read strategy ma5_hw.c uses for its PCM cursor. */
static int peek4(uintptr_t address,uint32_t *out)
{
    MEMORY_BASIC_INFORMATION region;
    if(!VirtualQuery((void*)address,&region,sizeof(region))||region.State!=MEM_COMMIT)return 0;
    if(!(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return 0;
    memcpy(out,(void*)address,4);return 1;
}
int main(int argc,char **argv)
{
    char error[256]={0};Ma5Hw *h;HMODULE base;uintptr_t engine;
    uint32_t before,after,ptr;
    if(argc!=2){fprintf(stderr,"Usage: ma5fmprobe M5_EmuHw.dll\n");return 2;}
    h=ma5_hw_open(argv[1],NULL,0,error,sizeof(error));
    if(!h){fprintf(stderr,"open: %s\n",error);return 1;}
    base=GetModuleHandleA(argv[1]);
    if(!base||!peek4((uintptr_t)base+0x101780+0x120,&ptr)||!ptr){fprintf(stderr,"FM engine pointer unreadable\n");ma5_hw_close(h);return 1;}
    engine=ptr;
    if(!peek4(engine+0x1804,&before)){fprintf(stderr,"voice count unreadable before\n");ma5_hw_close(h);return 1;}
    ma5_hw_register(h,0x38,1,0);
    ma5_hw_generate(h,1);
    if(!peek4(engine+0x1804,&after)){fprintf(stderr,"voice count unreadable after\n");ma5_hw_close(h);return 1;}
    printf("FM engine +0x1804 voice count: before=%u after=%u\n",before,after);
    ma5_hw_close(h);
    return (before==0x10&&after==0x20)?0:1;
}