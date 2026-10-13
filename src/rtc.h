#ifndef SGH_RTC_H
#define SGH_RTC_H
#include "bus.h"
typedef struct { unsigned year,month,day,hour,minute,second; } RtcDate;
typedef struct {
    Bus *bus;const uint64_t *nanoseconds;uint64_t last_ns;
    uint32_t base;unsigned year_base;
    RtcDate date;uint16_t registers[16];
    int attached,host_sync;
} PhoneRtc;
int rtc_attach(PhoneRtc *rtc,Bus *bus,uint32_t base,unsigned year_base,const uint64_t *nanoseconds);
int rtc_set_host_time(PhoneRtc *rtc,const RtcDate *date);
void rtc_set_sync(PhoneRtc *rtc,int enabled);
#endif
