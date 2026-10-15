#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phone_io.h"
#define PI 3.14159265358979323846
#define SLOTS 12
struct PhoneOutput {
    HWAVEOUT wave;HANDLE event;WAVEHDR headers[SLOTS];
    int16_t pcm[SLOTS][480];unsigned slot,fill;
    FILE *wav;int failed,paused;
};
static void put32(unsigned char *p,uint32_t n){unsigned i;for(i=0;i<4;++i)p[i]=(unsigned char)(n>>(i*8));}
static int header(PhoneIo *io){unsigned char b[44]={0};uint32_t bytes;
    if(io->frames>(UINT32_MAX-36u)/2u)return 0;bytes=(uint32_t)io->frames*2;
    memcpy(b,"RIFF",4);put32(b+4,bytes+36);memcpy(b+8,"WAVEfmt ",8);put32(b+16,16);
    b[20]=1;b[22]=1;put32(b+24,48000);put32(b+28,96000);b[32]=2;b[34]=16;
    memcpy(b+36,"data",4);put32(b+40,bytes);
    return !fseek(io->output->wav,0,SEEK_SET)&&fwrite(b,1,44,io->output->wav)==44;
}
/* Flash ROM has a device callback even in array mode. bus_peek deliberately
   rejects devices; the verified constant table may also be read from a fast
   array-mode ROM without issuing extra guest/device accesses. */
static int table_word(Bus *bus,uint32_t address,uint32_t *value)
{
    unsigned i;if(bus_peek(bus,address,2,value))return 1;
    for(i=0;i<bus->count;++i){Region *r=&bus->regions[i];uint64_t end=(uint64_t)address+2;
        if(r->readonly&&r->data&&r->fast&&address>=r->base&&end<=(uint64_t)r->base+r->size){
            uint16_t word;memcpy(&word,r->data+address-r->base,2);*value=word;return 1;}}
    return 0;
}
void phone_io_observe(PhoneIo *io,Arm7 *c){unsigned i;uint32_t note[2],v,a;
    if(!io->configured||!(c->cpsr&ARM_T))return;
    if(!io->hardware_led&&c->r[15]==io->gpio_entry&&c->r[1]<=1){
        for(i=0;i<3;++i)if(c->r[0]==io->led_pins[i]){unsigned old=io->led;
            io->led_tick=c->bus->ticks;
            io->led=(io->led&~(1u<<i))|(c->r[1]<<i);if(old!=io->led)++io->led_changes;}return;
    }
    if(c->r[15]==io->tone_stop){io->amplitude[0]=io->amplitude[1]=0;++io->tone_changes;return;}
    if(c->r[15]!=io->tone_entry)return;
    if(!bus_peek(c->bus,c->r[0],1,&note[0])||!bus_peek(c->bus,c->r[1],1,&note[1])||note[0]>=51||note[1]>=51)return;
    for(i=0;i<2;++i){
        if(!note[i]){io->amplitude[i]=0;continue;}
        if(!table_word(c->bus,io->tone_table+note[i]*4,&v)||
           !table_word(c->bus,io->tone_table+note[i]*4+2,&a))continue;
        /* DSP second-order oscillator coefficient: Q15 cos(2*pi*f/8000).
           The companion word is the oscillator excitation/amplitude. */
        io->step[i]=acos((double)(int16_t)v/32768.0)*8000.0/48000.0;
        io->amplitude[i]=(double)(int16_t)a*4.0;
    }
    ++io->tone_changes;
}
void phone_io_gpio_write(void *context,unsigned mask,unsigned value,uint64_t ticks)
{
    PhoneIo *io=context;unsigned i,relevant=0,state=0;
    for(i=0;i<3;++i){unsigned pin=1u<<io->led_pins[i];
        relevant|=pin;if(value&pin)state|=1u<<i;}
    if(!(mask&relevant))return;
    io->led_tick=ticks;if(io->led!=state)++io->led_changes;io->led=state;
}
int phone_io_open(PhoneIo *io,int speakers,const char *wav){PhoneOutput *o;WAVEFORMATEX fmt={0};
    if(!io->configured)return 1;phone_io_close(io);o=calloc(1,sizeof(*o));if(!o)return 0;io->output=o;
    io->frames=io->nonzero=io->fraction=io->last_ns=0;
    if(wav){o->wav=fopen(wav,"wb");if(!o->wav||!header(io))goto failed;}
    if(speakers){fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;fmt.nSamplesPerSec=48000;
        fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=96000;
        o->event=CreateEventA(NULL,FALSE,FALSE,NULL);if(!o->event)goto failed;
        if(waveOutOpen(&o->wave,WAVE_MAPPER,&fmt,(DWORD_PTR)o->event,0,CALLBACK_EVENT)!=MMSYSERR_NOERROR)goto failed;}
    return 1;
failed:phone_io_close(io);return 0;
}
void phone_io_pause(PhoneIo *io,int pause){PhoneOutput *o=io->output;if(!o)return;
    if(o->paused==!!pause)return;o->paused=!!pause;if(o->wave){if(pause)waveOutPause(o->wave);else waveOutRestart(o->wave);}}
