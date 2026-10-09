#include "sim_card.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
static void put(SimCard *c,unsigned byte)
{
    if (c->response_size==sizeof(c->response)) { c->overflow=1; return; }
    c->response[(c->response_head+c->response_size)%sizeof(c->response)]=(uint8_t)byte; ++c->response_size;
}
int sim_card_receive(SimCard *c,uint8_t *byte)
{
    if (!c->response_size) return 0;
    *byte=c->response[c->response_head]; c->response_head=(c->response_head+1)%sizeof(c->response); --c->response_size; return 1;
}
static void status(SimCard *c,unsigned sw) { put(c,sw>>8);put(c,sw); c->command_size=c->input_size=0; }
static SimFile *file(SimCard *c,unsigned id,unsigned parent)
{
    unsigned i; for(i=0;i<c->count;++i)if(c->files[i].id==id && c->files[i].parent==parent)return &c->files[i]; return NULL;
}
static SimFile *add(SimCard *c,unsigned id,unsigned parent,unsigned size,unsigned record,unsigned fill)
{
    SimFile *f=&c->files[c->count++]; f->id=(uint16_t)id;f->parent=(uint16_t)parent;
    f->size=(uint16_t)size;f->record=(uint8_t)record;memset(f->data,(int)fill,size);return f;
}
static void metadata(SimCard *c)
{
    SimFile *f=file(c,c->selected,c->directory);unsigned i;
    memset(c->metadata,0,sizeof(c->metadata)); c->metadata[4]=(uint8_t)(c->selected>>8);c->metadata[5]=(uint8_t)c->selected;
    if (c->selected==c->directory) {
        c->metadata_size=22;c->metadata[6]=c->directory==0x3f00?1:2;c->metadata[12]=9;
        c->metadata[13]=0x90; /* CHV1 disabled, 3 V technology supported. */
        c->metadata[14]=c->directory==0x3f00?2:0;
        for(i=0;i<c->count;++i)if(c->files[i].parent==c->directory)++c->metadata[15];
        c->metadata[16]=4;c->metadata[18]=0x83;c->metadata[19]=0x8a;c->metadata[20]=0x83;c->metadata[21]=0x8a;
    } else if(f) {
        c->metadata_size=15;c->metadata[2]=(uint8_t)(f->size>>8);c->metadata[3]=(uint8_t)f->size;c->metadata[6]=4;
        c->metadata[8]=0;c->metadata[9]=0xff;c->metadata[10]=0xff;c->metadata[11]=1;c->metadata[12]=2;
        c->metadata[13]=f->record?1:0;c->metadata[14]=f->record;
    } else c->metadata_size=0;
}
void sim_card_reset(SimCard *c)
{
    c->directory=c->selected=0x3f00;c->record=1;c->command_size=c->input_size=c->response_head=c->response_size=0;c->overflow=0;
    metadata(c);put(c,0x3b);put(c,0x00); /* Direct convention, T=0, default baud. */
}
static void populate(SimCard *c)
{
    SimFile *f;unsigned i,n=(unsigned)strlen(c->imsi);c->count=0;
    f=add(c,0x2fe2,0x3f00,10,0,255);
    for(i=0;i<20;++i) { unsigned digit=c->iccid[i]?(unsigned)(c->iccid[i]-'0'):15; if(!(i&1)) f->data[i/2]=(uint8_t)digit;else f->data[i/2]|=(uint8_t)(digit<<4); }
    f=add(c,0x6f07,0x7f20,9,0,255);f->data[0]=(uint8_t)((n+2)/2);f->data[1]=(uint8_t)(((c->imsi[0]-'0')<<4)|((n&1)?9:1));
    for(i=1;i<n;++i) { unsigned at=2+(i-1)/2,digit=(unsigned)(c->imsi[i]-'0'); if(i&1) f->data[at]=(uint8_t)(0xf0|digit);else f->data[at]=(uint8_t)((f->data[at]&15)|(digit<<4)); }
    f=add(c,0x6fad,0x7f20,4,0,0);f->data[3]=2;
    f=add(c,0x6fae,0x7f20,1,0,0);f->data[0]=2;
    f=add(c,0x6f05,0x7f20,2,0,255);f->data[0]=1; /* English GSM language index. */
    add(c,0x6f20,0x7f20,9,0,255);add(c,0x6f30,0x7f20,24,0,255);
    f=add(c,0x6f31,0x7f20,1,0,0);f->data[0]=10;
    add(c,0x6f38,0x7f20,16,0,0); /* Optional services not advertised yet. */
    f=add(c,0x6f46,0x7f20,17,0,255);f->data[0]=0;memcpy(f->data+1,c->spn,strlen(c->spn));
    add(c,0x6f74,0x7f20,16,0,255);
    f=add(c,0x6f78,0x7f20,2,0,0);f->data[1]=1;
    add(c,0x6f7b,0x7f20,12,0,255);
    f=add(c,0x6f7e,0x7f20,11,0,255);f->data[9]=0;f->data[10]=1;
    f=add(c,0x6fb7,0x7f20,3,0,255);f->data[0]=0x11;f->data[1]=0xf2;f->data[2]=0xff; /* ECC 112. */
    add(c,0x6f3a,0x7f10,320,32,255);f=add(c,0x6f3c,0x7f10,1760,176,255);
    for(i=0;i<10;++i)f->data[i*176]=0; /* Free SMS records. */
    add(c,0x6f40,0x7f10,32,32,255);add(c,0x6f42,0x7f10,40,40,255);
    sim_card_reset(c);
}
void sim_card_init(SimCard *c)
{
    memset(c,0,sizeof(*c));strcpy(c->imsi,"001010000000001");strcpy(c->iccid,"89001000000000000001");strcpy(c->spn,"SGHemu");populate(c);
}
int sim_card_load(SimCard *c,const char *path)
{
    FILE *fp=fopen(path,"r");char line[256],imsi[16],iccid[21],spn[17];unsigned seen=0;
    if(!fp){snprintf(c->error,sizeof(c->error),"Cannot open SIM config");return 0;}
    strcpy(imsi,c->imsi);strcpy(iccid,c->iccid);strcpy(spn,c->spn);
    while(fgets(line,sizeof(line),fp)) {
        char *key=line,*value,*end,*dst;size_t max,n;unsigned bit;
        while(isspace((unsigned char)*key))++key;if(!*key||*key=='#')continue;
        value=strchr(key,'=');if(!value)goto bad;*value++=0;end=key+strlen(key);while(end>key&&isspace((unsigned char)end[-1]))*--end=0;
        while(isspace((unsigned char)*value))++value;end=value+strlen(value);while(end>value&&isspace((unsigned char)end[-1]))*--end=0;
        if(!strcmp(key,"imsi")){dst=imsi;max=15;bit=1;}else if(!strcmp(key,"iccid")){dst=iccid;max=20;bit=2;}
        else if(!strcmp(key,"spn")){dst=spn;max=16;bit=4;}else goto bad;
        n=strlen(value);if(!n||n>max||(seen&bit)||(bit==1&&n<5)||(bit==2&&n!=20))goto bad;
        for(end=value;*end;++end)if(bit!=4 ? !isdigit((unsigned char)*end) : (*end<32||*end>126))goto bad;
        strcpy(dst,value);seen|=bit;
    }
    if(ferror(fp))goto bad;fclose(fp);strcpy(c->imsi,imsi);strcpy(c->iccid,iccid);strcpy(c->spn,spn);populate(c);return 1;
bad: fclose(fp);strcpy(c->error,"Invalid synthetic SIM config");return 0;
}
static void execute(SimCard *c)
{
    unsigned ins=c->command[1],length=c->command[4]?c->command[4]:256,offset=((unsigned)c->command[2]<<8)|c->command[3],i;
    SimFile *f=file(c,c->selected,c->directory);
    if(ins==0xa4) {
        unsigned id=((unsigned)c->command[5]<<8)|c->command[6];
        if(id==0x3f00||id==0x7f20||id==0x7f10){c->directory=c->selected=(uint16_t)id;}
        else if(file(c,id,c->directory)){c->selected=(uint16_t)id;c->record=1;}
        else {status(c,0x9404);return;}
        metadata(c);status(c,0x9f00|c->metadata_size);return;
    }
    if(ins==0xc0||ins==0xf2) {
        if(ins==0xf2) {uint16_t selected=c->selected;c->selected=c->directory;metadata(c);c->selected=selected;}
        if(length>c->metadata_size){status(c,0x6700);return;}
        put(c,ins);for(i=0;i<length;++i)put(c,c->metadata[i]);status(c,0x9000);return;
    }
    if(ins==0xb0||ins==0xb2||ins==0xd6||ins==0xdc) {
        int record=ins==0xb2||ins==0xdc;
        if(!f){status(c,0x9400);return;}
        if(record!=!!f->record){status(c,0x9408);return;}
        if(record) {
            unsigned mode=c->command[3]&7,rec=c->command[2];
            if(mode==2)rec=c->record+1;else if(mode==3)rec=c->record-1;else if(mode!=4){status(c,0x6b00);return;}
            if(!rec)rec=c->record;if(!rec||rec>(unsigned)f->size/f->record||length>f->record){status(c,0x9402);return;}
            c->record=(uint16_t)rec;offset=(rec-1)*f->record;
        }
        if(offset+length>f->size){status(c,0x9402);return;}
        if(ins==0xd6||ins==0xdc)memcpy(f->data+offset,c->command+5,length);
        else {put(c,ins);for(i=0;i<length;++i)put(c,f->data[offset+i]);}
        status(c,0x9000);return;
    }
    if(ins==0xfa){status(c,0x9000);return;}
    /* No authentication algorithm, toolkit, or invented success for unknown commands. */
    status(c,0x6d00);
}
void sim_card_send(SimCard *c,uint8_t byte)
{
    unsigned ins;
    if(c->command_size==sizeof(c->command)){c->overflow=1;status(c,0x6700);return;}
    c->command[c->command_size++]=byte;
    if(c->command_size<5)return;
    ins=c->command[1];
    if(c->command_size==5) {
        ++c->commands;
        if(c->command[0]!=0xa0){status(c,0x6e00);return;}
        if(ins==0xa4||ins==0xd6||ins==0xdc) {
            c->input_size=c->command[4]?c->command[4]:256;
            if(ins==0xa4&&c->input_size!=2){status(c,0x6700);return;}
            put(c,ins);return;
        }
        execute(c);
    } else if(c->command_size==5+c->input_size) execute(c);
}
