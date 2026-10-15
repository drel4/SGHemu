#include "lp_timer.h"
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"LP timer line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    Bus b={0};Intc intc={0};LpTimer d={0};uint64_t ns=0,due;Arm7 cpu={0};
    lp_timer_advance(&d);CHECK(!d.attached);
    CHECK(intc_attach(&intc,&b,0x40100));
    CHECK(!lp_timer_attach(&d,&b,&intc,&ns,0x40700,0,4,25,1000000));
    CHECK(!lp_timer_attach(&d,&b,&intc,&ns,0x40700,32768,4,4,1000000));
    CHECK(lp_timer_attach(&d,&b,&intc,&ns,0x40700,32768,4,25,1000000));
    CHECK(!lp_timer_attach(&d,&b,&intc,&ns,0x40700,32768,4,25,1000000));
    lp_timer_advance(&d);CHECK(d.next_event==UINT64_MAX&&!intc.pending);
    ns=1000000000;bus_write(&b,0x40700,2,0);CHECK(bus_read(&b,0x40704,4)==32768);
    ns+=500000000;CHECK(bus_read(&b,0x40704,4)==32768); /* coherent latched snapshot */
    bus_write(&b,0x40700,1,0);CHECK(bus_read(&b,0x40704,4)==49152);
    intc.enabled=1u<<4;lp_timer_advance(&d);due=d.next_cal;
    ns=due-1;lp_timer_advance(&d);CHECK(!intc.pending);
    ns=due;lp_timer_advance(&d);CHECK(intc.pending==(1u<<4)&&d.cal_events==1);
    intc.pending=0;lp_timer_advance(&d);CHECK(!intc.pending&&d.cal_events==1);
    ns+=5000000;lp_timer_advance(&d);CHECK(d.cal_events==2&&d.next_cal>ns);
    intc.enabled=0;lp_timer_advance(&d);CHECK(d.next_event==UINT64_MAX);intc.pending=0;
    /* Comparator wraps at 18 bits; it is independent of ARM cycle counts. */
    ns=7999900000;bus_write(&b,0x40700,2,0);CHECK(bus_read(&b,0x40704,4)==262140);
    bus_write(&b,0x40700,2,2);bus_write(&b,0x40704,4,2);CHECK(d.armed);
    due=d.wake_due;CHECK(due>ns&&due<ns+1000000);
    ns=due-1;lp_timer_advance(&d);CHECK(!intc.pending);
    ns=due;lp_timer_advance(&d);CHECK(!d.armed&&intc.pending==(1u<<25)&&d.wake_events==1);
    intc_advance_time(&intc,&cpu,ns);CHECK(!cpu.irq);intc.enabled=1u<<25;
    intc_advance_time(&intc,&cpu,ns);CHECK(cpu.irq);bus_write(&b,0x40100,4,1u<<25);
    lp_timer_advance(&d);CHECK(!intc.pending&&d.wake_events==1);
    bus_write(&b,0x40700,2,2);bus_write(&b,0x40704,4,1000);CHECK(d.armed);
    due=d.wake_due;bus_write(&b,0x40700,2,0);ns=due+1;lp_timer_advance(&d);
    CHECK(!d.armed&&!intc.pending&&d.wake_events==1); /* cancellation */
    (void)bus_read(&b,0x40707,2);CHECK(b.fault);b.fault=0;
    bus_destroy(&b);printf("Low-power timer: %u checks passed.\n",checks);return 0;
}
