#include "dsp_mailbox.h"
#include <stdlib.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"DSP mailbox line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void request(Bus *b,unsigned mux)
{
    static const uint16_t tail[]={0x09c2,0x52e4,0};unsigned i;
    bus_write(b,0x60000,2,6);bus_write(b,0x60002,2,0x0202);
    bus_write(b,0x60004,2,0x4814);bus_write(b,0x60006,2,mux);
    for(i=0;i<3;++i)bus_write(b,0x60008+2*i,2,tail[i]);
    bus_write(b,0x40910,2,0x11);
}
int main(void)
{
    Bus b={0};DspMailbox d={0},failed={0};unsigned before;
    CHECK(!dsp_mailbox_attach(&d,&b,0x60001,0x40910));
    CHECK(dsp_mailbox_attach(&d,&b,0x60000,0x40910));
    CHECK(!dsp_mailbox_attach(&d,&b,0x60000,0x40910));
    before=b.count;CHECK(!dsp_mailbox_attach(&failed,&b,0x80000,0x40910));
    CHECK(b.count==before&&!failed.attached); /* rollback first mapping */
    dsp_mailbox_inputs(&d,115,20);request(&b,0x800);
    CHECK(d.transactions==1&&d.conversions==1&&bus_read(&b,0x602bc,2)==0);
    CHECK(bus_read(&b,0x40910,2)==0); /* completed handshake, not busy forever */
    request(&b,0xa0);CHECK(d.conversions==2&&bus_read(&b,0x602bc,2)==3);
    CHECK(bus_read(&b,0x602be,2)==0); /* response offset, NOT its word count */
    CHECK(bus_read(&b,0x602c0,2)==0x09c2&&bus_read(&b,0x602c2,2)==0x52e4);
    CHECK(bus_read(&b,0x602c4,2)==115); /* battery, not the following temperature */
    dsp_mailbox_inputs(&d,138,20);request(&b,0xe0);
    CHECK(bus_read(&b,0x602c4,2)==20); /* staged result remains immutable */
    request(&b,0x800);CHECK(bus_read(&b,0x602c4,2)==0);
    bus_write(&b,0x60000,2,0);bus_write(&b,0x40910,1,1);
    CHECK(bus_read(&b,0x602c4,2)==138&&bus_read(&b,0x602bc,2)==3);
    bus_write(&b,0x40910,2,1);CHECK(bus_read(&b,0x602bc,2)==0);
    bus_write(&b,0x60000,2,2);bus_write(&b,0x60002,2,0x0d03);bus_write(&b,0x40910,2,1);
    CHECK(b.fault&&d.malformed==1&&!d.pending_count);b.fault=0;
    bus_write(&b,0x60000,2,350);bus_write(&b,0x40910,2,1);CHECK(b.fault&&d.malformed==2);b.fault=0;
    bus_write(&b,0x60000,2,1);bus_write(&b,0x60002,2,0x2600);bus_write(&b,0x40910,2,1);
    CHECK(d.unsupported==1&&!b.fault&&!d.pending_count);
    bus_read(&b,0x40910,4);CHECK(b.fault);b.fault=0;
    bus_write(&b,0x607fe,2,0x1234);CHECK(bus_read(&b,0x607ff,1)==0x12);
    bus_destroy(&b);printf("DSP mailbox: %u checks passed\n",checks);return 0;
}
