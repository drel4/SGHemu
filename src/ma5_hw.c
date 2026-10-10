#include "ma5_hw.h"
#include "file.h"
#include <windows.h>
#include <mmsystem.h>
#include <bcrypt.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#if !defined(_M_IX86)
#error Yamaha hardware backend requires x86
#endif
#define OUTPUT_SLOTS 8
typedef int (__cdecl *HwInit)(unsigned, unsigned char, void (__cdecl *)(void));
typedef void (__cdecl *HwTerm)(void);
typedef void (__cdecl *HwGenerate)(int16_t *, int16_t *, unsigned);
typedef void (__cdecl *HwWrite)(unsigned char);
typedef unsigned char (__cdecl *HwRead)(void);
struct Ma5Hw {
    HANDLE thread, request, done, ready, wave_event;
    HMODULE dll;
    HwInit init; HwTerm term; HwGenerate generate;
    HwWrite write_status, write_data; HwRead read_status, read_data;
    HWAVEOUT wave; WAVEHDR headers[OUTPUT_SLOTS]; int16_t pcm[OUTPUT_SLOTS][960]; unsigned next;
    FILE *wav; char path[1024], wav_path[1024], error[256];
    int speakers, initialized, op, paused, timer_period; unsigned arg, result;
    volatile LONG irq, callbacks;
    Ma5HwStats stats;
    unsigned adpcm_dirty[32];
    const uint8_t *packet;unsigned packet_size;
};
static volatile LONG occupied;
static int native_u32(uintptr_t address,uint32_t *value)
{
    MEMORY_BASIC_INFORMATION region;
    if(!VirtualQuery((const void *)address,&region,sizeof(region))||region.State!=MEM_COMMIT||
       (region.Protect&(PAGE_NOACCESS|PAGE_GUARD))||
       !(region.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|
           PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||
       address<(uintptr_t)region.BaseAddress||
       address-(uintptr_t)region.BaseAddress>region.RegionSize||
       region.RegionSize-(address-(uintptr_t)region.BaseAddress)<sizeof(*value))return 0;
    memcpy(value,(const void *)address,sizeof(*value));return 1;
}
static Ma5Hw *callback_owner; /* DLL singleton, touched only by its worker. */
static uintptr_t pcm_channel(Ma5Hw *h,unsigned voice)
{
    uint32_t engine;
    if(voice>=32||!native_u32((uintptr_t)h->dll+0x101780+0x124,&engine)||!engine)return 0;
    return (uintptr_t)engine+voice*0x80cc;
}
static int native_store(uintptr_t address,uint32_t value)
{
    MEMORY_BASIC_INFORMATION region;uint32_t original;
    if(!native_u32(address,&original)||!VirtualQuery((void *)address,&region,sizeof(region))||
       !(region.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return 0;
    memcpy((void *)address,&value,sizeof(value));return 1;
}
static void ram_changed(Ma5Hw *h,unsigned address,unsigned length)
{
    unsigned voice;uintptr_t start=(uintptr_t)h->dll+0x101780+0x160+address;
    if(address<0x4000||address>=0x6000||!length||length>0x6000-address)return;
    for(voice=0;voice<32;++voice) {
        uintptr_t channel=pcm_channel(h,voice);uint32_t format,source,count,active;
        if(!channel||!native_u32(channel+4,&active)||!(active&255)||
           !native_u32(channel+0x8028,&format)||((format>>8)&3)||
           !native_u32(channel+0x10,&source)||!native_u32(channel+0x14,&count))continue;
        /* Only writes overlapping already decoded input make the cache stale.
           Decoder 1001BCE4: next index +10, step +14, predictor +18 (channel+4).
           Leave predictor/step intact: a stream is continuous across refills. */
        if(start<(uintptr_t)source+(count+1)/2&&start+length>source)h->adpcm_dirty[voice]=1;
    }
}
static int synthesize(Ma5Hw *h,int16_t *left,int16_t *right,unsigned frames)
{
    while(frames) {
        unsigned voice,count=frames;uint32_t before[32]={0};
        for(voice=0;voice<32;++voice)if(h->adpcm_dirty[voice]) {
            uintptr_t channel=pcm_channel(h,voice);uint32_t phase,step,end,loop,format,active;
            uint64_t remaining,n,last;
            if(!channel||!native_u32(channel+0x8028,&format)||!native_u32(channel+4,&active))return 0;
            if(((format>>8)&3)||!(active&255)){h->adpcm_dirty[voice]=0;continue;}
            if(!channel||!native_u32(channel+0x808c,&phase)||!native_u32(channel+0x80b8,&step)||
               !native_u32(channel+0x8040,&end)||!native_u32(channel+0x803c,&loop))return 0;
            if(loop>=end||!step){h->adpcm_dirty[voice]=0;continue;}
            before[voice]=phase;
            remaining=(uint64_t)end*65536;
            last=(uint64_t)(end-1)*65536;
            if(phase<last)remaining=last;
            else {
                uint32_t decoded,tail,pair;unsigned shift=(loop&1)*16;
                /* MA-5 interpolates the final sample against cached loop[0].
                   That cache still contains the previous lap's opening. Hold
                   the decoded tail for this fractional boundary sample instead
                   of mixing in stale audio. After wrap, the existing refresh
                   decodes the new opening with uninterrupted ADPCM history. */
                if(end>0x4000||!native_u32(channel+0x14,&decoded))return 0;
                if(decoded>=end) {
                    if(!native_u32(channel+0x20+((end-1)&~1u)*2,&tail)||
                       !native_u32(channel+0x20+(loop&~1u)*2,&pair))return 0;
                    tail=(tail>>(((end-1)&1)*16))&0xffff;
                    pair=(pair&~(0xffffu<<shift))|(tail<<shift);
                    if(!native_store(channel+0x20+(loop&~1u)*2,pair))return 0;
                }
            }
            if(remaining>phase){n=(remaining-phase+step-1)/step;if(n<count)count=(unsigned)n;}
        }
        h->generate(left,right,count);
        for(voice=0;voice<32;++voice)if(h->adpcm_dirty[voice]) {
            uintptr_t channel=pcm_channel(h,voice);uint32_t phase;
            if(!channel||!native_u32(channel+0x808c,&phase))return 0;
            if(phase<before[voice]) {
                if(!native_store(channel+0x14,0))return 0;
                ++h->stats.adpcm_refreshes;
                h->adpcm_dirty[voice]=0;
            }
        }
        frames-=count;left+=count;right+=count;
    }
    return 1;
}
static void __cdecl irq_callback(void)
{
    if(callback_owner) { InterlockedExchange(&callback_owner->irq,1); InterlockedIncrement(&callback_owner->callbacks); }
}
static int verify(const char *path)
{
    static const char expected[]="f2702720d01730b3555d042cd22edd5ff58066b59da8f11241236170f26c4732";
    BCRYPT_ALG_HANDLE alg=NULL; BCRYPT_HASH_HANDLE hash=NULL;
    uint8_t *bytes=NULL,digest[32];size_t size;char hex[65];unsigned i;int ok=0;
    DWORD dummy=0,n; BYTE *version_data=NULL;VS_FIXEDFILEINFO *info;UINT length;
    if(!file_read(path,&bytes,&size)||size>ULONG_MAX)goto done;
    if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0||
       BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0||BCryptHashData(hash,bytes,(ULONG)size,0)<0||
       BCryptFinishHash(hash,digest,32,0)<0)goto done;
    for(i=0;i<32;++i)sprintf(hex+2*i,"%02x",digest[i]);
    if(strcmp(hex,expected))goto done;
    n=GetFileVersionInfoSizeA(path,&dummy);if(!n)goto done;version_data=malloc(n);if(!version_data)goto done;
    if(!GetFileVersionInfoA(path,0,n,version_data)||!VerQueryValueA(version_data,"\\",(LPVOID *)&info,&length)||length<sizeof(*info))goto done;
    ok=info->dwFileVersionMS==0x00010006u && info->dwFileVersionLS==0;
done:
    if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
    free(bytes);free(version_data);return ok;
}
static void put_le32(uint8_t *p,uint32_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=(uint8_t)(n>>16);p[3]=(uint8_t)(n>>24);}
static int wav_header(Ma5Hw *h)
{
    uint8_t header[44]={0};uint32_t bytes=(uint32_t)(h->stats.frames*4);
    memcpy(header,"RIFF",4);put_le32(header+4,bytes+36);memcpy(header+8,"WAVEfmt ",8);put_le32(header+16,16);
    header[20]=1;header[22]=2;put_le32(header+24,48000);put_le32(header+28,192000);header[32]=4;header[34]=16;
    memcpy(header+36,"data",4);put_le32(header+40,bytes);
    return !fseek(h->wav,0,SEEK_SET)&&fwrite(header,1,44,h->wav)==44;
}
static int generate(Ma5Hw *h,unsigned frames)
{
    int16_t left[482]={0},right[482]={0};unsigned i,slot=h->next;WAVEHDR *header=&h->headers[slot];
    if(!frames||frames>480||(h->wav&&h->stats.frames>(UINT32_MAX-36u)/4u-frames))return 0;
    left[0]=left[frames+1]=12345;right[0]=right[frames+1]=12345;
    if(!synthesize(h,left+1,right+1,frames))return 0;
    if(left[0]!=12345||left[frames+1]!=12345||right[0]!=12345||right[frames+1]!=12345)return 0;
    if(h->wave && (header->dwFlags&WHDR_PREPARED)) {
        unsigned finished=0;
        /* A fully drained, previously filled ring means the host ran out of
           PCM. Do not mislabel initial fill or intentional pause as underrun. */
        for(i=0;i<OUTPUT_SLOTS;++i)
            if((h->headers[i].dwFlags&(WHDR_PREPARED|WHDR_DONE))==(WHDR_PREPARED|WHDR_DONE))++finished;
        if(finished==OUTPUT_SLOTS&&!h->paused)++h->stats.underruns;
        while(!(header->dwFlags&WHDR_DONE)) { if(WaitForSingleObject(h->wave_event,2000)==WAIT_TIMEOUT)return 0; }
        if(waveOutUnprepareHeader(h->wave,header,sizeof(*header))!=MMSYSERR_NOERROR)return 0;
    }
    for(i=0;i<frames;++i) {
        int l=left[i+1],r=right[i+1];unsigned a=(unsigned)(l<0?-l:l),b=(unsigned)(r<0?-r:r);
        h->pcm[slot][i*2]=(int16_t)l;h->pcm[slot][i*2+1]=(int16_t)r;
        if(l)++h->stats.nonzero;if(r)++h->stats.nonzero;
        if(a>h->stats.peak)h->stats.peak=a;if(b>h->stats.peak)h->stats.peak=b;
    }
    if(h->wav&&fwrite(h->pcm[slot],4,frames,h->wav)!=frames)return 0;
    h->stats.frames+=frames;
    if(h->wave) {
        memset(header,0,sizeof(*header));header->lpData=(LPSTR)h->pcm[slot];header->dwBufferLength=frames*4;
        if(waveOutPrepareHeader(h->wave,header,sizeof(*header))!=MMSYSERR_NOERROR||
           waveOutWrite(h->wave,header,sizeof(*header))!=MMSYSERR_NOERROR)return 0;
        h->next=(slot+1)%OUTPUT_SLOTS;
    }
    return 1;
}
#define LOAD(member,name) do { FARPROC proc=GetProcAddress(h->dll,name);if(!proc)goto failed; \
    _Static_assert(sizeof(h->member)==sizeof(proc),"x86 function pointer");memcpy(&h->member,&proc,sizeof(proc)); } while(0)
static unsigned __stdcall worker(void *arg)
{
    Ma5Hw *h=arg;unsigned i;WAVEFORMATEX format={0};
    if(!verify(h->path)){strcpy(h->error,"Hardware DLL hash/version mismatch or unreadable file (requires pinned 1.6.0.0)");goto failed;}
    h->dll=LoadLibraryExA(h->path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);if(!h->dll)goto failed;
    LOAD(init,"Hw_Initialize");LOAD(term,"Hw_Terminate");LOAD(generate,"Hw_Generate");
    LOAD(write_status,"Hw_WriteStatusFlagReg");LOAD(write_data,"Hw_WriteDataReg");
    LOAD(read_status,"Hw_ReadStatusFlagReg");LOAD(read_data,"Hw_ReadDataReg");
    callback_owner=h;
    if(h->init(48000,0,irq_callback)!=0x46){strcpy(h->error,"Hw_Initialize did not return 0x46");goto failed;}
    h->initialized=1;
    if(*h->wav_path){h->wav=fopen(h->wav_path,"wb");if(!h->wav||!wav_header(h))goto failed;}
    if(h->speakers) {
        h->wave_event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!h->wave_event)goto failed;
        format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=48000;
        format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=192000;
        if(waveOutOpen(&h->wave,WAVE_MAPPER,&format,(DWORD_PTR)h->wave_event,0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)goto failed;
        /* Win7-compatible, balanced lifetime; avoids ~31ms GUI callbacks
           from a 16ms timer on the default system resolution. */
        h->timer_period=timeBeginPeriod(1)==TIMERR_NOERROR;
    }
    SetEvent(h->ready);
    for(;;) {
        WaitForSingleObject(h->request,INFINITE);h->result=1;
        switch(h->op) {
        case 0:h->result=h->read_status();break;
        case 1:h->result=h->read_data();break;
        case 2:h->write_status((unsigned char)h->arg);break;
        case 3:h->write_data((unsigned char)h->arg);break;
        case 4:if(!generate(h,h->arg)){h->stats.failed=1;h->result=0;}break;
        case 5:goto cleanup;
        case 6:h->term();h->initialized=0;h->irq=0;memset(h->adpcm_dirty,0,sizeof(h->adpcm_dirty));
            if(h->init(48000,0,irq_callback)!=0x46){h->stats.failed=1;h->result=0;}else h->initialized=1;break;
        case 7:if(h->wave){if(h->arg)waveOutPause(h->wave);else waveOutRestart(h->wave);}h->paused=(int)h->arg;break;
        case 8:
            for(i=0;i<1000000;++i) {
                uintptr_t before,after;unsigned char value;
                __asm {mov before,esp}
                value=h->read_status();
                __asm {mov after,esp}
                if(before!=after){h->result=0;break;}(void)value;
            }
            break;
        case 9:
            h->write_status(2);
            if((h->arg>>8)<128)h->write_data((unsigned char)((h->arg>>8)|128));
            else {h->write_data((unsigned char)((h->arg>>8)&127));h->write_data((unsigned char)((h->arg>>15)|128));}
            h->write_data((unsigned char)((h->arg&127)|128));break;
        case 10:
            h->write_status(4);h->write_data(0);h->write_status((unsigned char)(h->arg>>8));h->write_data((unsigned char)h->arg);
            h->write_status(4);h->write_data((unsigned char)(h->arg>>16));break;
        case 12:
            h->write_status(4);h->write_data(0);
            for(i=0;i<64;++i) {h->write_status((unsigned char)i);printf("HW reg %02x=%02x%s",i,h->read_data(),(i&7)==7?"\n":" ");}
            h->write_status(0x80);break;
        case 14: {
            /* The public read protocol has no WT cursor. For this exact hashed
               DLL only: singleton +124 is the PCM engine; channels are 80CC
               bytes apart. Its decoder argument is channel+4: phase is +8088
               (Q16 samples), format byte +8025. Read only, on the owning worker.
               Ghidra: 10001AD0, 1001C4E8, 1001C13C, 1001B324/5CC/820. */
            uint32_t engine,phase,sample_format;
            uintptr_t channel;
            h->result=UINT32_MAX;
            if(h->arg>=32||!native_u32((uintptr_t)h->dll+0x101780+0x124,&engine)||!engine)break;
            channel=(uintptr_t)engine+h->arg*0x80cc;
            if(!native_u32(channel+0x808c,&phase)||!native_u32(channel+0x8028,&sample_format))break;
            h->result=(((sample_format>>8)&3)<<24)|(phase>>16);break;
        }
        case 15:ram_changed(h,h->arg&0xffff,h->arg>>16);break;
        case 16:h->write_status((unsigned char)h->arg);
            for(i=0;i<h->packet_size;++i)h->write_data(h->packet[i]);break;
        case 13: {
            unsigned address=h->arg&0xfffff,bytes=(h->arg&0x100000)?3:address>=128?2:1;
            h->write_status(3);
            for(i=0;i<bytes;++i)h->write_data((unsigned char)(((address>>(i*7))&127)|(i+1==bytes?128:0)));
            h->write_data(0x80); /* Read requests execute synchronously, no sample-time advance. */
            if(!(h->read_status()&0x40)){h->stats.failed=1;h->result=0;break;}
            h->write_status(0x41);h->result=h->read_data();break;
        }
        }
        SetEvent(h->done);
    }
failed:
    h->stats.failed=1;if(!*h->error)snprintf(h->error,sizeof(h->error),"Hardware audio setup failed (Win32 %lu)",GetLastError());SetEvent(h->ready);
cleanup:
    if(h->wave) {
        waveOutReset(h->wave);
        for(i=0;i<OUTPUT_SLOTS;++i)if(h->headers[i].dwFlags&WHDR_PREPARED)waveOutUnprepareHeader(h->wave,&h->headers[i],sizeof(WAVEHDR));
        waveOutClose(h->wave);
    }
    if(h->wave_event)CloseHandle(h->wave_event);
    if(h->timer_period)timeEndPeriod(1);
    if(h->wav){if(!wav_header(h))h->stats.failed=1;fclose(h->wav);}
    if(h->initialized)h->term();callback_owner=NULL;if(h->dll)FreeLibrary(h->dll);
    InterlockedExchange(&occupied,0);return 0;
}
static unsigned command(Ma5Hw *h,int op,unsigned arg)
{
    if(!h||h->stats.failed)return 0;
    h->op=op;h->arg=arg;SetEvent(h->request);
    /* The only sender is the emulator thread; no DLL calls on this thread. */
    WaitForSingleObject(h->done,INFINITE);return h->result;
}
Ma5Hw *ma5_hw_open(const char *dll,const char *wav,int speakers,char *error,unsigned error_size)
{
    Ma5Hw *h=NULL;uintptr_t thread;DWORD n;
    snprintf(error,error_size,"Cannot allocate or initialize hardware audio worker");
    if(InterlockedCompareExchange(&occupied,1,0)) {snprintf(error,error_size,"Hardware DLL is a singleton; another card is active");return NULL;}
    h=calloc(1,sizeof(*h));if(!h)goto failed;
    n=GetFullPathNameA(dll,sizeof(h->path),h->path,NULL);if(!n||n>=sizeof(h->path))goto failed;
    if(wav){if(strlen(wav)>=sizeof(h->wav_path))goto failed;strcpy(h->wav_path,wav);}h->speakers=speakers;
    h->request=CreateEventA(NULL,FALSE,FALSE,NULL);h->done=CreateEventA(NULL,FALSE,FALSE,NULL);h->ready=CreateEventA(NULL,FALSE,FALSE,NULL);
    if(!h->request||!h->done||!h->ready)goto failed;
    thread=_beginthreadex(NULL,0,worker,h,0,NULL);if(!thread)goto failed;h->thread=(HANDLE)thread;
    WaitForSingleObject(h->ready,INFINITE);
    if(!h->stats.failed){if(error_size)*error=0;return h;}
    snprintf(error,error_size,"%s",h->error);WaitForSingleObject(h->thread,INFINITE);CloseHandle(h->thread);h->thread=NULL;
failed:
    if(h){if(h->request)CloseHandle(h->request);if(h->done)CloseHandle(h->done);if(h->ready)CloseHandle(h->ready);free(h);}
    InterlockedExchange(&occupied,0);return NULL;
}
unsigned ma5_hw_read(Ma5Hw *h,int data){return command(h,data?1:0,0);}
void ma5_hw_write(Ma5Hw *h,int data,unsigned value){(void)command(h,data?3:2,value);}
void ma5_hw_fifo(Ma5Hw *h,unsigned selector,const uint8_t *bytes,unsigned length)
{
    if(!h||!bytes||!length)return;
    /* Synchronous single-sender command: bytes stays alive through completion. */
    h->packet=bytes;h->packet_size=length;(void)command(h,16,selector);
    h->packet=NULL;h->packet_size=0;
}
int ma5_hw_generate(Ma5Hw *h,unsigned frames){return (int)command(h,4,frames);}
void ma5_hw_pause(Ma5Hw *h,int paused){(void)command(h,7,(unsigned)(paused!=0));}
void ma5_hw_reset(Ma5Hw *h){(void)command(h,6,0);}
void ma5_hw_parameter(Ma5Hw *h,unsigned address,unsigned value){(void)command(h,9,(address<<8)|(value&255));}
unsigned ma5_hw_memory(Ma5Hw *h,unsigned address,int full_ram){return command(h,13,address|(full_ram?0x100000:0));}
unsigned ma5_hw_pcm_cursor(Ma5Hw *h,unsigned voice){return command(h,14,voice);}
void ma5_hw_ram_changed(Ma5Hw *h,unsigned address,unsigned length){(void)command(h,15,(length<<16)|(address&0xffff));}
void ma5_hw_register(Ma5Hw *h,unsigned reg,unsigned value,unsigned bank){(void)command(h,10,(bank<<16)|(reg<<8)|(value&255));}
int ma5_hw_irq(Ma5Hw *h){return h?(int)InterlockedExchange(&h->irq,0):0;}
int ma5_hw_stress(Ma5Hw *h){return (int)command(h,8,0);}
Ma5HwStats ma5_hw_stats(Ma5Hw *h){Ma5HwStats result={0};if(h){result=h->stats;result.callbacks=(uint64_t)(unsigned)InterlockedCompareExchange(&h->callbacks,0,0);}return result;}
void ma5_hw_diagnostics(Ma5Hw *h){(void)command(h,12,0);}
void ma5_hw_close(Ma5Hw *h)
{
    if(!h)return;
    /* Even a failed generate must still deliver teardown to the worker. */
    h->op=5;SetEvent(h->request);WaitForSingleObject(h->thread,INFINITE);
    CloseHandle(h->thread);CloseHandle(h->request);CloseHandle(h->done);CloseHandle(h->ready);free(h);
}
