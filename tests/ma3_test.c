#include "ma3.h"
#include <stdlib.h>
#include <string.h>
/* Protocol tests use a recording hardware transport, not proprietary DLLs. */
struct Ma5Hw { unsigned unused; };
static struct Ma5Hw mock;
static unsigned checks, count, ports[4096], values[4096], frames, irq, pumps;
static unsigned parameter_address, parameter_value, registers[64];
static unsigned memory_address,memory_full,memory_value;
static unsigned native_selector,native_low,native_high;
static unsigned sample_cursor,sample_voice;
static unsigned changed_address,changed_length,led_token;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"MA-3 line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
Ma5Hw *ma5_hw_open(const char *d,const char *w,int s,char *e,unsigned n)
{ (void)d;(void)w;(void)s;(void)e;(void)n;return &mock; }
void ma5_hw_close(Ma5Hw *h){(void)h;}
void ma5_hw_ram_changed(Ma5Hw *h,unsigned address,unsigned length){(void)h;changed_address=address;changed_length=length;}
unsigned ma5_hw_read(Ma5Hw *h,int data){(void)h;return data?
    native_selector==0?native_low:native_selector==0x10?native_high:0:0;}
void ma5_hw_write(Ma5Hw *h,int data,unsigned value)
{(void)h;if(!data)native_selector=value&63;CHECK(count<4096);ports[count]=(unsigned)data;values[count++]=value;}
void ma5_hw_fifo(Ma5Hw *h,unsigned selector,const uint8_t *bytes,unsigned length)
{unsigned i;ma5_hw_write(h,0,selector);for(i=0;i<length;++i)ma5_hw_write(h,1,bytes[i]);}
void ma5_hw_parameter(Ma5Hw *h,unsigned a,unsigned v){(void)h;parameter_address=a;parameter_value=v;}
unsigned ma5_hw_memory(Ma5Hw *h,unsigned a,int full){(void)h;memory_address=a;memory_full=(unsigned)full;return memory_value;}
unsigned ma5_hw_pcm_cursor(Ma5Hw *h,unsigned voice){(void)h;sample_voice=voice;return sample_cursor;}
void ma5_hw_register(Ma5Hw *h,unsigned r,unsigned v,unsigned bank){(void)h;(void)bank;CHECK(r<64);registers[r]=v;}
int ma5_hw_generate(Ma5Hw *h,unsigned n){(void)h;frames+=n;return 1;}
unsigned ma5_hw_led_token(Ma5Hw *h){(void)h;return led_token;}
void ma5_hw_pump(Ma5Hw *h){(void)h;++pumps;}
int ma5_hw_irq(Ma5Hw *h){unsigned result=irq;(void)h;irq=0;return (int)result;}
static Bus bus;
static void ss(unsigned v){bus_write(&bus,0x200000,1,v);}
static void data(unsigned v){bus_write(&bus,0x200004,1,v);}
static unsigned read_parameter(unsigned a)
{
    ss(3);data(a&127);data((a>>7)|128);data(0x82);
    CHECK(bus_read(&bus,0x200000,1)&0x40);
    ss(0x43);return bus_read(&bus,0x200004,1);
}
int main(void)
{
    Ma3 m={0};Intc intc={0};unsigned i;
    intc.attached=1;
    CHECK(!ma3_attach(&m,&bus,&intc,0x200000,0x800000,0));
    CHECK(ma3_attach(&m,&bus,&intc,0x200000,0x800000,1157));
    CHECK(!ma3_attach(&m,&bus,&intc,0x200000,1,1157));
    CHECK(!ma3_enable(&m,"mock",NULL,0,47999));
    CHECK(ma3_enable(&m,"mock",NULL,0,48000));
    CHECK(registers[0x38]==1); /* DLL FM engine must be armed for 32 voices. */
    CHECK(read_parameter(0x90)==0x60);CHECK(read_parameter(0xa0)==0x3c);
    CHECK(read_parameter(0x14a)==0x40);
    ss(2);data(0x10);data(0x81);data(0xe4);
    CHECK(parameter_address==0xc0);CHECK(parameter_value==0xe4);
    CHECK(read_parameter(0x90)==0x64);
    ss(4);registers[0x38]=0;data(0x80);data(0);CHECK(read_parameter(0x90)==0x60);
    CHECK(registers[0x38]==1); /* The raw reset rebuilds the engine, so 32-voice mode must be re-armed after it. */
    /* Firmware 1B,00 is a 3456-unit divider, not native register 1B00/4. */
    ss(2);data(0x5e);data(0x82);data(0x1b);data(0x80);
    CHECK(registers[0x23]==0x39);CHECK(registers[0x24]==0x94);
    /* Explicit sequence reset rebases time, unlike ordinary pause/resume. */
    ss(2);data(0x61);data(0x82);data(0x81);CHECK(registers[0x26]==1);
    CHECK(pumps==1&&frames==0); /* Startup commands run without advancing PCM time. */
    ss(4);data(8);CHECK(registers[0x26]==3);
    registers[0x26]=99;data(8);CHECK(registers[0x26]==99);
    data(0);CHECK(registers[0x26]==99);data(8);CHECK(registers[0x26]==3);
    data(0);ss(2);data(0x61);data(0x82);data(0x80);CHECK(registers[0x26]==0);
    ss(4);data(0x28);CHECK(registers[0x26]==2);data(0);
    ss(2);data(0x61);data(0x82);data(0x81);CHECK(registers[0x26]==1);
    ss(4);data(0x20);CHECK(registers[0x26]==1);data(0);
    ss(2);data(0x61);data(0x82);data(0x80);
    /* A split FIFO packet must preserve its delta and note gate/channel byte. */
    count=0;ss(1);data(0x85);data(0x80);data(8);data(0);data(0x34);
    CHECK(count==1);ss(0x81);data(0x2c);data(0x4a);data(0xc0);
    CHECK(m.delayed_length==0);CHECK(values[count-1]==0xc0);
    CHECK(ports[2]==0 && values[2]==1);CHECK(values[3]==0x85);
    CHECK(values[count-3]==0x25);CHECK(values[count-2]==0x4a);
    /* A4: MA-3 BLOCK=4, FNUM=600 -> native high=0x24, low=0x58.
       C5: BLOCK=4, FNUM=714 -> native high=0x25, low=0x4a. */
    ss(2);data(0x83);data(0xa4);CHECK(parameter_address==3);CHECK(parameter_value==0xa4);
    CHECK(read_parameter(3)==0x24); /* Guest sees its original encoding. */
    memory_value=0x25;ss(3);data(0x83);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0x2c);CHECK(memory_address==3&&!memory_full);
    memory_value=0xab;ss(3);data(0);data(0);data(0x81);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0xab);CHECK(memory_address==0x4000&&memory_full);
    ss(2);data(0x83);data(0xec);CHECK(parameter_value==0xe5); /* Flags preserved. */
    ss(2);data(0x69);data(0x81);data(0xac);CHECK(parameter_address==0x119);CHECK(parameter_value==0xa5);
    /* GetWtBlockFnum packs PCM pitch in the same bit order. */
    ss(2);data(0xe3);data(0xac);CHECK(parameter_address==0x63);CHECK(parameter_value==0xa5);
    count=0;ss(1);data(0x80);data(0x63);data(0x81);data(0xec);
    CHECK(values[count-1]==0xe5); /* Second FM bank, pitch-only packet. */
    count=0;ss(1);data(0x80);data(0xe3);data(0xac);CHECK(values[count-1]==0xa5);
    /* Literal full-RAM data at an address resembling a pitch register. */
    count=0;ss(1);data(0x80);data(3);data(0);data(0x80);data(0x81);data(0xac);
    CHECK(values[count-1]==0xac);
    ss(2);data(0x20);data(0);data(0x81);data(0x83);data(1);data(2);
    CHECK(changed_address==0&&changed_length==0); /* No notification before packet completes. */
    data(3);CHECK(changed_address==0x4020&&changed_length==3);
    /* Stream-start payloads retain their delta, channel and live readback. */
    count=0;ss(1);data(0x89);data(0x5a);data(0x82);data(0xc3);
    CHECK(values[2]==0x89);CHECK(values[3]==0x56&&values[4]==0x83);
    CHECK(values[5]==0xc3);
    count=0;ss(1);data(0x80);data(0x5b);data(0x82);data(0xe4);
    CHECK(values[3]==0x57&&values[4]==0x83);CHECK(values[5]==0xe4);
    count=0;ss(1);data(0x80);data(0x5c);data(0x82);data(0xff);
    CHECK(values[3]==0x58&&values[4]==0x83);CHECK(values[5]==0xff);
    memory_value=0x43;ss(3);data(0x5a);data(0x82);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0x43);CHECK(memory_address==0x1d6&&!memory_full);
    ss(2);data(0x62);data(0x82);data(0xff);
    CHECK(registers[0x11]==0xbc);CHECK(registers[0x12]==0x0c);
    native_low=0x8c;native_high=0;ss(0);
    CHECK(bus_read(&bus,0x200004,1)==0x83); /* Streams 0/1, FIFO. */
    native_low=0x30;native_high=0x0c;
    CHECK(bus_read(&bus,0x200004,1)==0x6c); /* Timers, upper soft IRQs. */
    data(0x6f);CHECK(registers[0x10]==0x0c);CHECK(values[count-1]==0x3c);
    native_low=native_high=0;
    ss(2);data(0x52);data(0x82);data(0x9b);CHECK(registers[0x2e]==27);
    ss(2);data(0x54);data(0x82);data(0x94);CHECK(registers[0x2f]==20);
    ss(2);data(0x55);data(0x82);data(0x81);CHECK(registers[0x30]==1);
    ss(2);data(0x55);data(0x82);data(0x80);CHECK(registers[0x30]==0);
    sample_cursor=256;ss(3);data(0x51);data(0x82);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0x4a);CHECK(sample_voice==7);
    sample_cursor=(1u<<24)|256;ss(3);data(0x50);data(0x82);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0x62);CHECK(sample_voice==6);
    sample_cursor=(2u<<24)|256;ss(3);data(0x4a);data(0x82);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0x49);CHECK(sample_voice==0);
    sample_cursor=1984;ss(3);data(0x51);data(0x82);data(0x80);ss(0x11);
    CHECK(bus_read(&bus,0x200004,1)==0x40); /* Ring-buffer wrap, not linear end. */
    /* Unsupported delayed controls must not discard their time delta. */
    count=0;ss(1);data(0x87);data(0x47);data(0x82);data(0x80);
    CHECK(m.unsupported==1);CHECK(values[2]==0x87);
    CHECK(values[3]==0x5f && values[4]==0x83 && values[5]==0x80);
    /* LED commands retain their delta and update only when the DLL sequencer
       executes the private stored marker. No color is carried in the MMF. */
    count=0;ss(1);data(0x87);data(0x46);data(0x82);data(0x81);
    CHECK(m.unsupported==1&&m.led_on==0);
    CHECK(values[2]==0x87&&values[3]==0x5e&&values[4]==0x83&&values[5]==0x83);
    led_token=3;bus.ticks=17;ma3_advance(&m,478);
    CHECK(m.led_on==1&&m.led_tick==17&&m.led_events==1);
    CHECK(m.led_color==1||m.led_color==2||m.led_color==3||m.led_color==4);
    led_token=4;bus.ticks=18;
    ma3_advance(&m,479);CHECK(frames==0);ma3_advance(&m,480);CHECK(frames==480);
    irq=1;ma3_advance(&m,960);CHECK(frames==960);CHECK(intc.pending==0x800000);
    CHECK(m.irq_events==1);ma3_advance(&m,961);CHECK(m.irq_events==1);
    ss(4);data(0);ss(7);data(31);CHECK(registers[0x27]==0x7a);CHECK(registers[0x29]==0x7a);
    data(0);CHECK(registers[0x27]==0);CHECK(registers[0x29]==0);
    bus_write(&bus,0x200004,2,1);CHECK(bus.fault);bus.fault=0;
    ss(1);for(i=0;i<1025;++i)data(0);CHECK(*m.error);
    *m.error=0;ss(2);data(0);data(0);data(0);data(0);
    CHECK(strstr(m.error,"immediate address")!=NULL);
    /* Clearing the FIFO discards a half-received packet instead of gluing the
       next tune's first packet onto it. */
    {unsigned good[16],good_count,i2;uint64_t drops;
        /* The bound-overflow test above leaves the delayed parser full; start clean. */
        *m.error=0;m.delayed_phase=m.delayed_length=m.delayed_address=m.delayed_count=m.delayed_count_bytes=0;drops=m.partial_drops;
        count=0;ss(1);data(0x80);data(0x63);data(0x81);data(0xec);
        good_count=count;CHECK(good_count>0&&good_count<=16);for(i2=0;i2<good_count;++i2)good[i2]=values[i2];
        ss(1);data(0x80);data(0x63);  /* interrupted mid-address */
        CHECK(m.delayed_phase!=0);
        ss(4);data(0x28);data(0);     /* ClearFifo pulse */
        CHECK(m.delayed_phase==0&&m.delayed_length==0);CHECK(m.partial_drops==drops+1);
        count=0;ss(1);data(0x80);data(0x63);data(0x81);data(0xec);
        CHECK(count==good_count);for(i2=0;i2<good_count;++i2)CHECK(values[i2]==good[i2]);
        ss(4);data(0x20);data(0);CHECK(m.partial_drops==drops+1); /* idle clear: nothing dropped */}
    /* Short generation blocks: same total time, finer chip/CPU interleaving. */
    CHECK(!ma3_set_block(&m,100));CHECK(!ma3_set_block(&m,8));CHECK(!ma3_set_block(&m,0));CHECK(ma3_set_block(&m,96));
    {uint64_t t;unsigned before=frames;m.pending_frames=0;m.fractional=0;t=m.last_tick;
        ma3_advance(&m,t+95);CHECK(frames==before);
        ma3_advance(&m,t+96);CHECK(frames==before+96);
        ma3_advance(&m,t+480);CHECK(frames==before+480);}
    ma3_destroy(&m);CHECK(!m.hw);bus_destroy(&bus);
    printf("MA-3: %u checks passed\n",checks);return 0;
}
