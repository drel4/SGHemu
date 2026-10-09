#include "machine.h"
#include "firmware.h"
#include "nvram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
    Machine m = {0};
    NvramStore storage = {0};
    uint64_t budget = 100000;
    unsigned i;
    int continuation = 0, result = 0, mmio_trace = 0, until_set = 0;
    uint32_t until = 0;
    const char *model;
    const char *lcd_output = NULL;
    const char *nv_input = NULL, *nv_output = NULL;
    const char *state_directory = "state";
    int persistent = 1, storage_ready = 0, sim_override = -1;
    FirmwareMatch *matches = NULL;
    unsigned first_argument = 2;
    FILE *trace = NULL, *access_log = NULL, *log;
    if (argc < 2) { puts("sghemu MODEL.cfg | --folder PATH [instruction-budget] [--lcd-out FILE.bmp]\nOptions: --continue-unmapped --trace --mmio-trace --until ADDRESS\n         --nv-load FILE.snv --nv-save FILE.snv (diagnostic import/export)\n         --state-dir DIRECTORY --no-persist --sim present|none\nNVM auto-loads/saves in state/. Run from repo root; see build/unmapped.log."); return 2; }
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
        if (!strcmp(argv[i], "--continue-unmapped")) continuation = 1;
        else if (!strcmp(argv[i], "--no-persist")) persistent = 0;
        else if (!strcmp(argv[i],"--sim") && i+1<(unsigned)argc) {
            const char *mode=argv[++i];
            if(!strcmp(mode,"present"))sim_override=1;else if(!strcmp(mode,"none"))sim_override=0;
            else {fprintf(stderr,"--sim requires present or none\n");return 2;}
        }
        else if (!strcmp(argv[i], "--state-dir") && i + 1 < (unsigned)argc) state_directory = argv[++i];
        else if (!strcmp(argv[i], "--lcd-out") && i + 1 < (unsigned)argc) lcd_output = argv[++i];
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
        }
    }
    log = fopen("build/unmapped.log", "w");
    if (!log) { if (trace) fclose(trace); return 1; }
    if (!machine_load_pair(&m, model, matches ? matches[0].rom[0] : NULL, matches ? matches[0].rom[1] : NULL, log)) { fprintf(stderr, "%s\n", m.error); result = 1; goto done; }
    if(sim_override>=0) {if(!m.sim.attached){fprintf(stderr,"This model has no SIM interface\n");result=1;goto done;}m.sim.present=sim_override;}
    if (persistent && !nvram_store_open(&storage, &m, state_directory)) { fprintf(stderr, "%s\n", m.error); result = 1; goto done; }
    storage_ready = 1;
    if (nv_input && !(persistent ? nvram_store_import(&storage, &m, nv_input) : nvram_load(&m, nv_input))) { fprintf(stderr, "%s\n", m.error); result = 1; goto done; }
    if (storage.active) printf("Persistent NVM: %s%s\n", storage.path, storage.provision ? " (first-boot guest provisioning)" : "");
    if (m.bringup) { continuation = 1; puts("Experimental bring-up: inferred handoff state and fixed-instruction frame IRQ source; unknown MMIO continues."); }
    printf("Model: %s\n", m.name);
    if (m.unplaced_rom) printf("Second image: %s (%zu bytes, guest address unresolved)\n", m.unplaced_path, m.unplaced_size);
    if (mmio_trace) {
        access_log = fopen("build/mmio-trace.log", "w");
        if (!access_log) { result = 1; goto done; }
        m.bus.access_log = access_log; m.bus.watch_start = 0x00040000; m.bus.watch_end = 0x00042000;
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
            if (access_log) { m.bus.access_log = access_log; m.bus.watch_start = 0x40000; m.bus.watch_end = 0x42000; }
            continue;
        }
        if (until_set && m.cpu.r[15] == until) { printf("Address breakpoint reached: %08x\n", until); break; }
        if (trace) fprintf(trace, "%llu %08x CPSR=%08x R0=%08x R1=%08x SP=%08x\n", (unsigned long long)m.cpu.instructions, m.cpu.r[15], m.cpu.cpsr, m.cpu.r[0], m.cpu.r[1], m.cpu.r[13]);
        if (!machine_step(&m, continuation)) { result = 1; break; }
    }
    printf("instructions=%llu scheduler_ticks=%llu PC=%08x CPSR=%08x unmapped/ROM-faults=%llu\n",
        (unsigned long long)m.cpu.instructions, (unsigned long long)m.cpu.cycles, m.cpu.r[15], m.cpu.cpsr, (unsigned long long)m.bus.faults);
    if (m.lcd.attached) printf("LCD display=%s commands=%llu pixel_writes=%llu; IRQ events=%llu\n", m.lcd.enabled ? "on" : "off",
        (unsigned long long)m.lcd.commands, (unsigned long long)m.lcd.writes, (unsigned long long)m.intc.events);
    if (m.cpu.error) fprintf(stderr, "STOP PC=%08x opcode=%08x: %s\n", m.cpu.last_pc, m.cpu.last_opcode, m.cpu.error);
    if(m.sim.attached)printf("SIM: %s, resets=%llu APDUs=%llu RX=%llu TX=%llu DMA=%llu\n",m.sim.present?"synthetic card inserted":"removed",
        (unsigned long long)m.sim.resets,(unsigned long long)m.sim.card.commands,(unsigned long long)m.sim.received,
        (unsigned long long)m.sim.transmitted,(unsigned long long)m.sim.dma_bytes);
    for (i = 0; i < m.flash_count; ++i) printf("Flash %08x: programs=%llu erases=%llu last_program=%08x last_erase=%08x\n",
        m.flashes[i].base, (unsigned long long)m.flashes[i].programs, (unsigned long long)m.flashes[i].erases,
        m.flashes[i].last_program, m.flashes[i].last_erase);
    for (i = 0; i < m.irom.count; ++i) printf("Internal-ROM HLE %08x: %llu calls%s\n", m.irom.services[i].address,
        (unsigned long long)m.irom.services[i].calls, m.irom.services[i].kind == IROM_VERSION ? " (caller-specific version query; no BIST results)" : " (memcpy)");
    if (lcd_output && (!m.lcd.attached || !lcd_save_bmp(&m.lcd, lcd_output))) { fprintf(stderr, "Cannot save LCD framebuffer.\n"); result = 1; }
    if (nv_output && !nvram_save(&m, nv_output)) { fprintf(stderr, "%s\n", m.error); result = 1; }
    if (m.bus.fault) puts("Stopped on unknown hardware; see build/unmapped.log. This is not a successful phone boot.");
done:
    if (storage_ready && !nvram_store_flush(&storage, &m)) { fprintf(stderr, "%s\n", m.error); result = 1; }
    nvram_store_release(&storage);
    if (trace) fclose(trace); machine_destroy(&m); if (access_log) fclose(access_log); fclose(log); free(matches); return result;
}
