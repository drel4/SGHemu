#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "host.h"
int host_sync_rtc(PhoneRtc *rtc)
{
    SYSTEMTIME now;RtcDate date;GetLocalTime(&now);
    date.year=now.wYear;date.month=now.wMonth;date.day=now.wDay;
    date.hour=now.wHour;date.minute=now.wMinute;date.second=now.wSecond;
    return rtc_set_host_time(rtc,&date);
}
int host_high_priority(int enabled)
{
    return SetPriorityClass(GetCurrentProcess(),enabled?HIGH_PRIORITY_CLASS:NORMAL_PRIORITY_CLASS)!=0;
}
