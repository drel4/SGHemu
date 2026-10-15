#include "content.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* The import module drives real guest code. To exercise it without a firmware
   image, this test maps a small RAM block and places minimal Thumb stubs for
   the three guest entries a model declares. */
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"content line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

#define RAM_BASE 0x1000u
#define RAM_SIZE 0x4000u
static uint8_t ram[RAM_SIZE];

static void put(uint32_t address, unsigned width, uint32_t value)
{
    uint8_t *p = ram + (address - RAM_BASE);
    unsigned i;
    for (i = 0; i < width; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
/* ldr r0,[pc,#0]; bx lr; .word <value>  -- return a constant in r0. */
static void stub_return(uint32_t address, uint32_t value)
{
    put(address, 2, 0x4800); put(address + 2, 2, 0x4770); put(address + 4, 4, value);
}
/* movs r0,#<value>; bx lr -- return a small constant in r0. */
static void stub_small(uint32_t address, uint32_t value)
{
    put(address, 2, 0x2000u | (value & 0xffu)); put(address + 2, 2, 0x4770);
}
static void boot_cpu(Machine *m)
{
    arm7_reset(&m->cpu, &m->bus, 0x2100);
    CHECK(arm7_set_cpsr(&m->cpu, 0x33)); /* Thumb supervisor, like the guest at the trigger. */
    m->cpu.r[13] = 0x3f00;               /* A guest task stack with room for stacked arguments. */
    m->cpu.r[15] = 0x2100;
}
int main(void)
{
    static const uint8_t bytes[8] = {'M', 'M', 'F', 1, 2, 3, 4, 5};
    Machine m = {0};
    ContentImport im = {0};
    FILE *f;
    int done = 0, guard = 0, saw_search = 0, saw_write = 0, i;
    uint32_t name_ptr = 0, data_ptr = 0, args[4] = {0}, stacked[3] = {0};
    char guest_name[16];

    f = fopen("build/ring.mmf", "wb");
    CHECK(f != NULL);
    CHECK(fwrite(bytes, 1, sizeof(bytes), f) == sizeof(bytes));
    CHECK(fclose(f) == 0);

    CHECK(bus_map(&m.bus, RAM_BASE, RAM_SIZE, ram, 0, 0));
    m.import_trigger = 0x2100; m.import_alloc = 0x2000; m.import_search = 0x2010; m.import_write = 0x2020;
    stub_return(m.import_alloc, 0x3000);   /* allocator hands back a scratch block */
    stub_small(m.import_search, 4);        /* search: r0 = 4, the name is free */
    stub_small(m.import_write, 6);         /* writer: r0 = 6, job queued */
    boot_cpu(&m);

    CHECK(content_import_open(&im, &m, "build/ring.mmf"));
    CHECK(im.active && im.size == sizeof(bytes) && !strcmp(im.name, "ring"));

    while (!done && guard++ < 10000) {
        int r = content_import_step(&im, &m);
        if (r == 0) { fprintf(stderr, "%s\n", im.error); exit(1); }
        if (r == 2) { done = 1; break; }
        /* Read each call's arguments after the machine arms it and before the
           stub runs, so the registers still hold the staged values. */
        if (m.cpu.r[15] == m.import_search) { name_ptr = m.cpu.r[0]; saw_search = 1; }
        if (m.cpu.r[15] == m.import_write) {
            args[0] = m.cpu.r[0]; args[1] = m.cpu.r[1]; args[2] = m.cpu.r[2]; args[3] = m.cpu.r[3];
            data_ptr = m.cpu.r[2];
            stacked[0] = bus_read(&m.bus, m.cpu.r[13], 4);
            stacked[1] = bus_read(&m.bus, m.cpu.r[13] + 4, 4);
            stacked[2] = bus_read(&m.bus, m.cpu.r[13] + 8, 4);
            saw_write = 1;
        }
        CHECK(arm7_step(&m.cpu) == 1);
    }
    CHECK(done && saw_search && saw_write && im.done && !im.failed);
    CHECK(name_ptr != 0 && name_ptr == args[0]);
    CHECK(args[1] == 6 && args[2] == data_ptr && args[3] == sizeof(bytes));
    CHECK(data_ptr == 0x3000);
    /* Fifth argument is the out ticket; the sixth and seventh are null callbacks. */
    CHECK(stacked[0] != 0 && stacked[1] == 0 && stacked[2] == 0);
    for (i = 0; i < 12; ++i) guest_name[i] = (char)bus_read(&m.bus, name_ptr + i, 1);
    CHECK(!strcmp(guest_name, "ring"));
    for (i = 0; i < (int)sizeof(bytes); ++i) CHECK(bus_read(&m.bus, data_ptr + i, 1) == bytes[i]);
    content_import_release(&im);
    bus_destroy(&m.bus);

    /* A taken name makes the guest search answer 0; the module should retry with
       suffixes and then fail clearly rather than overwrite an existing entry. */
    memset(ram, 0, sizeof(ram));
    {
        Machine c = {0};
        ContentImport ci = {0};
        int failed = 0;
        CHECK(bus_map(&c.bus, RAM_BASE, RAM_SIZE, ram, 0, 0));
        c.import_trigger = 0x2100; c.import_alloc = 0x2000; c.import_search = 0x2010; c.import_write = 0x2020;
        stub_return(c.import_alloc, 0x3000);
        stub_small(c.import_search, 0); /* search: r0 = 0, the name is taken */
        stub_small(c.import_write, 6);
        boot_cpu(&c);
        CHECK(content_import_open(&ci, &c, "build/ring.mmf"));
        guard = 0;
        while (guard++ < 20000) {
            int r = content_import_step(&ci, &c);
            if (r == 0) { failed = 1; break; }
            if (r == 2) break;
            if (!arm7_step(&c.cpu)) break;
        }
        CHECK(failed && strstr(ci.error, "free file name") != NULL);
        content_import_release(&ci);
        bus_destroy(&c.bus);
    }

    /* A model that declares no guest entries cannot import, and neither can an
       unreadable input; both must report rather than guess. */
    {
        Machine none = {0};
        ContentImport ni = {0};
        CHECK(!content_import_open(&ni, &none, "build/ring.mmf"));
        CHECK(strstr(ni.error, "guest entries") != NULL);
        content_import_release(&ni);
    }
    {
        ContentImport ni = {0};
        CHECK(!content_import_open(&ni, &m, "build/content-missing.mmf"));
        CHECK(strstr(ni.error, "input file") != NULL);
        content_import_release(&ni);
    }
    /* Decode a real BMP into the handset's native bitmap container. */
    {
        uint8_t bmp[62]={0};ContentImport image={0};
        bmp[0]='B';bmp[1]='M';bmp[2]=62;bmp[10]=54;bmp[14]=40;
        bmp[18]=2;bmp[22]=1;bmp[26]=1;bmp[28]=24;bmp[34]=8;
        bmp[56]=255;bmp[58]=255; /* red, green; padded 24-bit row */
        f=fopen("build/content-picture.bmp","wb");CHECK(f!=NULL);
        CHECK(fwrite(bmp,1,sizeof(bmp),f)==sizeof(bmp));CHECK(!fclose(f));
        CHECK(content_import_open(&image,&m,"build/content-picture.bmp"));
        CHECK(image.type==7&&image.size==16);
        CHECK(image.file[0]==2&&image.file[1]==1&&image.file[2]==0&&image.file[3]==1&&image.file[5]==6);
        CHECK(image.file[9]==0&&image.file[10]==0);
        CHECK(image.file[11]==0&&image.file[12]==0xf8&&image.file[13]==0xe0&&image.file[14]==7);
        CHECK(image.file[15]==0);
        content_import_release(&image);
        f=fopen("build/content-bad.png","wb");CHECK(f!=NULL);
        CHECK(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));CHECK(!fclose(f));
        CHECK(!content_import_open(&image,&m,"build/content-bad.png"));
        CHECK(strstr(image.error,"decode this picture")!=NULL);content_import_release(&image);
    }
    /* Odd-length downloads must retain their final byte across the firmware's
       halfword-only storage reader; padding must not alter any input byte. */
    {
        static const uint8_t odd[]={ 'M','M','M','D',0,0,0,3,0x12,0x34,0xb6 };
        ContentImport sound={0};
        f=fopen("build/content-odd.mmf","wb");CHECK(f!=NULL);
        CHECK(fwrite(odd,1,sizeof(odd),f)==sizeof(odd));CHECK(!fclose(f));
        CHECK(content_import_open(&sound,&m,"build/content-odd.mmf"));
        CHECK(sound.size==12&&!memcmp(sound.file,odd,sizeof(odd))&&sound.file[11]==0);
        content_import_release(&sound);
    }
    /* A different RTOS task reaching idle is not an allocator return. */
    {
        Machine j={0};ContentImport ji={0};unsigned phase;
        CHECK(bus_map(&j.bus,RAM_BASE,RAM_SIZE,ram,0,0));boot_cpu(&j);
        j.java_ready=0x2100;j.import_alloc=0x2000;j.java_set_jar=0x2020;
        j.java_send_ui=0x2010;j.java_globals=0x3804;j.java_message=0x610;
        ji.type=10;ji.active=1;ji.trigger=j.java_ready;ji.alloc=j.import_alloc;
        ji.file=malloc(4);ji.jad=malloc(8);CHECK(ji.file&&ji.jad);
        memcpy(ji.file,"JAR!",4);strcpy(ji.jad,"JAD");ji.size=4;
        CHECK(content_import_step(&ji,&j)==1&&ji.stage==1);
        CHECK(j.cpu.r[14]==0xffff0001u);
        for(phase=1;phase<=5;++phase){
            j.cpu.r[15]=ji.trigger;j.cpu.r[0]=0;
            CHECK(content_import_step(&ji,&j)==1&&(unsigned)ji.stage==phase&&!ji.failed);
            j.cpu.r[15]=0xffff0000u;j.cpu.r[0]=0x3000+(phase-1)*0x100;
            CHECK(content_import_step(&ji,&j)==(phase==5?2:1));
            if(phase<5)CHECK(j.cpu.r[14]==0xffff0001u);
        }
        CHECK(ji.done&&j.cpu.r[15]==ji.trigger);
        CHECK(bus_read(&j.bus,0x3000,4)==0x2152414au);
        CHECK(bus_read(&j.bus,j.java_globals+4,4)==0x3100);
        content_import_release(&ji);bus_destroy(&j.bus);
    }
    printf("Content import: %u checks passed.\n", checks);
    return 0;
}
