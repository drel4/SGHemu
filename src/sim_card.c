#include "sim_card.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    f=add(c,0x6f38,0x7f20,16,0,0);
    f->data[0]=0xc0; /* Service 4: SMS storage, allocated and activated. */
    f->data[2]=0xc0; /* Service 12: SMS parameters. */
    f=add(c,0x6f46,0x7f20,17,0,255);f->data[0]=0;memcpy(f->data+1,c->spn,strlen(c->spn));
    add(c,0x6f74,0x7f20,16,0,255);
    f=add(c,0x6f78,0x7f20,2,0,0);f->data[1]=1;
    add(c,0x6f7b,0x7f20,12,0,255);
    f=add(c,0x6f7e,0x7f20,11,0,255);f->data[9]=0;f->data[10]=1;
    f=add(c,0x6fb7,0x7f20,3,0,255);f->data[0]=0x11;f->data[1]=0xf2;f->data[2]=0xff; /* ECC 112. */
    add(c,0x6f3a,0x7f10,320,32,255);f=add(c,0x6f3c,0x7f10,1760,176,255);
    for(i=0;i<10;++i)f->data[i*176]=0; /* Free SMS records. */
    add(c,0x6f40,0x7f10,32,32,255);f=add(c,0x6f42,0x7f10,40,40,255);
    memcpy(f->data,"SGHemu SMS",10);f->data[12]=0xfd; /* Only service centre is present. */
    f->data[25]=7;f->data[26]=0x91;
    f->data[27]=0x00;f->data[28]=0x00;f->data[29]=0x00;f->data[30]=0x00;f->data[31]=0x00;f->data[32]=0xf1;
    f=add(c,0x6f43,0x7f10,2,0,255);f->data[0]=0; /* Last used TP-MR; memory available. */
    c->dirty=1;
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
        if(ins==0xd6||ins==0xdc){memcpy(f->data+offset,c->command+5,length);c->dirty=1;}
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

/* SIM storage is separate from phone NVM. Serialize only user-writable EFs,
   bind it to the configured card identity, and validate before applying. */
