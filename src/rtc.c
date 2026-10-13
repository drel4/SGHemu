#include "rtc.h"
#include <string.h>
static unsigned days(unsigned y,unsigned m)
{
    static const unsigned length[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return length[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0));
}
static void update(PhoneRtc *r)
{
    uint64_t now=*r->nanoseconds,seconds,value,carry;RtcDate *d=&r->date;
    if(r->host_sync||now<r->last_ns){r->last_ns=now;return;}
    seconds=(now-r->last_ns)/1000000000u;if(!seconds)return;
    r->last_ns+=seconds*1000000000u;
    value=d->second+seconds;d->second=(unsigned)(value%60);carry=value/60;
    value=d->minute+carry;d->minute=(unsigned)(value%60);carry=value/60;
    value=d->hour+carry;d->hour=(unsigned)(value%24);carry=value/24;
    while(carry){unsigned length=days(d->year,d->month),left=d->day<length?length-d->day:0;
        if(carry<=left){d->day+=(unsigned)carry;break;}
        carry-=left+1;d->day=1;
        if(++d->month==13){d->month=1;if(++d->year>r->year_base+31)d->year=r->year_base;}}
}
static uint16_t value(PhoneRtc *r,unsigned i)
{
    switch(i){case 0:return (uint16_t)r->date.second;case 1:return (uint16_t)r->date.minute;
    case 2:return (uint16_t)r->date.hour;case 3:return (uint16_t)(r->date.day-1);
    case 4:return (uint16_t)(r->date.month-1);case 5:return (uint16_t)(r->date.year-r->year_base);
    default:return r->registers[i];}
}
static int valid(PhoneRtc *r,uint32_t offset,unsigned width,int write,uint32_t v)
{
    if((width!=1&&width!=2&&width!=4)||offset>=32||width>32-offset){
        bus_fault_access(r->bus,write,r->base+offset,width,v,"invalid RTC access");return 0;}return 1;
}
static uint32_t read_rtc(void *ctx,uint32_t offset,unsigned width)
{
    PhoneRtc *r=ctx;uint32_t v=0;unsigned i;
    if(!valid(r,offset,width,0,0))return 0;update(r);
    for(i=0;i<width;++i)v|=(uint32_t)((value(r,(offset+i)/2)>>(((offset+i)&1)*8))&255u)<<(i*8);
    return v;
}
static void write_rtc(void *ctx,uint32_t offset,unsigned width,uint32_t v)
{
    PhoneRtc *r=ctx;unsigned i;RtcDate *d=&r->date;
    if(!valid(r,offset,width,1,v))return;update(r);
    for(i=0;i<width;++i){unsigned at=offset+i,index=at/2;uint16_t byte=(uint16_t)((v>>(i*8))&255u);
        if(index<6){if(r->host_sync||(at&1)||!(byte&1))continue;
            /* RTCWrite increments each independent counter by writing one. */
            switch(index){case 0:d->second=(d->second+1)%60;break;case 1:d->minute=(d->minute+1)%60;break;
            case 2:d->hour=(d->hour+1)%24;break;case 3:d->day=d->day%31+1;break;
            case 4:d->month=d->month%12+1;break;default:d->year=r->year_base+(d->year-r->year_base+1)%32;break;}
        }else {unsigned shift=(at&1)*8;r->registers[index]=(uint16_t)((r->registers[index]&~(255u<<shift))|(byte<<shift));
            if(index==6&&!(at&1)&&byte==0&&!r->host_sync){
                d->year=r->year_base;d->month=d->day=1;d->hour=d->minute=d->second=0;r->last_ns=*r->nanoseconds;}}
    }
}
int rtc_attach(PhoneRtc *r,Bus *bus,uint32_t base,unsigned year_base,const uint64_t *ns)
{
    if(r->attached||(base&1)||year_base<1||year_base>9968||!ns)return 0;
    memset(r,0,sizeof(*r));r->bus=bus;r->base=base;r->year_base=year_base;r->nanoseconds=ns;
    r->last_ns=*ns;r->date.year=year_base;r->date.month=r->date.day=1;
    r->registers[9]=r->registers[10]=r->registers[11]=r->registers[12]=r->registers[13]=0xffff;
    if(!bus_device(bus,base,32,read_rtc,write_rtc,r))return 0;r->attached=1;return 1;
}
int rtc_set_host_time(PhoneRtc *r,const RtcDate *d)
{
    if(!r->attached||d->year<r->year_base||d->year>r->year_base+31||!d->month||d->month>12||
       !d->day||d->day>days(d->year,d->month)||d->hour>23||d->minute>59||d->second>59)return 0;
    r->date=*d;r->last_ns=*r->nanoseconds;return 1;
}
void rtc_set_sync(PhoneRtc *r,int enabled)
{
    if(!r->attached)return;update(r);r->host_sync=!!enabled;r->last_ns=*r->nanoseconds;
}
