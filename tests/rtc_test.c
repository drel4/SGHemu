#include "rtc.h"
#include <stdio.h>
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"RTC test line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    Bus bus={0};PhoneRtc r={0};uint64_t ns=0;unsigned i;
    RtcDate date={2026,10,13,23,59,59};
    CHECK(rtc_attach(&r,&bus,0x40080,1996,&ns));CHECK(rtc_set_host_time(&r,&date));rtc_set_sync(&r,1);
    CHECK(bus_read(&bus,0x40080,2)==59);CHECK(bus_read(&bus,0x40082,2)==59);
    CHECK(bus_read(&bus,0x40084,2)==23);CHECK(bus_read(&bus,0x40086,2)==12);
    CHECK(bus_read(&bus,0x40088,2)==9);CHECK(bus_read(&bus,0x4008a,2)==30);
    CHECK(bus_read(&bus,0x40087,1)==0);CHECK(bus_read(&bus,0x40080,4)==(59u|(59u<<16)));
    /* Bootstrap reset and native SetTime cannot overwrite a host-synchronized RTC. */
    bus_write(&bus,0x4008c,2,0);bus_write(&bus,0x40080,2,1);
    CHECK(bus_read(&bus,0x40080,2)==59);ns=10000000000ULL;CHECK(bus_read(&bus,0x40080,2)==59);
    rtc_set_sync(&r,0);ns+=1000000000;CHECK(bus_read(&bus,0x40080,2)==0);
    CHECK(bus_read(&bus,0x40086,2)==13);CHECK(bus_read(&bus,0x40084,2)==0);
    date=(RtcDate){2000,2,28,23,59,59};CHECK(rtc_set_host_time(&r,&date));ns+=1000000000;
    CHECK(bus_read(&bus,0x40086,2)==28);ns+=86400000000000ULL;
    CHECK(bus_read(&bus,0x40086,2)==0);CHECK(bus_read(&bus,0x40088,2)==2);
    date=(RtcDate){2001,2,28,23,59,59};CHECK(rtc_set_host_time(&r,&date));ns+=1000000000;
    CHECK(bus_read(&bus,0x40086,2)==0);CHECK(bus_read(&bus,0x40088,2)==2);
    date=(RtcDate){2027,12,31,23,59,59};CHECK(rtc_set_host_time(&r,&date));ns+=1000000000;
    CHECK(bus_read(&bus,0x4008a,2)==0);CHECK(bus_read(&bus,0x40088,2)==0);
    /* Native RTCWrite resets, then increments independent calendar counters. */
    bus_write(&bus,0x4008c,2,0);for(i=0;i<30;++i)bus_write(&bus,0x4008a,2,1);
    for(i=0;i<9;++i)bus_write(&bus,0x40088,2,1);
    for(i=0;i<12;++i)bus_write(&bus,0x40086,2,1);
    CHECK(bus_read(&bus,0x4008a,2)==30);CHECK(bus_read(&bus,0x40088,2)==9);CHECK(bus_read(&bus,0x40086,2)==12);
    bus_write(&bus,0x40092,2,0xffff);CHECK(bus_read(&bus,0x40092,2)==0xffff);
    date=(RtcDate){2026,2,29,0,0,0};CHECK(!rtc_set_host_time(&r,&date));
    date=(RtcDate){2028,1,1,0,0,0};CHECK(!rtc_set_host_time(&r,&date));
    date=(RtcDate){2026,0,1,0,0,0};CHECK(!rtc_set_host_time(&r,&date));
    date=(RtcDate){2026,1,1,24,0,0};CHECK(!rtc_set_host_time(&r,&date));
    CHECK(!bus.fault);bus_destroy(&bus);printf("RTC calendar/registers/sync: %u checks passed.\n",checks);return 0;
}
