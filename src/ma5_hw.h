#ifndef SGH_MA5_HW_H
#define SGH_MA5_HW_H
#include <stdint.h>
typedef struct Ma5Hw Ma5Hw;
typedef struct { uint64_t frames, nonzero, callbacks, underruns, adpcm_refreshes; unsigned peak; int failed; } Ma5HwStats;
/* All DLL entry points, including load/init/terminate, run on one worker. */
Ma5Hw *ma5_hw_open(const char *dll, const char *wav, int speakers, char *error, unsigned error_size);
unsigned ma5_hw_read(Ma5Hw *hw, int data);
void ma5_hw_write(Ma5Hw *hw, int data, unsigned value);
void ma5_hw_fifo(Ma5Hw *hw,unsigned selector,const uint8_t *bytes,unsigned length);
int ma5_hw_generate(Ma5Hw *hw, unsigned frames);
/* Execute already-due FIFO commands without rendering or advancing time. */
void ma5_hw_pump(Ma5Hw *hw);
void ma5_hw_pause(Ma5Hw *hw, int paused);
void ma5_hw_reset(Ma5Hw *hw);
void ma5_hw_parameter(Ma5Hw *hw,unsigned address,unsigned value);
unsigned ma5_hw_memory(Ma5Hw *hw,unsigned address,int full_ram);
/* Pinned-DLL read-only PCM cursor: format in bits 24..25, sample index low 16.
   UINT32_MAX on an invalid voice or unsupported/unreadable native layout. */
unsigned ma5_hw_pcm_cursor(Ma5Hw *hw,unsigned voice);
/* Notify completed guest sample-RAM writes; refresh decoded ADPCM on wrap. */
void ma5_hw_ram_changed(Ma5Hw *hw,unsigned address,unsigned length);
void ma5_hw_register(Ma5Hw *hw,unsigned reg,unsigned value,unsigned bank);
int ma5_hw_irq(Ma5Hw *hw);
int ma5_hw_stress(Ma5Hw *hw);
Ma5HwStats ma5_hw_stats(Ma5Hw *hw);
void ma5_hw_diagnostics(Ma5Hw *hw);
void ma5_hw_close(Ma5Hw *hw);
#endif