static const uint16_t storage_ids[]={0x6f3a,0x6f3c,0x6f40,0x6f42,0x6f43};
static uint32_t storage_crc(const uint8_t *p,size_t n)
{
    uint32_t crc=~0u;unsigned k;while(n--){crc^=*p++;for(k=0;k<8;++k)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}return ~crc;
}
int sim_card_save_storage(SimCard *c,const char *path)
{
    uint8_t bytes[8192]={0};size_t n=48;unsigned i;FILE *f;char temp[1100];uint32_t crc;int ok;
    if(!c->dirty)return 1;
    memcpy(bytes,"SGHSIM1",7);memcpy(bytes+8,c->imsi,16);memcpy(bytes+24,c->iccid,21);
    for(i=0;i<sizeof(storage_ids)/sizeof(storage_ids[0]);++i){SimFile *s=file(c,storage_ids[i],0x7f10);
        if(!s||n+4+s->size+4>sizeof(bytes)){snprintf(c->error,sizeof(c->error),"Invalid SIM storage layout");return 0;}
        bytes[n++]=(uint8_t)s->id;bytes[n++]=(uint8_t)(s->id>>8);bytes[n++]=(uint8_t)s->size;bytes[n++]=(uint8_t)(s->size>>8);
        memcpy(bytes+n,s->data,s->size);n+=s->size;
    }
    crc=storage_crc(bytes,n);for(i=0;i<4;++i)bytes[n++]=(uint8_t)(crc>>(i*8));
    if(strlen(path)+5>=sizeof(temp)){snprintf(c->error,sizeof(c->error),"SIM storage path is too long");return 0;}snprintf(temp,sizeof(temp),"%s.tmp",path);
    f=fopen(temp,"wb");if(!f){snprintf(c->error,sizeof(c->error),"Could not open SIM storage for writing");return 0;}ok=fwrite(bytes,1,n,f)==n;if(fclose(f))ok=0;
    if(ok)ok=MoveFileExA(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!ok){remove(temp);snprintf(c->error,sizeof(c->error),"Could not save SIM storage");return 0;}
    c->dirty=0;return 1;
}
int sim_card_load_storage(SimCard *c,const char *path)
{
    uint8_t bytes[8192];size_t n,pos=48;unsigned i;SimFile *targets[5];size_t offsets[5];FILE *f=fopen(path,"rb");uint32_t crc=0;
    if(!f){DWORD attr=GetFileAttributesA(path);if(attr==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND)return 1;
        snprintf(c->error,sizeof(c->error),"Could not open SIM storage");return 0;}
    n=fread(bytes,1,sizeof(bytes),f);if(ferror(f)||!feof(f)){fclose(f);goto invalid;}if(fclose(f))goto invalid;
    if(n<52||memcmp(bytes,"SGHSIM1",8)||memcmp(bytes+8,c->imsi,16)||memcmp(bytes+24,c->iccid,21))goto invalid;
    for(i=0;i<4;++i)crc|=(uint32_t)bytes[n-4+i]<<(i*8);
    if(storage_crc(bytes,n-4)!=crc)goto invalid;
    for(i=0;i<5;++i){unsigned id,size;if(pos+4>n-4)goto invalid;
        id=bytes[pos]|((unsigned)bytes[pos+1]<<8);size=bytes[pos+2]|((unsigned)bytes[pos+3]<<8);pos+=4;
        if(id!=storage_ids[i]||!(targets[i]=file(c,id,0x7f10))||size!=targets[i]->size||pos+size>n-4)goto invalid;
        offsets[i]=pos;pos+=size;
    }
    if(pos!=n-4)goto invalid;
    for(i=0;i<5;++i)memcpy(targets[i]->data,bytes+offsets[i],targets[i]->size);
    c->dirty=0;return 1;
invalid:snprintf(c->error,sizeof(c->error),"SIM storage is corrupt or belongs to another card");return 0;
}
unsigned sim_card_deliver_sms(SimCard *c,const char *sender,const uint16_t *text,unsigned length,const uint8_t scts[7])
{
    SimFile *sms=file(c,0x6f3c,0x7f10);uint8_t record[176];unsigned digits=0,i,n=0,slot;const char *number=sender;
    if(!sms||!sender||!text||!scts||!length||length>70)goto invalid;
    if(*number=='+')++number;while(number[digits]){if(number[digits]<'0'||number[digits]>'9'||digits==20)goto invalid;++digits;}
    if(!digits)goto invalid;for(i=0;i<length;++i)if(text[i]>=0xd800&&text[i]<=0xdfff)goto invalid;
    for(slot=0;slot<(unsigned)sms->size/sms->record;++slot)if(!(sms->data[slot*sms->record]&1u))break;
    if(slot==(unsigned)sms->size/sms->record){strcpy(c->error,"SIM SMS storage is full");return 0;}
    memset(record,255,sizeof(record));record[n++]=3;record[n++]=0; /* Unread; default SCA. */
    record[n++]=4;record[n++]=(uint8_t)digits;record[n++]=sender[0]=='+'?0x91:0x81;
    for(i=0;i<digits;i+=2)record[n++]=(uint8_t)((number[i]-'0')|((i+1<digits?number[i+1]-'0':15)<<4));
    record[n++]=0;record[n++]=8;memcpy(record+n,scts,7);n+=7;record[n++]=(uint8_t)(length*2);
    for(i=0;i<length;++i){record[n++]=(uint8_t)(text[i]>>8);record[n++]=(uint8_t)text[i];}
    memcpy(sms->data+slot*sms->record,record,sms->record);c->dirty=1;*c->error=0;return slot+1;
invalid:strcpy(c->error,"SMS needs a numeric sender and 1-70 UCS-2 characters (no emoji)");return 0;
}
