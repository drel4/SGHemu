#ifndef SGH_HOST_H
#define SGH_HOST_H
#include "rtc.h"
int host_sync_rtc(PhoneRtc *rtc);
int host_high_priority(int enabled);
#endif
