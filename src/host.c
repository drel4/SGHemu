#include "host.h"
#include "port.h"

int host_sync_rtc(PhoneRtc *rtc)
{
    PlatDateTime now;
    RtcDate date;

    if (!plat_local_time(&now))
        return 0;
    date.year = now.year;
    date.month = now.month;
    date.day = now.day;
    date.hour = now.hour;
    date.minute = now.minute;
    date.second = now.second;
    return rtc_set_host_time(rtc, &date);
}

int host_high_priority(int enabled)
{
    return plat_set_high_priority(enabled);
}
