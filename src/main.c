#include "machine.h"
#include "firmware.h"
#include "nvram.h"
#include "userfs.h"
#include "content.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    Machine m = {0};
    NvramStore storage = {0};
    ContentImport content = {0};
    uint64_t budget = 100000;
    unsigned i;
    int continuation = 0, result = 0, mmio_trace = 0, until_set = 0, watch_writes_only = 0, budget_set = 0;
    uint32_t until = 0, watch_start = 0, watch_end = 0;
    uint32_t watch_lo[BUS_MAX_WATCH], watch_hi[BUS_MAX_WATCH];
    unsigned watch_count = 0;
    const char *model;
    const char *lcd_output = NULL;
    const char *import_path = NULL;
    int java_menu=0;
    const char *fs_output=NULL;
    const char *nv_input = NULL, *nv_output = NULL;
    const char *state_directory = NULL; /* Default: model-local, firmware-hashed. */
    const char *hardware_dll=NULL,*audio_wav=NULL;
    uint32_t audio_ticks_hz=2166667;
    int persistent = 1, storage_ready = 0, sim_override = -1;
    FirmwareMatch *matches = NULL;
    unsigned first_argument = 2;
    FILE *trace = NULL, *access_log = NULL, *log;
    if (argc < 2) { puts("sghemu MODEL.cfg | --folder PATH [instruction-budget] [--lcd-out FILE.bmp]\nOptions: --continue-unmapped --trace --mmio-trace --until ADDRESS --service\n         --nv-load FILE.snv --nv-save FILE.snv (diagnostic import/export)\n         --state-dir DIRECTORY --no-persist --sim present|none\n         --ma5-hw M5_EmuHw.dll [--audio-wav FILE.wav]\n         --audio-ticks-hz N (legacy profiles only, not CPU MHz)\n         --fs-list FILE.tsv (directory metadata, not file contents)\n         --import FILE (sound/picture or MIDP 1.0 JAR through the guest)\n         --java-menu (boot the native installed-MIDlet menu)\nNVM auto-loads/saves beside the model config in state/. Run from repo root; see build/unmapped.log."); return 2; }
    model = argv[1];
    if (!strcmp(model, "--folder")) {
        unsigned count;
        if (argc < 3) return 2;
        matches = calloc(FIRMWARE_MAX_MATCHES, sizeof(*matches)); if (!matches) return 1;
        count = firmware_scan(argv[2], "models", matches, FIRMWARE_MAX_MATCHES);
        if (!count) { fprintf(stderr, "No complete supported ROM set matches this folder's SHA-256 hashes.\n"); free(matches); return 1; }
        for (i = 0; i < count; ++i) printf("Detected: %s\n", matches[i].name);
        model = matches[0].model; first_argument = 3;
        if (count > 1) puts("CLI selects the first match; use the GUI to select another detected version.");
    }
    for (i = first_argument; i < (unsigned)argc; ++i) {
        if (!strcmp(argv[i],"--ma5-hw")&&i+1<(unsigned)argc)hardware_dll=argv[++i];
        else if(!strcmp(argv[i],"--audio-wav")&&i+1<(unsigned)argc)audio_wav=argv[++i];
        else if(!strcmp(argv[i],"--audio-ticks-hz")&&i+1<(unsigned)argc) {
            char *end;unsigned long hz=strtoul(argv[++i],&end,0);
            if(*end||hz<48000||hz>100000000){fprintf(stderr,"Invalid audio instruction-tick rate\n");return 2;}audio_ticks_hz=(uint32_t)hz;
        }
        else if (!strcmp(argv[i], "--continue-unmapped")) continuation = 1;
        else if (!strcmp(argv[i], "--no-persist")) persistent = 0;
        else if (!strcmp(argv[i], "--java-menu")) java_menu=1;
        else if (!strcmp(argv[i], "--import") && i + 1 < (unsigned)argc) import_path = argv[++i];
        else if (!strcmp(argv[i], "--service")) sgh_service_mode = 1;
        else if (!strcmp(argv[i],"--sim") && i+1<(unsigned)argc) {
            const char *mode=argv[++i];
            if(!strcmp(mode,"present"))sim_override=1;else if(!strcmp(mode,"none"))sim_override=0;
            else {fprintf(stderr,"--sim requires present or none\n");return 2;}
        }
        else if (!strcmp(argv[i], "--state-dir") && i + 1 < (unsigned)argc) state_directory = argv[++i];
        else if (!strcmp(argv[i], "--lcd-out") && i + 1 < (unsigned)argc) lcd_output = argv[++i];
        else if(!strcmp(argv[i],"--fs-list")&&i+1<(unsigned)argc)fs_output=argv[++i];
        else if (!strcmp(argv[i], "--nv-load") && i + 1 < (unsigned)argc) nv_input = argv[++i];
        else if (!strcmp(argv[i], "--nv-save") && i + 1 < (unsigned)argc) nv_output = argv[++i];
        else if (!strcmp(argv[i], "--mmio-trace")) mmio_trace = 1;
        else if (!strcmp(argv[i], "--until") && i + 1 < (unsigned)argc) {
            char *end;
            unsigned long long n = strtoull(argv[++i], &end, 0);
            if (*end || end == argv[i] || argv[i][0] == '-' || n > UINT32_MAX) { if (trace) fclose(trace); return 2; }
            until = (uint32_t)n; until_set = 1;
        }
        else if (!strcmp(argv[i], "--trace")) { trace = fopen("build/cpu-trace.log", "w"); if (!trace) return 1; }
        else {
            char *end; budget = strtoull(argv[i], &end, 0);
            if (!budget || *end || argv[i][0] == '-') { if (trace) fclose(trace); return 2; }
            budget_set = 1;
        }
    }
    /* The import trigger sits past storage and UI initialization; give a bare
       --import run enough budget to reach it instead of silently doing nothing. */
    if (import_path && !budget_set) budget = 20000000;
    log = fopen("build/unmapped.log", "w");
    if (!log) { if (trace) fclose(trace); return 1; }
    if (!machine_load_pair(&m, model, matches ? matches[0].rom[0] : NULL, matches ? matches[0].rom[1] : NULL, log)) { fprintf(stderr, "%s\n", m.error); result = 1; goto done; }
    if(sim_override>=0) {if(!m.sim.attached){fprintf(stderr,"This model has no SIM interface\n");result=1;goto done;}m.sim.present=sim_override;}
    if (persistent && !nvram_store_open(&storage, &m, state_directory)) { fprintf(stderr, "%s\n", m.error); result = 1; goto done; }
    storage_ready = 1;
    if (nv_input && !(persistent ? nvram_store_import(&storage, &m, nv_input) : nvram_load(&m, nv_input))) { fprintf(stderr, "%s\n", m.error); result = 1; goto done; }
    if (import_path && !storage.active) { fprintf(stderr, "Content import needs the persistent phone directory; boot once so the guest initializes it, then import.\n"); result = 1; goto done; }
    if (import_path && !content_import_open(&content, &m, import_path)) { fprintf(stderr, "%s\n", content.error); result = 1; goto done; }
    if(import_path&&!budget_set&&content.type==10)budget=900000000;
    if(java_menu&&import_path){fprintf(stderr,"Use --java-menu or --import, not both.\n");result=1;goto done;}
    if(java_menu&&!content_java_menu(&content,&m)){fprintf(stderr,"%s\n",content.error);result=1;goto done;}
    if(java_menu&&!budget_set)budget=600000000;
    if(audio_wav&&!hardware_dll){fprintf(stderr,"--audio-wav requires --ma5-hw\n");result=1;goto done;}
    if(hardware_dll) {
        if(!ma3_enable(&m.ma3,hardware_dll,audio_wav,!audio_wav,m.clock.attached?1000000000:audio_ticks_hz)){fprintf(stderr,"MA-3 hardware: %s\n",m.ma3.error);result=1;goto done;}
        m.ma3.trace=fopen("build/ma3-traffic.log","w");
        if(!m.ma3.trace){result=1;goto done;}
        if(m.clock.attached)puts("Direct Yamaha hardware: PLL/core-cycle emulated time, PCM 48000 Hz; no host sequencer");
        else printf("Direct Yamaha hardware: %u instruction ticks/sec, PCM 48000 Hz; no host sequencer\n",audio_ticks_hz);
    }
    if (storage.active) printf("Persistent NVM: %s%s\n", storage.path, storage.provision ? " (first-boot guest provisioning)" : "");
    if (m.bringup) { continuation = 1; puts(m.periodic_time?
        "Experimental bring-up: inferred handoff state and emulated-time frame IRQ source; unknown MMIO continues.":
        "Experimental bring-up: inferred handoff state and instruction-tick frame IRQ source; unknown MMIO continues."); }
    printf("Model: %s\n", m.name);
    if (m.unplaced_rom) printf("Second image: %s (%zu bytes, guest address unresolved)\n", m.unplaced_path, m.unplaced_size);
    if (mmio_trace) {
        access_log = fopen("build/mmio-trace.log", "w");
        if (!access_log) { result = 1; goto done; }
        m.bus.access_log = access_log; m.bus.watch_start = 0x00040000; m.bus.watch_end = 0x00042000;
        watch_start = 0x00040000; watch_end = 0x00042000;
    } else {
        /* MA3_ACCESS_WATCH=start,end[;start,end...] arms the same watch over
           arbitrary guest RAM, writes only, so a RAM field can be attributed to
           the guest PC that wrote it. Same instrument src/gui.c exposes to the
           frontend. */
        const char *range = getenv("MA3_ACCESS_WATCH");
        if (range && *range) {
            access_log = fopen("build/ram-watch.log", "w");
            if (!access_log) { result = 1; goto done; }
            if (!bus_watch_parse(&m.bus, range)) { fclose(access_log); access_log = NULL; }
            else {
                unsigned r;
                watch_start = m.bus.watch_start; watch_end = m.bus.watch_end; watch_writes_only = 1;
                watch_count = m.bus.watch_count;
                for (r = 0; r < watch_count; ++r) { watch_lo[r] = m.bus.watch_lo[r]; watch_hi[r] = m.bus.watch_hi[r]; }
                m.bus.access_log = access_log; m.bus.watch_writes_only = 1;
            }
        }
    }
    while (m.cpu.instructions < budget) {
        int provision = nvram_store_step(&storage, &m);
        if (!provision) { fprintf(stderr, "%s\n", m.error); result = 1; break; }
        if (provision == 2) {
            int initialize_defaults = storage.provision && storage.stage == 3;
            puts(initialize_defaults ? "Guest format and initialization marker saved; cold-resetting to create defaults." :
                "Guest UI defaults complete and saved; cold-resetting for normal startup.");
            nvram_store_release(&storage);
            if (!machine_load_pair(&m, model, matches ? matches[0].rom[0] : NULL, matches ? matches[0].rom[1] : NULL, log) ||
                !nvram_store_open(&storage, &m, state_directory)) { fprintf(stderr, "%s\n", m.error); storage_ready = 0; result = 1; break; }
            storage.defaults_boot = initialize_defaults;
            if(sim_override>=0)m.sim.present=sim_override;
            if(hardware_dll&&!ma3_enable(&m.ma3,hardware_dll,audio_wav,!audio_wav,m.clock.attached?1000000000:audio_ticks_hz)){fprintf(stderr,"MA-3 hardware: %s\n",m.ma3.error);result=1;break;}
            if (access_log) {
                unsigned r;
                m.bus.access_log = access_log;
                for (r = 0; r < watch_count; ++r) { m.bus.watch_lo[r] = watch_lo[r]; m.bus.watch_hi[r] = watch_hi[r]; }
                m.bus.watch_count = watch_count;
                m.bus.watch_start = watch_start; m.bus.watch_end = watch_end;
                m.bus.watch_writes_only = watch_writes_only;
            }
            continue;
        }
        if (until_set && m.cpu.r[15] == until) { printf("Address breakpoint reached: %08x\n", until); break; }
        if (content.active && !content.done && !content.failed) {
            int imported = content_import_step(&content, &m);
            if (!imported) { fprintf(stderr, "%s\n", content.error); result = 1; break; }
            if (imported == 2 && content.type==10) puts(content.size ? "Java installer started; wait for the handset installation result." : "Native Java menu started.");
            if (imported == 2 && content.type!=10) printf("Content import: guest accepted \"%s\" (%u bytes); the flash task writes it as the boot continues.\n", content.name, content.size);
        }
        if (trace) fprintf(trace, "%llu %08x CPSR=%08x R0=%08x R1=%08x SP=%08x\n", (unsigned long long)m.cpu.instructions, m.cpu.r[15], m.cpu.cpsr, m.cpu.r[0], m.cpu.r[1], m.cpu.r[13]);
        if (!machine_step(&m, continuation)) { result = 1; break; }
    }
    if (content.active && !content.done && !content.failed) {
        fprintf(stderr, "Content import: the guest import trigger was not reached; give the boot enough instructions to pass storage initialization.\n");
        result = 1;
    }
    printf("instructions=%llu scheduler_ticks=%llu PC=%08x CPSR=%08x unmapped/ROM-faults=%llu\n",
        (unsigned long long)m.cpu.instructions, (unsigned long long)m.cpu.cycles, m.cpu.r[15], m.cpu.cpsr, (unsigned long long)m.bus.faults);
    if(m.clock.attached)printf("PLL: ARM=%u Hz DSP=%u Hz core-cycles=%llu emulated-time=%.6f seconds%s\n",
        m.clock.arm_hz,m.clock.dsp_hz,(unsigned long long)m.cpu.clock_cycles,(double)m.clock.nanoseconds/1000000000.0,
        m.clock.unknown?" (unrecognized divider)":"");
    if (m.lcd.attached) printf("LCD display=%s commands=%llu pixel_writes=%llu; IRQ events=%llu\n", m.lcd.enabled ? "on" : "off",
        (unsigned long long)m.lcd.commands, (unsigned long long)m.lcd.writes, (unsigned long long)m.intc.events);
    if (m.cpu.error) fprintf(stderr, "STOP PC=%08x opcode=%08x: %s\n", m.cpu.last_pc, m.cpu.last_opcode, m.cpu.error);
    if(m.sim.attached)printf("SIM: %s, resets=%llu APDUs=%llu RX=%llu TX=%llu DMA=%llu\n",m.sim.present?"synthetic card inserted":"removed",
        (unsigned long long)m.sim.resets,(unsigned long long)m.sim.card.commands,(unsigned long long)m.sim.received,
        (unsigned long long)m.sim.transmitted,(unsigned long long)m.sim.dma_bytes);
    if(m.ma3.hw) {Ma5HwStats stats=ma5_hw_stats(m.ma3.hw);
        printf("MA-3: reads=%llu writes=%llu FIFO=%llu IRQ=%llu; PCM frames=%llu nonzero=%llu peak=%u host-underruns=%llu%s\n",
            (unsigned long long)m.ma3.reads,(unsigned long long)m.ma3.writes,(unsigned long long)m.ma3.fifo_bytes,
            (unsigned long long)m.ma3.irq_events,(unsigned long long)stats.frames,(unsigned long long)stats.nonzero,stats.peak,
            (unsigned long long)stats.underruns,stats.failed?" FAILED":"");
        printf("MA-3 compatibility: RAM reads=%llu translated=%llu unsupported=%llu partial FIFO=%u\n",
            (unsigned long long)m.ma3.ram_reads,(unsigned long long)m.ma3.translated,(unsigned long long)m.ma3.unsupported,m.ma3.delayed_length);
        if(stats.failed)result=1;
    }
    for (i = 0; i < m.flash_count; ++i) printf("Flash %08x: programs=%llu erases=%llu last_program=%08x last_erase=%08x\n",
        m.flashes[i].base, (unsigned long long)m.flashes[i].programs, (unsigned long long)m.flashes[i].erases,
        m.flashes[i].last_program, m.flashes[i].last_erase);
    for (i = 0; i < m.irom.count; ++i) printf("Internal-ROM HLE %08x: %llu calls%s\n", m.irom.services[i].address,
        (unsigned long long)m.irom.services[i].calls, m.irom.services[i].kind == IROM_VERSION ? " (caller-specific version query; no BIST results)" : " (memcpy)");
    if (lcd_output && (!m.lcd.attached || !lcd_save_bmp(&m.lcd, lcd_output))) { fprintf(stderr, "Cannot save LCD framebuffer.\n"); result = 1; }
    if (nv_output && !nvram_save(&m, nv_output)) { fprintf(stderr, "%s\n", m.error); result = 1; }
    if(fs_output){UserDirectory directory;
        if(!userfs_directory(&m,&directory)){fprintf(stderr,"Phone filesystem: %s\n",directory.error);result=1;}
        else if(!userfs_export_directory(&directory,fs_output)){fprintf(stderr,"Cannot export phone file directory.\n");result=1;}
        else printf("Phone filesystem: %u files, %llu listed bytes; directory exported to %s\n",directory.count,(unsigned long long)directory.listed_bytes,fs_output);
    }
    if (m.bus.fault) puts("Stopped on unknown hardware; see build/unmapped.log. This is not a successful phone boot.");
done:
    if (storage_ready && !nvram_store_flush(&storage, &m)) { fprintf(stderr, "%s\n", m.error); result = 1; }
    nvram_store_release(&storage); content_import_release(&content);
    if (trace) fclose(trace); machine_destroy(&m); if (access_log) fclose(access_log); fclose(log); free(matches); return result;
}