static int flush(PhoneIo *io){PhoneOutput *o=io->output;WAVEHDR *h=&o->headers[o->slot];
    if(o->wav&&fwrite(o->pcm[o->slot],2,o->fill,o->wav)!=o->fill)return 0;
    if(o->wave){memset(h,0,sizeof(*h));h->lpData=(LPSTR)o->pcm[o->slot];h->dwBufferLength=o->fill*2;
        if(waveOutPrepareHeader(o->wave,h,sizeof(*h))!=MMSYSERR_NOERROR||waveOutWrite(o->wave,h,sizeof(*h))!=MMSYSERR_NOERROR)return 0;}
    o->fill=0;o->slot=(o->slot+1)%SLOTS;h=&o->headers[o->slot];
    if(o->wave&&(h->dwFlags&WHDR_PREPARED)){
        while(!(h->dwFlags&WHDR_DONE))if(WaitForSingleObject(o->event,2000)==WAIT_TIMEOUT)return 0;
        if(waveOutUnprepareHeader(o->wave,h,sizeof(*h))!=MMSYSERR_NOERROR)return 0;}
    return 1;
}
static int16_t next_sample(PhoneIo *io)
{
    double value=0;int sample;unsigned i;
    /* Most song playback has no CPU tone. Avoid two transcendental calls per
       sample for an entirely silent channel. Snap the inaudible release tail. */
    for(i=0;i<2;++i){
        if(!io->amplitude[i]&&fabs(io->render_amplitude[i])<0.01){io->render_amplitude[i]=0;continue;}
        io->render_amplitude[i]+=(io->amplitude[i]-io->render_amplitude[i])*0.02;
        value+=sin(io->phase[i])*io->render_amplitude[i];io->phase[i]+=io->step[i];
        if(io->phase[i]>=2*PI)io->phase[i]-=2*PI;
    }
    sample=(int)value;if(sample>32767)sample=32767;if(sample< -32768)sample=-32768;
    ++io->frames;if(sample)++io->nonzero;return (int16_t)sample;
}
static void output_sample(PhoneIo *io,int16_t sample)
{
    PhoneOutput *o=io->output;
    o->pcm[o->slot][o->fill++]=sample;
    if(o->fill==480&&!flush(io))o->failed=1;
}
/* Runs on the MA worker while the emulation caller is synchronously waiting.
   Thus tone parameters cannot change concurrently, and one output queue is
   the clock for both CPU and Yamaha sound. No DLL call is made by this mixer. */
void phone_io_mix(void *context,int16_t *left,int16_t *right,unsigned frames)
{
    PhoneIo *io=context;unsigned i;if(!io->output||io->output->failed)return;
    for(i=0;i<frames;++i){int16_t sample=next_sample(io);int l=left[i]+sample,r=right[i]+sample;
        left[i]=(int16_t)(l>32767?32767:l< -32768?-32768:l);
        right[i]=(int16_t)(r>32767?32767:r< -32768?-32768:r);
        output_sample(io,sample);}
}
void phone_io_advance(PhoneIo *io,uint64_t ns)
{
    PhoneOutput *o=io->output;uint64_t count;
    if(!o||io->mixed||o->failed||ns<io->last_ns)return;
    if(ns-io->last_ns<1000000)return;
    count=io->fraction+(ns-io->last_ns)*48000;io->last_ns=ns;
    io->fraction=count%1000000000;count/=1000000000;
    while(count--&&!o->failed)output_sample(io,next_sample(io));
}
void phone_io_close(PhoneIo *io){PhoneOutput *o=io->output;unsigned i;if(!o)return;
    if(o->wav&&o->fill)fwrite(o->pcm[o->slot],2,o->fill,o->wav);
    if(o->wave){waveOutReset(o->wave);for(i=0;i<SLOTS;++i)if(o->headers[i].dwFlags&WHDR_PREPARED)
        waveOutUnprepareHeader(o->wave,&o->headers[i],sizeof(WAVEHDR));waveOutClose(o->wave);}
    if(o->event)CloseHandle(o->event);if(o->wav){header(io);fclose(o->wav);}free(o);io->output=NULL;
}
