#include "cellular.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CELL_RETURN 0xffff0100u
static int disconnect_event(Cellular *s,unsigned cause);
static int error(Cellular *s,const char *message){snprintf(s->error,sizeof(s->error),"%s",message);return 0;}
static void word(uint8_t *p,uint32_t n){unsigned i;for(i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static int number_valid(const char *number)
{
    unsigned n=0;if(!number)return 0;if(*number=='+')++number;
    while(*number){if((*number<'0'||*number>'9')&&*number!='*'&&*number!='#')return 0;if(++n>30)return 0;++number;}
    return n!=0;
}
void cellular_init(Cellular *s){memset(s,0,sizeof(*s));strcpy(s->network,"Tele2");}
int cellular_queue(Cellular *s,uint32_t target,uint32_t message,const void *bytes,unsigned length)
{
    CellEvent *q;if(s->count==CELL_EVENTS||length>CELL_EVENT_BYTES||!bytes||!length||
        (target==CELL_CALLBACK&&(length!=8||!message||(message&1))))return error(s,"Cellular event queue is full or invalid");
    q=&s->events[(s->head+s->count)%CELL_EVENTS];memset(q,0,sizeof(*q));q->target=target;q->message=message;q->size=length;memcpy(q->data,bytes,length);++s->count;return 1;
}
static int release_later(Cellular *s,uint32_t pointer)
{
    CellEvent *q;if(!pointer)return 1;if(s->count==CELL_EVENTS)return 0;
    q=&s->events[(s->head+s->count)%CELL_EVENTS];memset(q,0,sizeof(*q));q->free_ptr=pointer;++s->count;return 1;
}
static void store_name(Cellular *s,Bus *b)
{
    unsigned i;if(!s->name_ptr)return;for(i=0;i<31;++i)bus_write(b,s->name_ptr+i,1,(uint8_t)s->network[i]);
}
int cellular_set_network(Cellular *s,Bus *bus,int enabled,const char *name)
{
    size_t n=name?strlen(name):0,i;
    if(!s->ready)return error(s,"This firmware has no cellular profile");
    if(!n||n>30)return error(s,"Network name must contain 1-30 printable ASCII characters");
    for(i=0;i<n;++i)if((unsigned char)name[i]<32||(unsigned char)name[i]>126)return error(s,"Network name must use printable ASCII characters");
    if(!enabled&&s->call_state){if(!disconnect_event(s,16))return 0;s->call_state=CELL_IDLE;++s->revision;}
    memset(s->network,0,sizeof(s->network));memcpy(s->network,name,n);s->enabled=!!enabled;s->network_pending=1;
    if(bus)store_name(s,bus);*s->error=0;return 1;
}
static int disconnect_event(Cellular *s,unsigned cause)
{
    uint8_t data[12]={0};data[0]=(uint8_t)s->call_id;data[1]=(uint8_t)(s->call_id>>8);data[4]=(uint8_t)cause;
    return cellular_queue(s,0,0x15e,data,sizeof(data));
}
int cellular_answer(Cellular *s,int accept)
{
    uint8_t data[12]={0};if(!s->call_state)return error(s,"No call is pending");
    if(accept&&s->call_state==CELL_FROM_PHONE){data[0]=(uint8_t)s->call_id;data[1]=(uint8_t)(s->call_id>>8);
        if(!cellular_queue(s,0,0x160,data,sizeof(data)))return 0;s->call_state=CELL_CONNECTED;
    }else{if(!disconnect_event(s,accept?16:21))return 0;s->call_state=CELL_IDLE;}
    ++s->revision;*s->error=0;return 1;
}
int cellular_call(Cellular *s,Bus *bus,const char *number)
{
    uint8_t data[0x84]={0};const char *digits=number;size_t n;unsigned i,service;
    if(!s->enabled||!s->name_ptr)return error(s,"Wait for the enabled cellular network to initialize");
    if(s->call_state)return error(s,"A call is already active");if(!number_valid(number))return error(s,"Enter a valid phone number");
    service=bus_read(bus,s->services,2);if(service==0xffff)return error(s,"Phone voice service is not initialized");
    s->call_id=8;data[0]=8;data[2]=(uint8_t)service;data[3]=(uint8_t)(service>>8);data[4]=0xff;data[5]=0;
    data[6]=0xff;data[7]=0;data[10]=1;data[12]=*digits=='+'?0x91:0x81;data[13]=0x80;
    if(*digits=='+')++digits;n=strlen(digits);data[14]=(uint8_t)((n+1)/2);
    for(i=0;i<n;i+=2){unsigned low=digits[i]=='*'?10:digits[i]=='#'?11:(unsigned)(digits[i]-'0');
        unsigned high=i+1==n?15:digits[i+1]=='*'?10:digits[i+1]=='#'?11:(unsigned)(digits[i+1]-'0');data[16+i/2]=(uint8_t)(low|(high<<4));}
    if(s->count>CELL_EVENTS-2)return error(s,"Cellular event queue is full");
    if(!cellular_queue(s,0,0x163,data,sizeof(data)))return 0;
    {uint8_t alert[4]={8,0,1,0};(void)cellular_queue(s,0,0x162,alert,sizeof(alert));}
    strcpy(s->number,number);s->call_state=CELL_TO_PHONE;s->outgoing=0;++s->revision;*s->error=0;return 1;
}
int cellular_sms(Cellular *s,const char *number,const uint16_t *text,unsigned length,const uint8_t scts[7])
{
    SimCard *temp;SimFile *sms=NULL;uint8_t data[0x10c]={0};unsigned i,n,slot;
    if(!s->enabled||!s->name_ptr||!s->sr_task)return error(s,"Wait for the enabled cellular network to initialize");
    temp=calloc(1,sizeof(*temp));if(!temp)return error(s,"Cannot prepare SMS");sim_card_init(temp);
    slot=sim_card_deliver_sms(temp,number,text,length,scts);
    if(!slot){snprintf(s->error,sizeof(s->error),"%s",temp->error);free(temp);return 0;}
    for(i=0;i<temp->count;++i)if(temp->files[i].id==0x6f3c)sms=&temp->files[i];
    if(!sms){free(temp);return error(s,"No SIM SMS file");}
    n=2+1+((unsigned)sms->data[3]+1)/2+2+7+1+length*2; /* Complete TPDU, no EF status/SCA. */
    data[3]=7;data[4]=0x91;data[10]=0xf1;data[0x1b]=(uint8_t)n;memcpy(data+0x1c,sms->data+2,n);free(temp);
    if(!cellular_queue(s,s->sr_task,0x1a0,data,sizeof(data)))return 0;++s->received_sms;*s->error=0;return 1;
}
static void restore(Cellular *s,Arm7 *c)
{
    uint64_t instructions=c->instructions,cycles=c->cycles,clocks=c->clock_cycles;*c=s->caller;
    c->instructions=instructions;c->cycles=cycles;c->clock_cycles=clocks;
    s->head=(s->head+1)%CELL_EVENTS;--s->count;s->stage=0;++s->delivered_events;
}
static int enter_event(Cellular *s,Arm7 *c,CellEvent *q)
{
    c->r[13]=(s->native_stack+CELL_NATIVE_STACK_BYTES)&~7u;c->r[14]=CELL_RETURN|1u;
    if(q->target==CELL_CALLBACK){
        c->r[0]=(uint32_t)q->data[0]|((uint32_t)q->data[1]<<8)|((uint32_t)q->data[2]<<16)|((uint32_t)q->data[3]<<24);
        c->r[1]=(uint32_t)q->data[4]|((uint32_t)q->data[5]<<8)|((uint32_t)q->data[6]<<16)|((uint32_t)q->data[7]<<24);
        c->r[15]=q->message;c->cpsr|=ARM_T;s->stage=4;return 1;
    }
    if(q->free_ptr){c->r[0]=q->free_ptr;c->r[15]=s->release;s->stage=4;return 1;}
    c->r[0]=s->name_ptr?q->size:64;c->r[15]=s->alloc;s->stage=s->name_ptr?2:1;return 1;
}
static int valid_stack(Bus *bus,uint32_t base)
{
    unsigned i;uint64_t end=(uint64_t)base+CELL_NATIVE_STACK_BYTES;
    if(!base||(base&3)||end>UINT32_MAX)return 0;
    for(i=0;i<bus->count;++i){Region *r=&bus->regions[i];
        if(r->data&&!r->readonly&&!r->read&&!r->write&&base>=r->base&&end<=(uint64_t)r->base+r->size)return 1;
    }return 0;
}
int cellular_advance(Cellular *s,Arm7 *c,int busy,int sim_present)
{
    CellEvent *q;unsigned i;
    if(!s->ready)return 1;
    if(s->network_pending&&!s->stage&&!busy&&(s->boot_ready||c->instructions>=300000000)&&sim_present){
        uint8_t data[20]={0};data[0]=s->enabled?6:1;word(data+4,0x00101f);data[8]=1;data[9]=0;
        data[10]=0;data[11]=0xf1;data[12]=0x10;data[14]=1;data[16]=1;
        if(s->enabled){uint8_t searching[20];memcpy(searching,data,sizeof(searching));searching[0]=2;
            if(s->count>CELL_EVENTS-3)return 1;
            if(!cellular_queue(s,0,0xb0,searching,sizeof(searching)))return 0;
        }
        if(!cellular_queue(s,0,0xb0,data,sizeof(data)))return 0;
        if(s->enabled){const uint8_t signal[2]={63,0};if(!cellular_queue(s,0,0x1c,signal,2))return 0;}s->network_pending=0;
    }
    if(!s->count)return 1;q=&s->events[s->head];
    if(!s->stage){
        if(busy||(!s->boot_ready&&c->instructions<300000000)||
            (c->fiq&&!(c->cpsr&0x40u))||(c->irq&&!(c->cpsr&0x80u))||
            (c->r[15]!=s->ready&&(!s->poll||c->r[15]!=s->poll))||!(c->cpsr&ARM_T)||
            (c->cpsr&31)==0x11||(c->cpsr&31)==0x12)return 1;
        s->caller=*c;c->r[14]=CELL_RETURN|1u;
        /* The idle CPU SP may be above a parked task's live frames. Deep WAP
           callbacks must not borrow that stack: they overwrite its saved PSR
           and return addresses. Allocate a guest-owned stack once per boot. */
        if(!s->native_stack){c->r[0]=CELL_NATIVE_STACK_BYTES;c->r[15]=s->alloc;s->stage=5;return 1;}
        return enter_event(s,c,q);
    }
    if(c->r[15]!=CELL_RETURN)return 1;
    if(s->stage==5){if(!valid_stack(c->bus,c->r[0]))return error(s,"Guest cannot allocate native-call stack");
        s->native_stack=c->r[0];return enter_event(s,c,q);}
    if(s->stage==1){if(!c->r[0])return error(s,"Guest cannot allocate network name");s->name_ptr=c->r[0];store_name(s,c->bus);
        c->r[0]=q->size;c->r[14]=CELL_RETURN|1;c->r[15]=s->alloc;s->stage=2;return 1;}
    if(s->stage==2){if(!c->r[0])return error(s,"Guest cannot allocate cellular event");s->data_ptr=c->r[0];
        for(i=0;i<q->size;++i)bus_write(c->bus,s->data_ptr+i,1,q->data[i]);
        if(c->bus->fault)return error(s,"Cellular event buffer is not writable");
        c->r[14]=CELL_RETURN|1;
        if(q->target){c->r[0]=q->target;c->r[1]=q->message;c->r[2]=s->data_ptr;c->r[15]=s->send_message;}
        else {c->r[0]=q->message;c->r[1]=s->data_ptr;c->r[15]=s->send_ui;}
        s->stage=3;return 1;
    }
    restore(s,c);return 1;
}
static int returned(Arm7 *c)
{
    uint32_t lr=c->r[14];c->cpsr=(c->cpsr&~ARM_T)|((lr&1)?ARM_T:0);c->r[15]=lr&((lr&1)?~1u:~3u);
    ++c->instructions;++c->cycles;return 1;
}
static void sms_capture(Cellular *s,Arm7 *c,uint32_t p)
{
    uint8_t data[256];unsigned n=bus_read(c->bus,p+0x1b,1),i,at,digits,octets,dcs,count;
    for(i=0;i<n;++i)data[i]=(uint8_t)bus_read(c->bus,p+0x1c+i,1);
    s->last_sms_number[0]=0;s->last_sms_length=0;
    if(n<7||(data[0]&3)!=1)return;digits=data[2];octets=(digits+1)/2;if(digits>30||4+octets+3>n)return;
    at=0;if((data[3]&0x70)==0x10)s->last_sms_number[at++]='+';
    for(i=0;i<digits;++i){unsigned digit=(data[4+i/2]>>((i&1)*4))&15;s->last_sms_number[at++]=(char)(digit<10?'0'+digit:digit==10?'*':'#');}
    s->last_sms_number[at]=0;at=4+octets;dcs=data[at+1];at+=2;
    switch((data[0]>>3)&3){case 2:at+=1;break;case 1:case 3:at+=7;break;default:break;}
    if(at>=n)return;count=data[at++];
    if((dcs&0x0c)==8){if(count>n-at)return;for(i=0;i+1<count&&s->last_sms_length<160;i+=2)s->last_sms_text[s->last_sms_length++]=(uint16_t)((data[at+i]<<8)|data[at+i+1]);}
    else if(!(dcs&0x0c)){for(i=0;i<count&&s->last_sms_length<160;++i){unsigned bit=i*7,index=at+bit/8,value;if(index>=n)break;
        value=data[index]>>(bit%8);if(bit%8>1&&index+1<n)value|=(unsigned)data[index+1]<<(8-bit%8);value&=127;
        s->last_sms_text[s->last_sms_length++]=(uint16_t)(value>=32&&value<127?value:value==0?'@':value==10?'\n':value==13?'\r':'?');}}
    s->last_sms_text[s->last_sms_length]=0;
}
int cellular_hle(Cellular *s,Arm7 *c)
{
    uint32_t pc=c->r[15],p=c->r[0];unsigned i;
    if(!s->enabled||!s->ready||!(c->cpsr&ARM_T))return 0;
    if(pc==s->radio_select){
        uint8_t reg[20]={0};reg[0]=6;word(reg+4,0x00101f);reg[8]=1;
        reg[10]=0;reg[11]=0xf1;reg[12]=0x10;reg[14]=1;reg[16]=1;
        for(i=0;i<20;++i)bus_write(c->bus,p+i,1,reg[i]);return 0;
    }
    if(pc==s->name_lookup&&p==0x00101f&&s->name_ptr){bus_write(c->bus,c->r[1],4,s->name_ptr);bus_write(c->bus,c->r[1]+4,4,s->name_ptr);bus_write(c->bus,c->r[1]+8,1,0);return returned(c);}
    if(pc==s->dial){uint8_t alert[4]={0};if(s->count>CELL_EVENTS-3)return 0;
        s->call_id=bus_read(c->bus,p,2);for(i=0;i<32;++i){s->number[i]=(char)bus_read(c->bus,p+2+i,1);if(!s->number[i])break;}s->number[32]=0;
        s->call_state=CELL_FROM_PHONE;s->outgoing=1;++s->revision;alert[0]=(uint8_t)s->call_id;alert[1]=(uint8_t)(s->call_id>>8);
        (void)release_later(s,p);(void)cellular_queue(s,0,0x162,alert,sizeof(alert));return returned(c);
    }
    if(pc==s->accept&&s->call_state==CELL_TO_PHONE){uint8_t connected[12]={0};if(s->count>CELL_EVENTS-3)return 0;
        connected[0]=(uint8_t)s->call_id;connected[1]=(uint8_t)(s->call_id>>8);(void)release_later(s,p);
        (void)cellular_queue(s,0,0x160,connected,sizeof(connected));s->call_state=CELL_CONNECTED;++s->revision;return returned(c);}
    if(pc==s->disconnect&&s->call_state){if(s->count>CELL_EVENTS-3)return 0;(void)release_later(s,p);(void)disconnect_event(s,16);
        s->call_state=CELL_IDLE;++s->revision;return returned(c);}
    if(pc==s->sms_send){uint8_t confirmation[0x10c]={0};if(s->count>CELL_EVENTS-3)return 0;
        sms_capture(s,c,p);(void)release_later(s,p);(void)cellular_queue(s,s->sr_task,0x1a1,confirmation,sizeof(confirmation));
        ++s->sent_sms;++s->sms_revision;return returned(c);}
    if(pc==s->alert_request){if(!release_later(s,p))return 0;return returned(c);}
    if(pc==s->sms_reply){if(!release_later(s,p))return 0;return returned(c);}
    return 0;
}
