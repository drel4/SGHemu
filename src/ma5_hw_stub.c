/*
 * Linux build: the Yamaha MA-5 backend talks to an x86 Windows DLL, so it is
 * not available here.  The MA-3 register model in ma3.c still runs; only the
 * optional --ma5-hw path reports that the hardware backend is missing.
 */

#include "ma5_hw.h"
#include <stdio.h>

void ma5_hw_profile_enable(Ma5Hw *hw, int enabled)
{
    (void)hw;
    (void)enabled;
}

Ma5HwProfile ma5_hw_profile(Ma5Hw *hw)
{
    Ma5HwProfile profile = {0};
    (void)hw;
    return profile;
}

Ma5Hw *ma5_hw_open(const char *dll, const char *wav, int speakers,
                   char *error, unsigned error_size)
{
    (void)dll;
    (void)wav;
    (void)speakers;
    if (error && error_size)
        snprintf(error, error_size, "Yamaha hardware audio is Windows-only in this build");
    return NULL;
}

unsigned ma5_hw_read(Ma5Hw *hw, int data)
{
    (void)hw;
    (void)data;
    return 0;
}

void ma5_hw_write(Ma5Hw *hw, int data, unsigned value)
{
    (void)hw;
    (void)data;
    (void)value;
}

void ma5_hw_fifo(Ma5Hw *hw, unsigned selector, const uint8_t *bytes, unsigned length)
{
    (void)hw;
    (void)selector;
    (void)bytes;
    (void)length;
}

int ma5_hw_generate(Ma5Hw *hw, unsigned frames)
{
    (void)hw;
    (void)frames;
    return 0;
}

void ma5_hw_set_mixer(Ma5Hw *hw, Ma5PcmMixer mixer, void *context)
{
    (void)hw;
    (void)mixer;
    (void)context;
}

void ma5_hw_pump(Ma5Hw *hw)
{
    (void)hw;
}

void ma5_hw_set_detune(Ma5Hw *hw, int enabled)
{
    (void)hw;
    (void)enabled;
}

void ma5_hw_set_water(Ma5Hw *hw, int enabled)
{
    (void)hw;
    (void)enabled;
}

void ma5_hw_pause(Ma5Hw *hw, int paused)
{
    (void)hw;
    (void)paused;
}

void ma5_hw_reset(Ma5Hw *hw)
{
    (void)hw;
}

void ma5_hw_parameter(Ma5Hw *hw, unsigned address, unsigned value)
{
    (void)hw;
    (void)address;
    (void)value;
}

unsigned ma5_hw_memory(Ma5Hw *hw, unsigned address, int full_ram)
{
    (void)hw;
    (void)address;
    (void)full_ram;
    return 0;
}

unsigned ma5_hw_pcm_cursor(Ma5Hw *hw, unsigned voice)
{
    (void)hw;
    (void)voice;
    return UINT32_MAX;
}

void ma5_hw_ram_changed(Ma5Hw *hw, unsigned address, unsigned length)
{
    (void)hw;
    (void)address;
    (void)length;
}

void ma5_hw_register(Ma5Hw *hw, unsigned reg, unsigned value, unsigned bank)
{
    (void)hw;
    (void)reg;
    (void)value;
    (void)bank;
}

unsigned ma5_hw_led_token(Ma5Hw *hw)
{
    (void)hw;
    return 0;
}

int ma5_hw_irq(Ma5Hw *hw)
{
    (void)hw;
    return 0;
}

int ma5_hw_stress(Ma5Hw *hw)
{
    (void)hw;
    return 0;
}

Ma5HwStats ma5_hw_stats(Ma5Hw *hw)
{
    Ma5HwStats stats = {0};
    (void)hw;
    return stats;
}

void ma5_hw_diagnostics(Ma5Hw *hw)
{
    (void)hw;
}

void ma5_hw_close(Ma5Hw *hw)
{
    (void)hw;
}
