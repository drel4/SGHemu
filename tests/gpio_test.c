#include "battery.h"
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"GPIO line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    Bus bus={0};GpioPort port={0};Intc intc={0};Battery battery={0};Arm7 cpu={0};unsigned mask,value;
    CHECK(intc_attach(&intc,&bus,0x1000));
    CHECK(!gpio_port_attach(&port,&bus,0x2001,0));
    CHECK(gpio_port_attach(&port,&bus,0x2000,4));
    CHECK(!gpio_port_attach(&port,&bus,0x2000,4));
    CHECK(bus_read(&bus,0x2000,2)==4);
    for(mask=0;mask<256;++mask)for(value=0;value<256;++value){unsigned old=port.output;
        bus_write(&bus,0x2000,2,(value<<8)|mask);
        CHECK(bus_read(&bus,0x2000,2)==((old&~mask)|(value&mask)));}
    CHECK(!gpio_port_input(&port,8,1));
    battery.charger_port=&port;battery.charger_intc=&intc;battery.charger_bit=2;battery.charger_irq=5;
    battery_set(&battery,25,0);CHECK((bus_read(&bus,0x2000,2)&4)&&!intc.pending);
    battery_set(&battery,25,1);CHECK(!(bus_read(&bus,0x2000,2)&4)&&intc.pending==32&&!battery.cable_pending);
    intc_advance_time(&intc,&cpu,0);CHECK(!cpu.irq);intc.enabled=32;
    intc_advance_time(&intc,&cpu,0);CHECK(cpu.irq);
    bus_write(&bus,0x1000,4,32);battery_set(&battery,25,1);CHECK(!intc.pending);
    bus_write(&bus,0x2000,2,0x0404);CHECK(!(bus_read(&bus,0x2000,2)&4)); /* external input wins */
    battery_set(&battery,25,0);CHECK((bus_read(&bus,0x2000,2)&4)&&intc.pending==32);
    bus_read(&bus,0x2000,1);CHECK(bus.fault);bus.fault=0;
    bus_write(&bus,0x2000,1,255);CHECK(bus.fault);
    {DspMailbox adc={0};uint32_t pc=0x12345678;
        CHECK(dsp_mailbox_attach(&adc,&bus,0x60000,0x40910));
        battery.adc=&adc;battery.adc_empty=107;battery.adc_full=138;battery.adc_temperature=20;
        battery.enabled=1;cpu.r[15]=pc;cpu.instructions=400000000;
        battery_set(&battery,0,0);CHECK(adc.battery_raw==107&&adc.temperature_raw==20&&!battery.level_pending);
        battery_set(&battery,25,0);CHECK(adc.battery_raw==115);
        battery_set(&battery,100,0);CHECK(adc.battery_raw==138);
        battery_set(&battery,200,0);CHECK(battery.percent==100&&adc.battery_raw==138);
        CHECK(battery_step(&battery,&cpu)&&cpu.r[15]==pc&&!battery.stage);
    }
    {GpioPort complete={0};Intc aux={0};Battery charger={0};uint64_t ns=0;
        bus.fault=0;intc.pending=intc.enabled=0;
        CHECK(gpio_port_attach(&complete,&bus,0x3000,0x80));CHECK(intc_attach(&aux,&bus,0x4000));intc.secondary=&aux;
        charger.complete_port=&complete;charger.complete_intc=&aux;charger.complete_bit=2;charger.enable_bit=7;
        charger.complete_irq=2;charger.complete_delay_ms=20000;charger.complete_due=UINT64_MAX;charger.nanoseconds=&ns;
        complete.output_changed=battery_charger_output;complete.output_context=&charger;
        battery_set(&charger,100,1);CHECK(charger.complete_due==UINT64_MAX);
        bus_write(&bus,0x3000,2,0x0080);CHECK(charger.complete_due==20000000000ull);
        ns=19999999999ull;battery_advance(&charger);CHECK(!charger.complete);
        ns=20000000000ull;battery_advance(&charger);CHECK(charger.complete&&aux.pending==4&&(bus_read(&bus,0x3000,2)&4));
        intc_advance_time(&intc,&cpu,ns);CHECK(!cpu.irq);aux.enabled=4;intc_advance_time(&intc,&cpu,ns);CHECK(cpu.irq);
        bus_write(&bus,0x3000,2,0x8080);CHECK(charger.complete); /* done survives charge disable */
        battery_set(&charger,75,1);CHECK(!charger.complete&&charger.complete_due==UINT64_MAX);
        battery_set(&charger,100,1);CHECK(charger.complete_due==UINT64_MAX);
        bus_write(&bus,0x3000,2,0x0080);CHECK(charger.complete_due==ns+20000000000ull);
        battery_set(&charger,100,0);CHECK(charger.complete_due==UINT64_MAX&&!charger.complete);
        ns+=30000000000ull;battery_advance(&charger);CHECK(!charger.complete);
        CHECK(!bus.fault);
    }
    bus_destroy(&bus);printf("GPIO: %u checks passed\n",checks);return 0;
}
