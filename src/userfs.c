#include "userfs.h"
#include "file.h"
#include <string.h>
static const uint8_t *ram(const Machine *m,uint32_t address,uint32_t size)
{
    unsigned i;
    for(i=0;i<m->bus.count;++i){const Region *r=&m->bus.regions[i];
        if(r->data&&!r->read&&!r->write&&!r->readonly&&address>=r->base&&(uint64_t)address+size<=(uint64_t)r->base+r->size)
            return r->data+address-r->base;}
    return NULL;
}
static int error(UserDirectory *d,const char *message){strcpy(d->error,message);return 0;}
int userfs_directory(const Machine *m,UserDirectory *d)
{
    const uint8_t *header,*table;unsigned slots,records,i;
    memset(d,0,sizeof(*d));
    if(!m->loaded||!m->fs_header)return error(d,"This model has no recovered phone file directory.");
    header=ram(m,m->fs_header,4);
    if(!header)return error(d,"Phone directory header is not mapped RAM.");
    slots=header[0]|((unsigned)header[1]<<8);records=header[2]|((unsigned)header[3]<<8);
    if(!slots||!records)return error(d,"Phone directory is not initialized yet. Run the phone, then Refresh.");
    /* Current C100 static NLT allocation is exactly 100*28 bytes. A second
       record bank cannot be assumed to fit beyond that verified allocation. */
    if(slots>m->fs_capacity||slots>USERFS_MAX_FILES||records!=1||m->fs_stride!=28)
        return error(d,"Phone directory layout is unsupported or inconsistent.");
    table=ram(m,m->fs_table,slots*28);if(!table)return error(d,"Phone file table is not mapped RAM.");
    d->slots=slots;
    for(i=0;i<slots;++i){const uint8_t *p=table+i*28;unsigned j,id=p[12]|((unsigned)p[13]<<8);UserFile *f;
        if(!id)continue;
        if(!memchr(p,0,12))return error(d,"Phone file name is not terminated within its 12-byte field.");
        for(j=0;j<d->count;++j)if(d->files[j].id==id)return error(d,"Duplicate phone file identifier.");
        f=&d->files[d->count++];memcpy(f->name,p,12);f->id=(uint16_t)id;f->type=p[14];f->size=le32(p+20);
        memcpy(f->metadata,p,28);d->listed_bytes+=f->size;
    }
    return 1;
}
int userfs_export_directory(const UserDirectory *d,const char *path)
{
    FILE *f;unsigned i,j;int ok=1;
    if(*d->error)return 0;
    f=fopen(path,"wb");if(!f)return 0;
    if(fputs("Name\tID\tType\tBytes\tNLT metadata (hex)\r\n",f)==EOF)ok=0;
    for(i=0;i<d->count&&ok;++i){const UserFile *entry=&d->files[i];char name[13];
        strcpy(name,entry->name);for(j=0;name[j];++j)if((unsigned char)name[j]<32||name[j]==127)name[j]='?';
        if(fprintf(f,"%s\t%04X\t%u\t%u\t",name,entry->id,entry->type,entry->size)<0)ok=0;
        for(j=0;j<28&&ok;++j)if(fprintf(f,"%02X",entry->metadata[j])<0)ok=0;
        if(fputs("\r\n",f)==EOF)ok=0;
    }
    if(fclose(f))ok=0;return ok;
}
