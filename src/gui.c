#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <math.h>
#include <wchar.h>
#include "machine.h"
#include "firmware.h"
#include "nvram.h"
#include "userfs.h"
#include "content.h"
#include "host.h"
#include "key_text.h"
#include "emu_speed.h"
#define ID_OPEN_MODEL 100
#define ID_OPEN_ROM 101
#define ID_RESET 102
#define ID_EXIT 103
#define ID_OPEN_SECOND 104
#define ID_OPEN_FOLDER 105
#define ID_NV_LOAD 106
#define ID_USERFS 107
#define ID_LOGGING 108
#define ID_FS_REFRESH 300
#define ID_FS_DIRECTORY 301
#define ID_FS_BACKUP 302
#define ID_FS_IMPORT 303
#define ID_FS_JAVA 304
#define ID_FS_ADD 305
#define ID_FS_REMOVE 306
#define ID_FS_STOP 307
#define ID_FS_SYSTEM 308
#define ID_RUN 110
#define ID_PAUSE 111
#define ID_STEP 112
#define ID_CONTINUE 113
#define ID_SIM 114
#define ID_AUDIO 115
#define ID_AUDIO_IMPORT 116
#define ID_HIGH_PRIORITY 117
#define ID_HOST_TIME 118
#define ID_JAVA_EXTENDED 119
#define ID_TEXT_INPUT 167
#define ID_CELL_ENABLED 160
#define ID_CELL_SMS 161
#define ID_CELL_CALL 162
#define ID_CELL_NETWORK 163
#define ID_GSM_OFF 164
#define ID_GSM_INTERNET 165
#define ID_GSM_GATEWAY 166
#define ID_BATTERY_PERCENT 140
#define ID_BATTERY_CABLE 141
#define ID_LCD_GHOSTING 150
#define ID_MA_DETUNE 151
#define ID_MA_WATER 152
#define ID_PATTERN 120
#define ID_ZOOM1 121
#define ID_ZOOM2 122
#define ID_ZOOM3 123
#define ID_DIAGNOSTICS 124
#define ID_ABOUT 130
#define ID_KEY_BASE 200
#define WM_PHONE_KEY (WM_APP + 1)
#define IMPORT_QUEUE_MAX 256
 typedef struct {
    char path[1024],name[CONTENT_NAME_MAX],error[180];
    uint32_t size,registry_id;unsigned type;int status;
    uint64_t started,accepted;
 } ImportItem;
typedef struct {
    Machine m;
    NvramStore storage;
    char sim_storage[1100];
    DWORD nv_checkpoint;
    HWND window, buttons[SGH_MAX_KEYS], diagnostics, diagnostic_edit,status;
    int status_height,status_running,status_clock,status_water;
    uint64_t status_guest;
    char status_state[48];
    EmuSpeed speed;
    HWND fs_window,fs_list,fs_status,fs_queue,fs_left_label,fs_right_label;
    ImportItem imports[IMPORT_QUEUE_MAX];unsigned import_count,import_current;
    int import_running,fs_system;
    UserDirectory fs_directory;
    ContentImport content; /* Armed by Import...; driven on the emulation thread. */
    HFONT font;
    FILE *log;
    char model[1024], rom[1024], second_rom[1024], nv_input[1024];
    RECT lcd;
    unsigned zoom;
    uint32_t mouse_keys, keyboard_keys, *test_pixels;
    uint32_t text_held;
    KeyText text_keys;
    int text_input;
    int vk_keys[256], running, continuation, pattern, menu_active, sim_override;
    const char *state;
    unsigned last_key;
    char audio_dll[1024], audio_settings[1024];
    uint32_t audio_ticks_hz;
    int audio_enabled, audio_paused, logging;
    unsigned audio_block;
    char emulator_settings[1024];
    int high_priority,host_time,java_extended;
    DWORD time_checkpoint;
    int recent_firmware;
    unsigned battery_percent;
    int charging;
    HWND battery_window,cell_editor,call_window,call_label;
    int cell_editor_kind,cell_enabled;unsigned gsm_mode,cell_revision;char cell_network[31];
    int ghosting;
    int ma_detune;
    int ma_water; /* Session-only: deliberately damaged sound is never a startup default. */
    uint32_t *ghost_pixels,*ghost_target,*ghost_published,*display_pixels;unsigned display_width,display_height;
    int ghost_published_ready;
    uint64_t ghost_writes,ghost_commands,ghost_changed_at,ghost_first_changed_at;
    int ghost_pending;
    DWORD ghost_tick;
    HICON led_icons[8];
    unsigned icon_led;
    int reload_defaults; /* set by the emulation thread, consumed by the GUI thread */
} App;
static App app;
/* Only the complete-frame handoff uses this lock, never CPU/audio work. */
static SRWLOCK presentation_lock=SRWLOCK_INIT;
/* Threading model:
   - The emulation thread (emu_main) steps the CPU and drives audio. With
     hardware audio on, the WaveOut queue's backpressure is the clock.
   - The GUI thread owns every window. It takes emu_mutex around any code that
     touches app.m / app.storage, and the emulation thread holds the same mutex
     while it runs a slice.
   - The emulation thread must NEVER call a GUI function (MessageBox,
     SendMessage, MoveWindow, load_model...). The GUI thread would be waiting
     for the mutex and both sides would deadlock. It posts WM_EMU_EVENT. */
#define WM_EMU_EVENT (WM_APP + 2)
#define EMU_RELOAD 1u
#define EMU_PROVISION_FAILED 2u
#define EMU_IMPORT_DONE 3u
#define EMU_IMPORT_FAILED 4u
static HANDLE emu_mutex, emu_thread;
static volatile LONG emu_quit;
#include "gui_profile.inc"
static void emu_lock(void)
{
    LARGE_INTEGER before={0},after={0};int track=profile_enabled!=0;
    if(track)QueryPerformanceCounter(&before);WaitForSingleObject(emu_mutex,INFINITE);
    if(track&&gui_profile.file){uint64_t ticks;QueryPerformanceCounter(&after);ticks=(uint64_t)(after.QuadPart-before.QuadPart);
        gui_profile.gui_wait+=ticks;if(ticks>gui_profile.max_gui_wait)gui_profile.max_gui_wait=ticks;}
}
static void emu_unlock(void) { ReleaseMutex(emu_mutex); }
static void update_ui(void);
/* Called only by the GUI thread. Never pair an old guest snapshot with a new
   wall timestamp when audio/CPU work temporarily owns the emulation mutex. */
static void status_refresh(int fresh)
{
    LARGE_INTEGER now,frequency;char text[80],speed[32];int running;
    if(!app.status)return;
    QueryPerformanceCounter(&now);QueryPerformanceFrequency(&frequency);
    if(fresh){
        running=app.running&&app.m.loaded&&!app.m.cpu.halted;
        if(running!=app.status_running)memset(&app.speed,0,sizeof(app.speed));
        app.status_running=running;app.status_clock=app.m.clock.attached;
        app.status_guest=app.m.clock.nanoseconds;app.status_water=app.ma_water;
        snprintf(app.status_state,sizeof(app.status_state),"%s",!app.m.loaded?"No firmware":
            app.m.cpu.halted?"Stopped (CPU)":running?"Running":
            app.state&&(strstr(app.state,"stopped")||strstr(app.state,"failed"))?"Stopped":"Paused");
    }
    if(fresh)emu_speed_sample(&app.speed,(uint64_t)now.QuadPart,(uint64_t)frequency.QuadPart,
        app.status_guest,app.status_running&&app.status_clock);
    else emu_speed_stale(&app.speed,(uint64_t)now.QuadPart,(uint64_t)frequency.QuadPart);
    snprintf(text,sizeof(text),"%s%s",app.status_state,app.status_water?" | Water":"");
    if(!app.status_running)strcpy(speed,"0.0%");
    else if(!app.speed.valid||!app.status_clock)strcpy(speed,"--%");
    else snprintf(speed,sizeof(speed),"%.1f%%",app.speed.percent);
    SendMessageA(app.status,SB_SETTEXTA,0,(LPARAM)text);
    SendMessageA(app.status,SB_SETTEXTA,1,(LPARAM)speed);
}
static int load_model(void);
static int save_storage(void);
#define filesystem_proc filesystem_proc_unlocked
#include "gui_fs.inc"
#undef filesystem_proc
static LRESULT CALLBACK filesystem_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result;
    /* Messages that can enter a modal move/size loop must not hold the lock. */
    switch (message) {
    case WM_SYSCOMMAND: case WM_NCLBUTTONDOWN: case WM_NCHITTEST: case WM_NCMOUSEMOVE:
    case WM_SETCURSOR: case WM_NCPAINT: case WM_NCACTIVATE:
        return filesystem_proc_unlocked(window, message, wparam, lparam);
    default: break;
    }
    emu_lock(); result = filesystem_proc_unlocked(window, message, wparam, lparam); emu_unlock();
    return result;
}
#include "gui_cellular.inc"
static void update_led_icon(void)
{
    unsigned state=app.m.phone_io.led&7u;
    if(app.m.ma3.led_tick>app.m.phone_io.led_tick)
        state=app.m.ma3.led_on?app.m.ma3.led_color:0;
    if(app.icon_led==state&&app.led_icons[state])return;
    if(!app.led_icons[state]){
        BITMAPINFO info={0};ICONINFO icon={0};uint32_t *pixels;HBITMAP color,mask;unsigned i;
        uint32_t rgb=((state&1)?0xff0000:0)|((state&2)?0x00ff00:0)|((state&4)?0x0000ff:0);
        info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=16;
        info.bmiHeader.biHeight=-16;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        color=CreateDIBSection(NULL,&info,DIB_RGB_COLORS,(void **)&pixels,NULL,0);
        mask=CreateBitmap(16,16,1,1,NULL);
        if(!color||!mask){if(color)DeleteObject(color);if(mask)DeleteObject(mask);return;}
        for(i=0;i<256;++i)pixels[i]=0xff000000u|rgb;
        icon.fIcon=TRUE;icon.hbmColor=color;icon.hbmMask=mask;
        app.led_icons[state]=CreateIconIndirect(&icon);DeleteObject(color);DeleteObject(mask);
    }
    if(app.led_icons[state]){
        SendMessageA(app.window,WM_SETICON,ICON_SMALL,(LPARAM)app.led_icons[state]);
        SendMessageA(app.window,WM_SETICON,ICON_BIG,(LPARAM)app.led_icons[state]);app.icon_led=state;
    }
}
static void ghost_frame(void)
{
    DWORD now=GetTickCount(),elapsed=now-app.ghost_tick;size_t i,n=(size_t)app.display_width*app.display_height;
    unsigned retain=(unsigned)(exp(-(double)elapsed/75.0)*256.0);
    const uint32_t *target=app.pattern?app.test_pixels:app.ghost_target;
    app.ghost_tick=now;if(!target||!app.ghost_pixels)return;
    for(i=0;i<n;++i){unsigned shift;uint32_t result=0;
        for(shift=0;shift<24;shift+=8){unsigned a=(app.ghost_pixels[i]>>shift)&255,b=(target[i]>>shift)&255;
            unsigned v=(a*retain+b*(256-retain)+128)>>8;
            if(a>b?a-b<=2:b-a<=2)v=b;result|=v<<shift;}
        app.ghost_pixels[i]=result;}
}
/* Prefer a settled guest redraw over a temporary clear. Continuous redraws
   use a bounded snapshot. Emulation thread only, with emu_mutex held. */
static void ghost_capture(void)
{
    uint64_t now=app.m.clock.nanoseconds;
    if(app.pattern||!app.ghost_published||!app.m.pixels)return;
    if(!app.m.clock.attached)app.ghost_pending=1;
    else if(app.ghost_writes!=app.m.lcd.writes||app.ghost_commands!=app.m.lcd.commands){
        app.ghost_writes=app.m.lcd.writes;app.ghost_commands=app.m.lcd.commands;
        if(!app.ghost_pending)app.ghost_first_changed_at=now;
        app.ghost_changed_at=now;app.ghost_pending=1;
    }
    /* Continuous drawing (e.g. games) must not starve presentation forever.
       Prefer a settled redraw, otherwise latch a consistent 30-Hz snapshot. */
    if(app.ghost_pending&&(!app.m.clock.attached||now-app.ghost_changed_at>=3000000||
        now-app.ghost_first_changed_at>=33333333)){
        AcquireSRWLockExclusive(&presentation_lock);
        memcpy(app.ghost_published,app.m.pixels,(size_t)app.m.width*app.m.height*sizeof(uint32_t));
        app.ghost_published_ready=1;ReleaseSRWLockExclusive(&presentation_lock);app.ghost_pending=0;
    }
}
/* GUI-only blending continues even while the CPU waits on an audio buffer.
   Never read app.m or its live framebuffer without the emulation mutex. */
static void presentation_tick(int fresh)
{
    size_t bytes=(size_t)app.display_width*app.display_height*sizeof(uint32_t);
    if(app.ghost_target&&app.ghost_published){int changed;
        AcquireSRWLockExclusive(&presentation_lock);
        changed=app.ghost_published_ready;
        if(app.ghost_published_ready){memcpy(app.ghost_target,app.ghost_published,bytes);app.ghost_published_ready=0;}
        ReleaseSRWLockExclusive(&presentation_lock);
        if(app.ghosting&&app.ghost_pixels)ghost_frame();
        if(app.display_pixels){const uint32_t *frame=app.ghosting&&app.ghost_pixels?app.ghost_pixels:
                app.pattern?app.test_pixels:app.ghost_target;
            if(frame)memcpy(app.display_pixels,frame,bytes);}
        if(app.window&&(changed||app.ghosting))InvalidateRect(app.window,&app.lcd,FALSE);
    }else if(fresh&&app.display_pixels){const uint32_t *frame=app.pattern?app.test_pixels:app.m.pixels;
        if(frame)memcpy(app.display_pixels,frame,bytes);
    }
}
static const char *key_names[SGH_MAX_KEYS] = {
    "Soft left", "Up", "Soft right", "Left", "OK", "Right", "Call", "Down", "End",
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#", "C / Back"
};
static void update_diagnostics(void)
{
    char text[4096];
    unsigned i;
    size_t length;
    if (!app.diagnostic_edit) return;
    length = (size_t)snprintf(text, sizeof(text),
        "Model: %s\r\nConfig: %s\r\nState: %s\r\n"
        "Instructions: %llu   scheduler ticks: %llu\r\nCPSR: %08X\r\n"
        "Unknown / ROM faults: %llu\r\nLast access: %c%u at %08X, value %08X, instruction PC %08X\r\n"
        "Keypad bits: %08X (guest active-low matrix)\r\n"
        "ROM images: %u (second mapped at %08X)\r\n"
        "Unplaced image: %s (%zu bytes)\r\n"
        "LCD: %s, display %s, commands %llu, pixel writes %llu\r\n"
        "CPU error: %s\r\n\r\n",
        app.m.name, app.model, app.state, (unsigned long long)app.m.cpu.instructions,
        (unsigned long long)app.m.cpu.cycles, app.m.cpu.cpsr, (unsigned long long)app.m.bus.faults,
        app.m.bus.last_write ? 'W' : 'R', app.m.bus.last_width * 8, app.m.bus.last_address,
        app.m.bus.last_value, app.m.bus.pc, app.m.keys,
        app.m.rom_count, app.m.rom_count > 1 ? app.m.roms[1].base : 0,
        *app.m.unplaced_path ? app.m.unplaced_path : "none", app.m.unplaced_size,
        app.m.lcd.attached ? "attached" : "not mapped", app.m.lcd.enabled ? "on" : "off",
        (unsigned long long)app.m.lcd.commands, (unsigned long long)app.m.lcd.writes,
        app.m.cpu.error ? app.m.cpu.error : "none");
    for (i = 0; i < 16 && length < sizeof(text); ++i)
        length += (size_t)snprintf(text + length, sizeof(text) - length, "R%-2u = %08X%s", i, app.m.cpu.r[i], i % 4 == 3 ? "\r\n" : "    ");
    for (i = 0; i < app.m.flash_count && length < sizeof(text); ++i) {
        const Flash *f = &app.m.flashes[i];
        length += (size_t)snprintf(text + length, sizeof(text) - length,
            "\r\nFlash %08X: %llu programs, %llu erases; last %08X / %08X\r\n",
            f->base, (unsigned long long)f->programs, (unsigned long long)f->erases, f->last_program, f->last_erase);
    }
    if (length < sizeof(text)) length += (size_t)snprintf(text + length, sizeof(text) - length,
        "\r\nMA-3 audio: %s; timebase %u %s/sec; FIFO bytes %llu, IRQs %llu, FIFO clears %llu (partial packet dropped %llu)\r\n"
        "Audio error: %s\r\n",
        app.m.ma3.hw?"experimental hardware":"silent", app.m.clock.attached?1000000000:app.audio_ticks_hz,
        app.m.clock.attached?"nanoseconds":"instruction ticks",
        (unsigned long long)app.m.ma3.fifo_bytes,(unsigned long long)app.m.ma3.irq_events,
        (unsigned long long)app.m.ma3.fifo_clears,(unsigned long long)app.m.ma3.partial_drops,
        *app.m.ma3.error?app.m.ma3.error:"none");
    if(app.m.clock.attached&&length<sizeof(text))length+=(size_t)snprintf(text+length,sizeof(text)-length,
        "PLL: ARM %u Hz / DSP %u Hz; core cycles %llu; time %.3f s%s\r\n",
        app.m.clock.arm_hz,app.m.clock.dsp_hz,(unsigned long long)app.m.cpu.clock_cycles,
        (double)app.m.clock.nanoseconds/1000000000.0,app.m.clock.unknown?" (unrecognized divider)":"");
    if (app.m.ma3.hw && length < sizeof(text)) {
        Ma5HwStats stats=ma5_hw_stats(app.m.ma3.hw);
        length += (size_t)snprintf(text + length,sizeof(text)-length,
            "PCM frames %llu, nonzero samples %llu, peak %u, host underruns %llu\r\n",
            (unsigned long long)stats.frames,(unsigned long long)stats.nonzero,stats.peak,
            (unsigned long long)stats.underruns);
    }
    if (length < sizeof(text)) length += (size_t)snprintf(text + length, sizeof(text) - length,
        "\r\nSIM: %s; resets %llu, APDUs %llu, RX %llu, TX %llu, DMA bytes %llu\r\n",
        app.m.sim.attached ? (app.m.sim.present ? "synthetic card inserted" : "removed") : "not mapped",
        (unsigned long long)app.m.sim.resets,(unsigned long long)app.m.sim.card.commands,
        (unsigned long long)app.m.sim.received,(unsigned long long)app.m.sim.transmitted,(unsigned long long)app.m.sim.dma_bytes);
    if(app.m.cellular.ready&&length<sizeof(text))length+=(size_t)snprintf(text+length,sizeof(text)-length,
        "Cellular: %s; network %s; call state %u; SMS received %llu / sent %llu; last recipient %s\r\n"
        "Packet data: mode %u; UDP TX %llu / RX %llu; %s\r\n",
        app.m.cellular.enabled?"enabled":"off",app.m.cellular.network,app.m.cellular.call_state,
        (unsigned long long)app.m.cellular.received_sms,(unsigned long long)app.m.cellular.sent_sms,app.m.cellular.last_sms_number,
        app.m.network.mode,(unsigned long long)app.m.network.transmitted,(unsigned long long)app.m.network.received,
        *app.m.network.error?app.m.network.error:"no transport error");
    if (length < sizeof(text)) snprintf(text + length, sizeof(text) - length,
        "\r\nSee build/gui-unmapped.log for every unknown access.\r\n"
        "LCD output comes only from guest command/data writes. Keypad scanning/debounce runs in firmware.\r\n"
        "Diagnostic continuation returns zero on unknown reads and drops writes.\r\n"
        "Clock-enabled profiles use core-cycle time; external bus waits remain uncalibrated.");
    SetWindowTextA(app.diagnostic_edit, text);
}
static void update_ui(void)
{
    HMENU menu = GetMenu(app.window);
    unsigned i;
    CheckMenuItem(menu, ID_CONTINUE, MF_BYCOMMAND | (app.continuation ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_PATTERN, MF_BYCOMMAND | (app.pattern ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SIM, MF_BYCOMMAND | (app.m.sim.present ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu,ID_LCD_GHOSTING,MF_BYCOMMAND|(app.ghosting?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(menu,ID_MA_DETUNE,MF_BYCOMMAND|(app.ma_detune?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(menu,ID_MA_WATER,MF_BYCOMMAND|(app.ma_water?MF_CHECKED:MF_UNCHECKED));
    phone_io_pause(&app.m.phone_io,!app.running);
    CheckMenuItem(menu, ID_AUDIO, MF_BYCOMMAND | (app.audio_enabled ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_HIGH_PRIORITY, MF_BYCOMMAND | (app.high_priority ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_TEXT_INPUT, MF_BYCOMMAND | (app.text_input ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu,ID_LOGGING,MF_BYCOMMAND|(app.logging?MF_CHECKED:MF_UNCHECKED));
    CheckMenuItem(menu, ID_HOST_TIME, MF_BYCOMMAND | (app.host_time ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu,ID_CELL_ENABLED,MF_BYCOMMAND|(app.cell_enabled?MF_CHECKED:MF_UNCHECKED));
    CheckMenuRadioItem(menu,ID_GSM_OFF,ID_GSM_GATEWAY,ID_GSM_OFF+app.gsm_mode,MF_BYCOMMAND);
    EnableMenuItem(menu,ID_CELL_ENABLED,MF_BYCOMMAND|(app.m.cellular.ready?MF_ENABLED:MF_GRAYED));
    EnableMenuItem(menu,ID_CELL_NETWORK,MF_BYCOMMAND|(app.m.cellular.ready?MF_ENABLED:MF_GRAYED));
    {unsigned id;int available=app.m.cellular.enabled&&app.m.cellular.name_ptr&&app.m.sim.present;
     for(id=ID_CELL_SMS;id<=ID_CELL_CALL;++id)EnableMenuItem(menu,id,MF_BYCOMMAND|(available?MF_ENABLED:MF_GRAYED));}
    {unsigned id;for(id=ID_GSM_OFF;id<=ID_GSM_GATEWAY;++id)EnableMenuItem(menu,id,MF_BYCOMMAND|(app.m.network.txn_start?MF_ENABLED:MF_GRAYED));}
    CheckMenuItem(menu, ID_JAVA_EXTENDED, MF_BYCOMMAND | (app.java_extended ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, ID_JAVA_EXTENDED, MF_BYCOMMAND | (app.m.java_buffer_get ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_HOST_TIME, MF_BYCOMMAND | (app.m.rtc.attached ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_BATTERY_PERCENT, MF_BYCOMMAND | (app.m.battery.enabled ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_BATTERY_CABLE, MF_BYCOMMAND | ((app.m.battery.enabled || app.m.battery.charger_port) ? MF_ENABLED : MF_GRAYED));
    CheckMenuItem(menu, ID_BATTERY_CABLE, MF_BYCOMMAND | (app.charging ? MF_CHECKED : MF_UNCHECKED));
    if (app.m.ma3.hw && app.audio_paused != !app.running) {
        ma5_hw_pause(app.m.ma3.hw, !app.running); app.audio_paused = !app.running;
    }
    EnableMenuItem(menu, ID_SIM, MF_BYCOMMAND | (app.m.sim.attached ? MF_ENABLED : MF_GRAYED));
    CheckMenuRadioItem(menu, ID_ZOOM1, ID_ZOOM3, ID_ZOOM1 + app.zoom - 1, MF_BYCOMMAND);
    EnableMenuItem(menu, ID_RUN, MF_BYCOMMAND | (app.m.loaded && !app.running && !app.m.cpu.halted ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_PAUSE, MF_BYCOMMAND | (app.running ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_STEP, MF_BYCOMMAND | (app.m.loaded && !app.running && !app.m.cpu.halted ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_NV_LOAD, MF_BYCOMMAND | (app.m.loaded && app.m.nvram_data ? MF_ENABLED : MF_GRAYED));
    for (i = 0; i < SGH_MAX_KEYS; ++i) EnableWindow(app.buttons[i], app.m.loaded);
    update_diagnostics();status_refresh(1);
}
static void release_keys(void)
{
    unsigned i;
    key_text_reset(&app.text_keys);app.text_held=0;
    app.mouse_keys = app.keyboard_keys = 0; app.m.keys = 0;
    for (i = 0; i < SGH_MAX_KEYS; ++i) machine_key(&app.m, i, 0);
    for (i = 0; i < 256; ++i) app.vk_keys[i] = -1;
    for (i = 0; i < SGH_MAX_KEYS; ++i) if (app.buttons[i]) SendMessageA(app.buttons[i], BM_SETSTATE, 0, 0);
}
static void phone_key(unsigned key, int down, int mouse)
{
    uint32_t bit, *source;
    if (!app.m.loaded || key >= SGH_MAX_KEYS) return;
    bit = 1u << key; source = mouse ? &app.mouse_keys : &app.keyboard_keys;
    if (down) { *source |= bit; app.last_key = key; } else *source &= ~bit;
    machine_key(&app.m, key, !!((app.mouse_keys | app.keyboard_keys | app.text_held) & bit));
    if (!mouse) SendMessageA(app.buttons[key], BM_SETSTATE, !!((app.mouse_keys | app.keyboard_keys) & bit), 0);
    update_ui();
}
static LRESULT CALLBACK button_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id, DWORD_PTR data)
{
    (void)data;
    if (message == WM_LBUTTONDOWN) SendMessageA(app.window, WM_PHONE_KEY, id, 1);
    if (message == WM_LBUTTONUP || message == WM_CAPTURECHANGED || message == WM_CANCELMODE)
        SendMessageA(app.window, WM_PHONE_KEY, id, 0);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, button_proc, id);
    return DefSubclassProc(window, message, wparam, lparam);
}
static int keyboard_key(WPARAM vk)
{
    if (vk >= '1' && vk <= '9') {
        if ((GetKeyState(VK_SHIFT) & 0x8000) && vk == '3') return 20;
        if ((GetKeyState(VK_SHIFT) & 0x8000) && vk == '8') return 18;
        return 9 + (int)(vk - '1');
    }
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return 9 + (int)(vk - VK_NUMPAD1);
    switch (vk) {
    case '0': case VK_NUMPAD0: return 19; case VK_MULTIPLY: return 18;
    case VK_F1: return 0; case VK_F2: return 2;
    case VK_UP: return 1; case VK_LEFT: return 3; case VK_RIGHT: return 5; case VK_DOWN: return 7;
    case VK_RETURN: case VK_SPACE: return 4;
    case VK_F3: return 6; case VK_F4: return 8; case VK_BACK: return 21;
    default: return -1;
    }
}
static void text_step(void)
{
    unsigned key;int down;uint32_t bit;
    if(!key_text_tick(&app.text_keys,app.m.clock.nanoseconds,&key,&down))return;
    bit=1u<<key;if(down){app.text_held|=bit;app.last_key=key;}else app.text_held&=~bit;
    machine_key(&app.m,key,!!((app.mouse_keys|app.keyboard_keys|app.text_held)&bit));
}
static void layout(void)
{
    RECT client;
    int width, screen, grid_y, button_width, i;
    GetClientRect(app.window, &client); width = client.right;
    if(app.status){RECT bar;int parts[2];
        SendMessageA(app.status,WM_SIZE,0,0);GetWindowRect(app.status,&bar);
        app.status_height=bar.bottom-bar.top;
        parts[0]=width>160?width-90:width/2;parts[1]=-1;
        SendMessageA(app.status,SB_SETPARTS,2,(LPARAM)parts);}
    screen = (int)(app.m.loaded ? app.m.width : 128) * (int)app.zoom;
    app.lcd.left = (width - screen) / 2; app.lcd.top = 0;
    app.lcd.right = app.lcd.left + screen;
    app.lcd.bottom = (int)(app.m.loaded ? app.m.height : 128) * (int)app.zoom;
    grid_y = app.lcd.bottom + 4; button_width = (width - 8) / 3;
    for (i = 0; i < 21; ++i) MoveWindow(app.buttons[i], 2 + (i % 3) * (button_width + 2),
        grid_y + ((i / 3) + (i >= 9 ? 1 : 0)) * 36, button_width, 34, TRUE);
    MoveWindow(app.buttons[21], 2, grid_y + 108, width - 4, 34, TRUE);
    InvalidateRect(app.window, NULL, FALSE);
}
static void fit_window(void)
{
    RECT rect, current, work;
    int left, top;
    int width = (int)(app.m.loaded ? app.m.width : 128) * (int)app.zoom;
    int height = (int)(app.m.loaded ? app.m.height : 128) * (int)app.zoom + 292 + app.status_height;
    if (width < 256) width = 256;
    SetRect(&rect, 0, 0, width, height);
    AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, TRUE, 0);
    GetWindowRect(app.window, &current); SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    left = current.left; top = current.top;
    if (left + rect.right - rect.left > work.right) left = work.right - (rect.right - rect.left);
    if (top + rect.bottom - rect.top > work.bottom) top = work.bottom - (rect.bottom - rect.top);
    if (left < work.left) left = work.left;
    if (top < work.top) top = work.top;
    SetWindowPos(app.window, NULL, left, top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
    layout();
}
static int save_storage(void)
{
    if (nvram_store_flush(&app.storage, &app.m)) {
        if(!app.m.sim.attached||!*app.sim_storage||sim_card_save_storage(&app.m.sim.card,app.sim_storage))return 1;
        snprintf(app.m.error,sizeof(app.m.error),"%s",app.m.sim.card.error);
    }
    app.running = 0; app.state = "Paused - NVM save failed";
    MessageBoxA(app.window, app.m.error, "SGHemu - automatic NVM save failed", MB_OK | MB_ICONERROR);
    update_ui(); return 0;
}
/* Called with emu_mutex held: neither the CPU nor MA port tracing can write
   while their FILE pointers are replaced. Enabling is all-or-nothing. */
static int apply_logging(int enabled)
{
    FILE *unmapped=NULL,*traffic=NULL,*profile=NULL;
    if(enabled){
        unmapped=fopen("build/gui-unmapped.log","a");
        if(unmapped)traffic=fopen("build/gui-ma3-traffic.log","a");
        if(traffic)profile=fopen("build/gui-profile.log","a");
        if(!unmapped||!traffic||!profile){if(unmapped)fclose(unmapped);if(traffic)fclose(traffic);if(profile)fclose(profile);return 0;}
        (void)setvbuf(unmapped,NULL,_IOFBF,65536);
        (void)setvbuf(traffic,NULL,_IOFBF,65536);
        fputs("# GUI logging enabled\n",unmapped);fputs("# GUI logging enabled\n",traffic);
    }
    profile_close();
    app.m.bus.log=NULL;
    if(app.log)fclose(app.log);
    if(app.m.ma3.trace)fclose(app.m.ma3.trace);
    app.log=unmapped;app.m.bus.log=unmapped;app.m.ma3.trace=traffic;
    app.m.network.trace=unmapped;
    if(profile)profile_start(profile);
    app.logging=!!enabled;return 1;
}
static int load_model(void)
{
    Machine pattern = {0};
    app.running = 0; release_keys();
    if (!save_storage()) {
        if (app.m.loaded) {
            strcpy(app.model, app.m.config_path);
            if (app.m.rom_count) strcpy(app.rom, app.m.roms[0].path);
            if (app.m.rom_count > 1) strcpy(app.second_rom, app.m.roms[1].path);
        }
        *app.nv_input = 0; return 0;
    }
    free(app.ghost_pixels);app.ghost_pixels=NULL;free(app.ghost_target);app.ghost_target=NULL;
    free(app.ghost_published);app.ghost_published=NULL;app.ghost_published_ready=0;
    free(app.display_pixels);app.display_pixels=NULL;app.display_width=app.display_height=0;
    app.ghost_writes=app.ghost_commands=app.ghost_changed_at=0;app.ghost_pending=0;
    profile_close();
    nvram_store_release(&app.storage);
    machine_destroy(&app.m);
    if (app.log) { fflush(app.log); fclose(app.log); app.log = NULL; }
    app.log = NULL;
    if (!machine_load_pair(&app.m, app.model, *app.rom ? app.rom : NULL,
        *app.second_rom ? app.second_rom : NULL, app.log)) {
        app.state = "No firmware loaded";
        MessageBoxA(app.window, app.m.error, "SGHemu - model load failed", MB_OK | MB_ICONERROR);
    } else {
        app.state = "Paused - ready"; app.continuation = app.m.bringup;
        if (app.sim_override >= 0 && app.m.sim.attached) app.m.sim.present = app.sim_override;
        if (!nvram_store_open(&app.storage, &app.m, NULL) ||
            (*app.nv_input && !nvram_store_import(&app.storage, &app.m, app.nv_input))) {
            app.state = "Paused - NVM storage rejected";
            app.m.cpu.halted = 1; app.m.cpu.error = "Persistent NVM error; see diagnostics";
            MessageBoxA(app.window, app.m.error, "SGHemu - NVM load failed", MB_OK | MB_ICONERROR);
        }
        *app.nv_input = 0;
        *app.sim_storage=0;
        if(app.storage.active&&app.m.sim.attached){
            snprintf(app.sim_storage,sizeof(app.sim_storage),"%s.sim",app.storage.path);
            if(!sim_card_load_storage(&app.m.sim.card,app.sim_storage)){
                app.running=0;app.m.cpu.halted=1;app.m.cpu.error="SIM storage load failed";*app.sim_storage=0;
                MessageBoxA(app.window,app.m.sim.card.error,"SGHemu - SIM storage",MB_OK|MB_ICONERROR);
            }
        }
        app.nv_checkpoint = GetTickCount();
        battery_set(&app.m.battery,app.battery_percent,app.charging);
        app.m.java_extended=app.java_extended;app.cell_revision=~0u;
        if(app.m.cellular.ready){(void)cellular_set_network(&app.m.cellular,&app.m.bus,app.cell_enabled,app.cell_network);
            app.m.cellular.mode=app.gsm_mode;phone_net_set_mode(&app.m.network,app.cell_enabled?app.gsm_mode:0);}
        if(app.m.rtc.attached){
            rtc_set_sync(&app.m.rtc,app.host_time);
            if(app.host_time&&!host_sync_rtc(&app.m.rtc)){
                MessageBoxA(app.window,"The host date is outside this model's RTC year range.","SGHemu - clock sync",MB_OK|MB_ICONERROR);
                rtc_set_sync(&app.m.rtc,0);
            }
            app.time_checkpoint=GetTickCount();
        }
        if (app.audio_enabled && app.m.ma3.attached && !app.m.cpu.halted) {
            if (!ma3_enable(&app.m.ma3, app.audio_dll, NULL, 1, app.m.clock.attached?1000000000:app.audio_ticks_hz)) {
                MessageBoxA(app.window, app.m.ma3.error, "SGHemu - hardware audio unavailable", MB_OK | MB_ICONERROR);
                *app.m.ma3.error=0; app.audio_enabled=0;
            } else {
                app.audio_paused=0;
                if(!ma3_set_block(&app.m.ma3,app.audio_block))app.audio_block=480;

            }
        }
    }
    if(!apply_logging(app.logging)){
        app.logging=0;
        WritePrivateProfileStringA("diagnostics","logging","0",app.emulator_settings);
        MessageBoxA(app.window,"Could not open the diagnostic logs in build. Logging remains off.","SGHemu - logging",MB_OK|MB_ICONERROR);
    }
    free(app.test_pixels); app.test_pixels = NULL; app.pattern = 0;
    if (app.m.loaded) {
        app.test_pixels = calloc((size_t)app.m.width * app.m.height, sizeof(uint32_t));
        pattern.width = app.m.width; pattern.height = app.m.height; pattern.pixels = app.test_pixels;
        machine_pattern(&pattern, 1);
        /* MA3_ACCESS_WATCH=start,end[;start,end...] arms the same bus watch used by
           --mmio-trace over arbitrary guest ranges, writes only, so a RAM state
           field can be attributed to the guest PC that changed it. Multiple ranges
           (semicolon separated) allow correlating the MA-3 driver block and the
           sequencer block on the same tick timeline. Diagnostics only. Do NOT call
           setvbuf here: MSVC's CRT fast-fails on _IOLBF with size 0. A normal exit
           flushes the stream, so close the window rather than killing it. */
        {
            const char *range = getenv("MA3_ACCESS_WATCH");
            if (range && *range) {
                FILE *log = fopen("build/gui-ram-watch.log", "w");
                if (log) {
                    if (bus_watch_parse(&app.m.bus, range)) {
                        app.m.bus.access_log = log;
                        app.m.bus.watch_writes_only = 1;
                    } else fclose(log);
                }
            }
        }
    }
    app.last_key = SGH_MAX_KEYS; fit_window(); update_ui();
    if(app.m.loaded&&!app.m.cpu.halted&&app.recent_firmware){
        char model_path[1024],rom_path[1024],second_path[1024];
        GetFullPathNameA(app.model,sizeof(model_path),model_path,NULL);
        if(*app.rom)GetFullPathNameA(app.rom,sizeof(rom_path),rom_path,NULL);else *rom_path=0;
        if(*app.second_rom)GetFullPathNameA(app.second_rom,sizeof(second_path),second_path,NULL);else *second_path=0;
        CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("recent_firmware","model",model_path,app.emulator_settings);
        WritePrivateProfileStringA("recent_firmware","rom1",rom_path,app.emulator_settings);
        WritePrivateProfileStringA("recent_firmware","rom2",second_path,app.emulator_settings);
    }
    if(app.m.loaded){size_t bytes=(size_t)app.m.width*app.m.height*sizeof(uint32_t);
        app.ghost_pixels=malloc(bytes);if(app.ghost_pixels)memcpy(app.ghost_pixels,app.m.pixels,bytes);
        app.ghost_target=malloc(bytes);if(app.ghost_target)memcpy(app.ghost_target,app.m.pixels,bytes);
        else {free(app.ghost_pixels);app.ghost_pixels=NULL;}
        app.ghost_published=malloc(bytes);if(app.ghost_published)memcpy(app.ghost_published,app.m.pixels,bytes);
        app.display_pixels=malloc(bytes);if(app.display_pixels){memcpy(app.display_pixels,app.m.pixels,bytes);app.display_width=app.m.width;app.display_height=app.m.height;}
        app.ghost_tick=GetTickCount();
        app.m.phone_io.mixed=app.m.ma3.hw!=NULL;
        if(!phone_io_open(&app.m.phone_io,!app.m.phone_io.mixed,NULL))app.state="CPU tone output unavailable";
        if(app.m.phone_io.mixed)ma5_hw_set_mixer(app.m.ma3.hw,phone_io_mix,&app.m.phone_io);
        ma5_hw_set_detune(app.m.ma3.hw,app.ma_detune&&strstr(app.m.name,"C100")!=NULL);
        ma3_set_water(&app.m.ma3,app.ma_water);memset(&app.speed,0,sizeof(app.speed));
        update_led_icon();}
    return app.m.loaded && !app.m.cpu.halted;
}
static void choose_file(int firmware)
{
    OPENFILENAMEA ofn = {0};
    char path[1024] = {0};
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = app.window;
    ofn.lpstrFilter = firmware ? "Firmware images (*.bin;*.rom)\0*.bin;*.rom\0All files\0*.*\0" : "SGH model configs (*.cfg)\0*.cfg\0All files\0*.*\0";
    ofn.lpstrFile = path; ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = firmware == 2 ? "Open second firmware image (unplaced)" : firmware ? "Open first firmware image" : "Open model configuration";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    app.running = 0; app.state = "Paused"; release_keys(); update_ui();
    if (GetOpenFileNameA(&ofn)) {
        *app.nv_input = 0;
        content_import_release(&app.content);
        if (firmware == 2) strcpy(app.second_rom, path);
        else if (firmware) strcpy(app.rom, path);
        else { strcpy(app.model, path); *app.rom = 0; *app.second_rom = 0; }
        app.recent_firmware=1;
        load_model();
    }
}
static void choose_nvram(void)
{
    OPENFILENAMEA ofn = {0};
    char path[1024] = {0};
    if (!app.m.loaded || !app.m.nvram_data) return;
    app.running = 0; release_keys(); app.state = "Paused"; update_ui();
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = app.window;
    ofn.lpstrFilter = "SGH NVM snapshots (*.snv)\0*.snv\0All files\0*.*\0";
    ofn.lpstrDefExt = "snv"; ofn.lpstrFile = path; ofn.nMaxFile = sizeof(path);
    ofn.lpstrTitle = "Import NVM snapshot and reset phone";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) { strcpy(app.nv_input, path); load_model(); }
    update_ui(); InvalidateRect(app.window, &app.lcd, FALSE);
}
static void choose_folder(void)
{
    IFileOpenDialog *dialog = NULL;
    IShellItem *item = NULL;
    PWSTR wide = NULL;
    char folder[1024];
    BOOL substituted = FALSE;
    FirmwareMatch *matches;
    unsigned count, chosen = 0, i;
    app.running = 0; app.state = "Paused"; release_keys(); update_ui();
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        &IID_IFileOpenDialog, (void **)&dialog))) return;
    IFileOpenDialog_SetOptions(dialog, FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    IFileOpenDialog_SetTitle(dialog, L"Open firmware folder");
    if (SUCCEEDED(IFileOpenDialog_Show(dialog, app.window)) &&
        SUCCEEDED(IFileOpenDialog_GetResult(dialog, &item))) IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide);
    if (item) IShellItem_Release(item);
    IFileOpenDialog_Release(dialog);
    if (!wide) return;
    count = (unsigned)WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide, -1, folder, sizeof(folder), NULL, &substituted);
    CoTaskMemFree(wide);
    if (!count || substituted) { MessageBoxA(app.window, "The selected path cannot be represented by this build's ANSI file APIs.", "Firmware folder", MB_OK | MB_ICONERROR); return; }
    matches = calloc(FIRMWARE_MAX_MATCHES, sizeof(*matches)); if (!matches) return;
    SetCursor(LoadCursor(NULL, IDC_WAIT));
    count = firmware_scan(folder, "models", matches, FIRMWARE_MAX_MATCHES);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    if (!count) {
        MessageBoxA(app.window, "No complete supported ROM set found. Both images must match a model's SHA-256 profile.\n\nRenamed files are supported. A .lst symbol listing alone is not a bootable ROM.\nUnknown ROMs need a model profile before booting.", "Firmware folder", MB_OK | MB_ICONINFORMATION);
    } else {
        if (count > 1) {
            HMENU menu = CreatePopupMenu(); POINT point;
            for (i = 0; i < count; ++i) AppendMenuA(menu, MF_STRING, i + 1, matches[i].name);
            GetCursorPos(&point);
            chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, 0, app.window, NULL);
            DestroyMenu(menu);
        } else chosen = 1;
        if (chosen && chosen <= count) {
            FirmwareMatch *match = &matches[chosen - 1];
            *app.nv_input = 0;
            strcpy(app.model, match->model); strcpy(app.rom, match->rom[0]); strcpy(app.second_rom, match->rom[1]);
            app.recent_firmware=1;
            if (load_model()) {
                app.continuation = app.m.bringup; app.running = 1;
                app.state = app.m.bringup ? "Running - experimental hardware bring-up" : "Running";
                update_ui();
            }
        }
    }
    free(matches);
}
static LRESULT CALLBACK diagnostics_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CREATE) {
        app.diagnostic_edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0, window, NULL, GetModuleHandleA(NULL), NULL);
        SendMessageA(app.diagnostic_edit, WM_SETFONT, (WPARAM)GetStockObject(ANSI_FIXED_FONT), TRUE); update_diagnostics(); return 0;
    }
    if (message == WM_SIZE) { MoveWindow(app.diagnostic_edit, 0, 0, LOWORD(lparam), HIWORD(lparam), TRUE); return 0; }
    if (message == WM_DESTROY) { app.diagnostics = app.diagnostic_edit = NULL; return 0; }
    return DefWindowProcA(window, message, wparam, lparam);
}
static void import_audio(void)
{
    OPENFILENAMEA ofn={0}; char path[1024]={0};
    app.running=0; app.state="Paused"; update_ui();
    ofn.lStructSize=sizeof(ofn);ofn.hwndOwner=app.window;
    ofn.lpstrFilter="Yamaha hardware DLL (M5_EmuHw.dll)\0M5_EmuHw.dll\0All files\0*.*\0";
    ofn.lpstrFile=path;ofn.nMaxFile=sizeof(path);
    ofn.lpstrTitle="Import pinned Yamaha hardware DLL - experimental MA-3 audio";
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameA(&ofn))return;
    strcpy(app.audio_dll,path);app.audio_enabled=1;
    if(load_model()) {
        CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("audio","hardware_dll",path,app.audio_settings);
        WritePrivateProfileStringA("audio","enabled",app.audio_enabled?"1":"0",app.audio_settings);
    }
}
static LRESULT CALLBACK battery_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam)
{
    (void)lparam;
    if(message==WM_CREATE){char text[16];app.battery_window=window;
        CreateWindowA("STATIC","Battery charge (0-100%):",WS_CHILD|WS_VISIBLE,12,14,190,20,window,NULL,NULL,NULL);
        snprintf(text,sizeof(text),"%u",app.battery_percent);
        CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_NUMBER|ES_AUTOHSCROLL,
            12,40,190,24,window,(HMENU)10,NULL,NULL);
        CreateWindowA("BUTTON","Apply",WS_CHILD|WS_VISIBLE|WS_TABSTOP,12,78,90,26,window,(HMENU)1,NULL,NULL);
        CreateWindowA("BUTTON","Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,112,78,90,26,window,(HMENU)2,NULL,NULL);return 0;
    }
    if(message==WM_COMMAND&&LOWORD(wparam)==1){BOOL valid;unsigned percent=GetDlgItemInt(window,10,&valid,FALSE);
        if(!valid||percent>100){MessageBoxA(window,"Enter a percentage from 0 to 100.","Battery",MB_OK|MB_ICONINFORMATION);return 0;}
        emu_lock();app.battery_percent=percent;battery_set(&app.m.battery,percent,app.charging);emu_unlock();
        {char text[16];snprintf(text,sizeof(text),"%u",percent);CreateDirectoryA("state",NULL);
            WritePrivateProfileStringA("battery","percent",text,app.emulator_settings);}
        DestroyWindow(window);return 0;
    }
    if(message==WM_COMMAND&&LOWORD(wparam)==2){DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){app.battery_window=NULL;return 0;}
    return DefWindowProcA(window,message,wparam,lparam);
}
static void command(unsigned id)
{
    switch (id) {
    case ID_OPEN_MODEL: choose_file(0); break;
    case ID_OPEN_ROM: choose_file(1); break;
    case ID_OPEN_SECOND: choose_file(2); break;
    case ID_OPEN_FOLDER: choose_folder(); break;
    case ID_RESET: load_model(); break;
    case ID_LOGGING:
        if(!apply_logging(!app.logging)){
            MessageBoxA(app.window,"Could not open the diagnostic logs in build. Logging was not changed.","SGHemu - logging",MB_OK|MB_ICONERROR);break;
        }
        CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("diagnostics","logging",app.logging?"1":"0",app.emulator_settings);break;
    case ID_LCD_GHOSTING:
        app.ghosting=!app.ghosting;app.ghost_tick=GetTickCount();
        if(app.ghost_pixels&&app.m.pixels)memcpy(app.ghost_pixels,app.pattern?app.test_pixels:app.m.pixels,
            (size_t)app.m.width*app.m.height*sizeof(uint32_t));
        if(app.ghost_target&&app.m.pixels)memcpy(app.ghost_target,app.m.pixels,(size_t)app.m.width*app.m.height*sizeof(uint32_t));
        if(app.ghost_published&&app.m.pixels)memcpy(app.ghost_published,app.m.pixels,(size_t)app.m.width*app.m.height*sizeof(uint32_t));
        app.ghost_published_ready=0;
        app.ghost_writes=app.m.lcd.writes;app.ghost_commands=app.m.lcd.commands;app.ghost_pending=0;
        CreateDirectoryA("state",NULL);WritePrivateProfileStringA("look_and_feel","lcd_ghosting",app.ghosting?"1":"0",app.emulator_settings);
        InvalidateRect(app.window,&app.lcd,FALSE);break;
    case ID_BATTERY_PERCENT:
        if(!app.m.battery.enabled)break;
        if(!app.battery_window)CreateWindowExA(0,"SGHemuBattery","Battery percentage",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,230,150,app.window,NULL,GetModuleHandleA(NULL),NULL);
        else {ShowWindow(app.battery_window,SW_RESTORE);SetForegroundWindow(app.battery_window);}break;
    case ID_BATTERY_CABLE:
        if(!app.m.battery.enabled&&!app.m.battery.charger_port)break;
        app.charging=!app.charging;battery_set(&app.m.battery,app.battery_percent,app.charging);
        CreateDirectoryA("state",NULL);WritePrivateProfileStringA("battery","charging",app.charging?"1":"0",app.emulator_settings);break;
    case ID_CELL_ENABLED:
        if(app.m.cellular.ready&&cellular_set_network(&app.m.cellular,&app.m.bus,!app.cell_enabled,app.cell_network)){
            app.cell_enabled=!app.cell_enabled;phone_net_set_mode(&app.m.network,app.cell_enabled?app.gsm_mode:0);cell_save_settings();}break;
    case ID_CELL_SMS: cell_open_editor(2);break;
    case ID_CELL_CALL: cell_open_editor(3);break;
    case ID_CELL_NETWORK: cell_open_editor(1);break;
    case ID_GSM_OFF:case ID_GSM_INTERNET:case ID_GSM_GATEWAY:
        app.gsm_mode=id-ID_GSM_OFF;app.m.cellular.mode=app.gsm_mode;phone_net_set_mode(&app.m.network,app.cell_enabled?app.gsm_mode:0);cell_save_settings();
        if(id==ID_GSM_GATEWAY)MessageBoxA(app.window,
            "In the phone's WAP browser profile settings, enter:\r\n\r\n"
            "Gateway IP address: 10.0.0.1\r\nSecurity: Non-secure\r\nPort (if shown): 9201\r\n\r\n"
            "On the C100, use # to enter the dots in the IP address.\r\n\r\n"
            "Test homepage: http://sghemu.local/\r\n\r\n"
            "Make sure Cellular is enabled, then open the phone's WAP browser.",
            "SGHemu - Set up the built-in WAP gateway",MB_OK|MB_ICONINFORMATION);break;
    case ID_JAVA_EXTENDED:
        app.java_extended=!app.java_extended;CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("emulation","extended_java_buffers",app.java_extended?"1":"0",app.emulator_settings);break;
    case ID_TEXT_INPUT:
        release_keys();app.text_input=!app.text_input;CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("emulation","keyboard_text_input",app.text_input?"1":"0",app.emulator_settings);break;
    case ID_HIGH_PRIORITY:
        if(!host_high_priority(!app.high_priority)){
            char message[160];snprintf(message,sizeof(message),"Windows could not change process priority (error %lu).",(unsigned long)GetLastError());
            MessageBoxA(app.window,message,"SGHemu - process priority",MB_OK|MB_ICONERROR);break;
        }
        app.high_priority=!app.high_priority;CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("emulation","high_priority",app.high_priority?"1":"0",app.emulator_settings);break;
    case ID_HOST_TIME:
        if(!app.m.rtc.attached)break;
        if(!app.host_time&&!host_sync_rtc(&app.m.rtc)){
            MessageBoxA(app.window,"The host date is outside this model's RTC year range.","SGHemu - clock sync",MB_OK|MB_ICONERROR);break;}
        app.host_time=!app.host_time;rtc_set_sync(&app.m.rtc,app.host_time);
        app.time_checkpoint=GetTickCount();CreateDirectoryA("state",NULL);
        WritePrivateProfileStringA("emulation","sync_time",app.host_time?"1":"0",app.emulator_settings);break;
    case ID_AUDIO_IMPORT: import_audio(); break;
    case ID_AUDIO:
        if (!app.audio_enabled && !*app.audio_dll) import_audio();
        else {
            app.audio_enabled=!app.audio_enabled;
            load_model();
            CreateDirectoryA("state",NULL);
            WritePrivateProfileStringA("audio","enabled",app.audio_enabled?"1":"0",app.audio_settings);
        }
        break;
    case ID_MA_DETUNE:
        app.ma_detune=!app.ma_detune;
        ma5_hw_set_detune(app.m.ma3.hw,app.ma_detune&&strstr(app.m.name,"C100")!=NULL);
        CreateDirectoryA("state",NULL);WritePrivateProfileStringA("look_and_feel","c100_ma_detune",app.ma_detune?"1":"0",app.emulator_settings);break;
    case ID_MA_WATER:
        app.ma_water=!app.ma_water;ma3_set_water(&app.m.ma3,app.ma_water);break;
    case ID_SIM:
        if (app.m.sim.attached && save_storage()) {
            int running=app.running,previous=app.sim_override;
            app.sim_override=!app.m.sim.present;
            if(load_model()) {app.running=running;app.state=running?"Running":"Paused - ready";}
            else app.sim_override=previous;
            update_ui();
        }
        break;
    case ID_NV_LOAD: choose_nvram(); break;
    case ID_USERFS:
        if(!app.fs_window)CreateWindowExA(0,"SGHemuFilesystem","SGHemu - phone filesystem manager",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,1020,580,app.window,NULL,GetModuleHandleA(NULL),NULL);
        else {fs_refresh();ShowWindow(app.fs_window,SW_RESTORE);SetForegroundWindow(app.fs_window);}break;
    case ID_RUN:
        if (app.m.loaded && !app.m.cpu.halted) { app.running = 1; app.state = "Running"; }
        break;
    case ID_PAUSE: app.running = 0; release_keys(); app.state = "Paused"; (void)save_storage(); break;
    case ID_STEP: {
        int provision;
        app.running = 0;
        provision = nvram_store_step(&app.storage, &app.m);
        if (!provision) { app.state = "Paused - provisioning failed"; app.m.cpu.halted = 1; }
        else if (provision == 2) {
            int initialize_defaults = app.storage.provision && app.storage.stage == 3;
            if (load_model()) app.storage.defaults_boot = initialize_defaults;
        }
        else app.state = machine_step(&app.m, app.continuation) ? "Paused - stepped" : app.m.cpu.halted ? "CPU stopped - see diagnostics" : "Unknown hardware - stopped";
        (void)save_storage();
        break;
    }
    case ID_CONTINUE: app.continuation = !app.continuation; break;
    case ID_PATTERN: if (app.test_pixels) app.pattern = !app.pattern; InvalidateRect(app.window, &app.lcd, FALSE); break;
    case ID_ZOOM1: case ID_ZOOM2: case ID_ZOOM3: app.zoom = id - ID_ZOOM1 + 1; fit_window(); break;
    case ID_DIAGNOSTICS:
        if (!app.diagnostics) app.diagnostics = CreateWindowExA(0, "SGHemuDiagnostics", "SGHemu - diagnostics", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, 730, 400, app.window, NULL, GetModuleHandleA(NULL), NULL);
        else { ShowWindow(app.diagnostics, SW_RESTORE); SetForegroundWindow(app.diagnostics); }
        break;
    case ID_ABOUT:
        MessageBoxA(app.window,
            "SGHemu - Samsung SGH-C100 emulator workbench\n\n"
            "Native C17 / x86 / Win32 frontend\n\n"
            "Keys: digits, Shift+8 (*), Shift+3 (#), arrows, Enter (OK),\n"
            "F1/F2 (soft keys), F3 (call), F4 (end), Backspace (C).\n"
            "F5 Run, F6 Pause, F10 Step, Ctrl+R Reset.\n\n"
            "Open a firmware folder (Ctrl+O) to identify and boot a supported ROM pair.\n"
            "LCD pixels and keypad scanning are handled by the guest firmware.\n"
            "Experimental hardware audio: import M5_EmuHw.dll from the Emulation menu.\n"
            "MA-3 translation and instruction timing are not fully calibrated.", "About SGHemu", MB_OK | MB_ICONINFORMATION);
        break;
    case ID_EXIT: SendMessageA(app.window, WM_CLOSE, 0, 0); return;
    default:
        if (id >= ID_KEY_BASE && id < ID_KEY_BASE + SGH_MAX_KEYS) {
            /* Native automation's Invoke/BM_CLICK has no mouse-down message. */
            phone_key(id - ID_KEY_BASE, 1, 1); phone_key(id - ID_KEY_BASE, 0, 1);
        }
        break;
    }
    if (app.log) fflush(app.log);
    if(app.fs_window)fs_refresh();
    update_ui();
}
static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), emulation = CreatePopupMenu(), view = CreatePopupMenu(), battery=CreatePopupMenu(),cell=CreatePopupMenu(),gsm=CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ID_OPEN_FOLDER, "Open &firmware folder...\tCtrl+O");
    HMENU look=CreatePopupMenu();
    AppendMenuA(look,MF_STRING,ID_LCD_GHOSTING,"LCD ghosting");
    AppendMenuA(look,MF_STRING,ID_MA_WATER,"Water (not accurate)");
    AppendMenuA(look,MF_STRING,ID_MA_DETUNE,"C100 MA detune (-12.5 cents)");
    AppendMenuA(file, MF_STRING, ID_OPEN_MODEL, "Open &model config (advanced)...");
    AppendMenuA(file, MF_STRING, ID_NV_LOAD, "Import NVM snapshot and reset...");
    AppendMenuA(file, MF_STRING, ID_USERFS, "Phone filesystem manager...");
    AppendMenuA(file,MF_STRING,ID_LOGGING,"&Logging");
    AppendMenuA(file, MF_STRING, ID_RESET, "&Reset\tCtrl+R"); AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_EXIT, "E&xit");
    AppendMenuA(emulation, MF_STRING, ID_RUN, "&Run\tF5"); AppendMenuA(emulation, MF_STRING, ID_PAUSE, "&Pause\tF6");
    AppendMenuA(emulation, MF_STRING, ID_STEP, "&Step instruction\tF10"); AppendMenuA(emulation, MF_SEPARATOR, 0, NULL);
    AppendMenuA(emulation, MF_STRING, ID_CONTINUE, "Diagnostic: continue &unknown accesses");
    AppendMenuA(emulation, MF_STRING, ID_SIM, "Virtual &SIM inserted (reset)");
    AppendMenuA(emulation, MF_STRING, ID_HIGH_PRIORITY, "High process priority");
    AppendMenuA(emulation, MF_STRING, ID_JAVA_EXTENDED, "Extended Java buffers (next import/reset)");
    AppendMenuA(emulation, MF_STRING, ID_HOST_TIME, "Sync phone date/time to Windows");
    AppendMenuA(emulation, MF_STRING, ID_TEXT_INPUT, "Keyboard text input (abc/ABC)\tCtrl+T");
    AppendMenuA(battery,MF_STRING,ID_BATTERY_PERCENT,"Set percentage...");
    AppendMenuA(battery,MF_STRING,ID_BATTERY_CABLE,"Charging cable connected");
    AppendMenuA(emulation,MF_POPUP,(UINT_PTR)battery,"Battery");
    AppendMenuA(cell,MF_STRING,ID_CELL_ENABLED,"Cellular enabled");
    AppendMenuA(cell,MF_STRING,ID_CELL_SMS,"Send SMS to phone...");
    AppendMenuA(cell,MF_STRING,ID_CELL_CALL,"Call phone...");
    AppendMenuA(cell,MF_STRING,ID_CELL_NETWORK,"Network name...");
    AppendMenuA(gsm,MF_STRING,ID_GSM_OFF,"Off");
    AppendMenuA(gsm,MF_STRING,ID_GSM_INTERNET,"Internet");
    AppendMenuA(gsm,MF_STRING,ID_GSM_GATEWAY,"Internet + built-in WAP gateway");
    AppendMenuA(cell,MF_POPUP,(UINT_PTR)gsm,"GSM");AppendMenuA(emulation,MF_POPUP,(UINT_PTR)cell,"Cellular");
    AppendMenuA(emulation, MF_SEPARATOR, 0, NULL);
    AppendMenuA(emulation, MF_STRING, ID_AUDIO, "Experimental hardware &audio (reset)");
    AppendMenuA(emulation, MF_STRING, ID_AUDIO_IMPORT, "Import Yamaha hardware DLL...");
    AppendMenuA(view, MF_STRING, ID_ZOOM1, "LCD zoom &1x"); AppendMenuA(view, MF_STRING, ID_ZOOM2, "LCD zoom &2x"); AppendMenuA(view, MF_STRING, ID_ZOOM3, "LCD zoom &3x");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_PATTERN, "LCD &test pattern"); AppendMenuA(view, MF_STRING, ID_DIAGNOSTICS, "&Diagnostics...");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File"); AppendMenuA(bar, MF_POPUP, (UINT_PTR)emulation, "&Emulation");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View"); AppendMenuA(bar,MF_POPUP,(UINT_PTR)look,"L&&F"); return bar;
}
static LRESULT window_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam, int *handled)
{
    *handled = 1;
    if (message == WM_CREATE) {
        unsigned i;
        app.window = window;
        app.font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        app.status=CreateWindowExA(0,STATUSCLASSNAMEA,"",WS_CHILD|WS_VISIBLE,0,0,0,0,
            window,NULL,NULL,NULL);
        SendMessageA(app.status,WM_SETFONT,(WPARAM)app.font,TRUE);layout();
        for (i = 0; i < SGH_MAX_KEYS; ++i) {
            app.buttons[i] = CreateWindowA("BUTTON", key_names[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window, (HMENU)(UINT_PTR)(ID_KEY_BASE + i), NULL, NULL);
            SendMessageA(app.buttons[i], WM_SETFONT, (WPARAM)app.font, TRUE); SetWindowSubclass(app.buttons[i], button_proc, i, 0);
        }
        SetMenu(window, make_menu()); SetTimer(window, 1, 33, NULL);
        if(load_model()&&app.recent_firmware){app.running=1;app.state="Running - last firmware";update_ui();}
        return 0;
    }
    if (message == WM_SIZE) { layout(); return 0; }
    if (message == WM_PHONE_KEY) { phone_key((unsigned)wparam, (int)lparam, 1); return 0; }
    if (message == WM_COMMAND) { command(LOWORD(wparam)); return 0; }
    if (message == WM_ENTERMENULOOP) { app.menu_active = 1; release_keys(); return 0; }
    if (message == WM_EXITMENULOOP) { app.menu_active = 0; return 0; }
    if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) { release_keys(); update_ui(); }
    if (message == WM_TIMER) {
        status_refresh(1);fs_tick();cell_tick();update_led_icon();presentation_tick(1);
        if(app.host_time&&app.m.rtc.attached&&GetTickCount()-app.time_checkpoint>=1000){
            app.time_checkpoint=GetTickCount();(void)host_sync_rtc(&app.m.rtc);
        }
        /* GUI refresh only. Emulation runs on emu_main. */
        if (app.running) {
            if (GetTickCount() - app.nv_checkpoint >= 5000) {
                app.nv_checkpoint = GetTickCount(); (void)save_storage();
            }
            if (app.log) fflush(app.log);
            update_ui(); InvalidateRect(window, &app.lcd, FALSE);
        }
        return 0;
    }
    if (message == WM_EMU_EVENT) {
        if (wparam == EMU_PROVISION_FAILED) {
            MessageBoxA(app.window, app.m.error, "SGHemu - first boot failed", MB_OK | MB_ICONERROR);
        } else if (wparam == EMU_RELOAD) {
            int initialize_defaults = app.reload_defaults;
            if (load_model()) {
                app.storage.defaults_boot = initialize_defaults;
                app.running = 1; app.state = initialize_defaults ? "First boot - creating persistent defaults" : "Running - initialized NVM";
            }
        } else if (wparam == EMU_IMPORT_DONE) {
            int java=app.content.type==10,install=app.content.size!=0;
            if(app.import_current<app.import_count&&app.imports[app.import_current].status==1){
                app.imports[app.import_current].status=2;
                app.imports[app.import_current].accepted=app.m.cpu.instructions;
            }
            content_import_release(&app.content); /* The guest keeps only its own copy in guest RAM. */
            fs_refresh();
            SetWindowTextA(app.fs_status, java ? (install ? "Native Java installer running; compilation can take minutes.\r\nWait for Installed, then press the phone's Close softkey before opening Java games.\r\nLarge apps may need Emulation > Extended Java buffers, then reimport." : "Native Java menu opened on the phone screen.") : "Import accepted by the guest; the flash task stores it as the boot continues.");
        } else if (wparam == EMU_IMPORT_FAILED) {
            if(app.import_current<app.import_count&&app.imports[app.import_current].status==1){
                ImportItem *item=&app.imports[app.import_current];item->status=4;
                snprintf(item->error,sizeof(item->error),"%.170s",app.content.error);app.import_running=0;
            }
            MessageBoxA(app.window, app.content.error, "SGHemu - import failed", MB_OK | MB_ICONERROR);
            content_import_release(&app.content);
        }
        update_ui(); return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        HDC target_dc=dc,buffer_dc=NULL;HBITMAP buffer=NULL,previous=NULL;RECT client;
        BITMAPINFO bitmap = {0};
        const uint32_t *pixels = app.display_pixels;
        /* Layout invalidates without WM_ERASEBKGND. Clear the paint region so
           shrinking/moving the LCD cannot leave its old pixels behind. The DC
           clips child buttons and regions outside the current update area. */
        /* Compose in memory and present once. Erasing the live LCD before
           StretchDIBits made periodic background-colored flashes visible. */
        GetClientRect(window,&client);
        if(client.right>0&&client.bottom>0){buffer_dc=CreateCompatibleDC(dc);
            if(buffer_dc)buffer=CreateCompatibleBitmap(dc,client.right,client.bottom);
            if(buffer){previous=(HBITMAP)SelectObject(buffer_dc,buffer);target_dc=buffer_dc;}}
        FillRect(target_dc, &ps.rcPaint, GetSysColorBrush(COLOR_BTNFACE));
        if (pixels) {
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bitmap.bmiHeader.biWidth = (LONG)app.display_width;
            bitmap.bmiHeader.biHeight = -(LONG)app.display_height; bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(target_dc, COLORONCOLOR);
            StretchDIBits(target_dc, app.lcd.left, app.lcd.top, app.lcd.right - app.lcd.left, app.lcd.bottom - app.lcd.top,
                0, 0, (int)app.display_width, (int)app.display_height, pixels, &bitmap, DIB_RGB_COLORS, SRCCOPY);
        } else FillRect(target_dc, &app.lcd, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if(buffer){BitBlt(dc,ps.rcPaint.left,ps.rcPaint.top,ps.rcPaint.right-ps.rcPaint.left,ps.rcPaint.bottom-ps.rcPaint.top,
            buffer_dc,ps.rcPaint.left,ps.rcPaint.top,SRCCOPY);SelectObject(buffer_dc,previous);DeleteObject(buffer);}
        if(buffer_dc)DeleteDC(buffer_dc);
        EndPaint(window, &ps); return 0;
    }
    if (message == WM_CLOSE) {
        app.running = 0;
        if (save_storage()) DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        app.running = 0; KillTimer(window, 1); if (app.diagnostics) DestroyWindow(app.diagnostics);
        if(app.fs_window)DestroyWindow(app.fs_window);
        if(app.battery_window)DestroyWindow(app.battery_window);
        nvram_store_release(&app.storage);
        content_import_release(&app.content);
        profile_close();machine_destroy(&app.m); free(app.test_pixels);free(app.ghost_pixels);free(app.ghost_target);free(app.ghost_published);free(app.display_pixels);
        {unsigned i;for(i=0;i<8;++i)if(app.led_icons[i])DestroyIcon(app.led_icons[i]);} if (app.log) fclose(app.log);
        if (app.font) DeleteObject(app.font); PostQuitMessage(0); return 0;
    }
    *handled = 0; return 0;
}
static int window_needs_lock(UINT message)
{
    switch (message) {
    case WM_CREATE: case WM_SIZE: case WM_PHONE_KEY: case WM_COMMAND: case WM_ENTERMENULOOP:
    case WM_EXITMENULOOP: case WM_ACTIVATE: case WM_TIMER: case WM_CLOSE:
    case WM_DESTROY: case WM_EMU_EVENT: return 1;
    default: return 0;
    }
}
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    LRESULT result; int handled;
    /* Everything else (mouse move, hit-testing, move/size loops) never touches
       app.m, so it must not wait for the emulation thread. */
    if(message==WM_ERASEBKGND)return 1;
    if(message==WM_PAINT)return window_message(window,message,wparam,lparam,&handled);
    if(message==WM_TIMER&&WaitForSingleObject(emu_mutex,2)!=WAIT_OBJECT_0){
        if(profile_enabled)InterlockedIncrement(&profile_skipped);status_refresh(0);presentation_tick(0);return 0;}
    if(message==WM_TIMER){result=window_message(window,message,wparam,lparam,&handled);emu_unlock();return result;}
    if (!window_needs_lock(message)) return DefWindowProcA(window, message, wparam, lparam);
    emu_lock(); result = window_message(window, message, wparam, lparam, &handled); emu_unlock();
    return handled ? result : DefWindowProcA(window, message, wparam, lparam);
}
/* Runs on emu_main with emu_mutex held. paced = hardware audio is attached and
   its output queue (backpressure inside ma5_hw generate) is the clock. */
static void emu_slice(LONGLONG frequency, int paced)
{
    unsigned i, limit = paced ? 20000u : 120000u;
    LARGE_INTEGER start, now;
    LONGLONG budget = frequency * 2 / 1000; /* Short slices keep input responsive. */
    QueryPerformanceCounter(&start);
    for (i = 0; i < limit; ++i) {
        unsigned batch=1,at=i;
        if(!(i%1000))ghost_capture();
        if(!(i%1000)&&(app.text_keys.count||app.text_keys.pressed))text_step();
        if(gui_profile.file&&!(i&1023))profile_sample(app.m.cpu.r[15]);
        int provision = nvram_store_step(&app.storage, &app.m);
        if (!provision) {
            app.running = 0; app.state = "Paused - first-boot provisioning failed";
            app.m.cpu.halted = 1; app.m.cpu.error = "Guest NVM provisioning failed";
            PostMessageA(app.window, WM_EMU_EVENT, EMU_PROVISION_FAILED, 0); return;
        }
        if (provision == 2) {
            app.reload_defaults = app.storage.provision && app.storage.stage == 3;
            app.running = 0; /* idle until the GUI thread has reloaded the model */
            PostMessageA(app.window, WM_EMU_EVENT, EMU_RELOAD, 0); return;
        }
        if(!app.m.cellular.stage&&(app.m.battery.stage||!(app.content.active&&!app.content.done&&!app.content.failed))&&
           !battery_step(&app.m.battery,&app.m.cpu)){
            app.running=0;app.state="Battery event failed";return;
        }
        if (!app.m.cellular.stage && !app.m.battery.stage && app.content.active && !app.content.done && !app.content.failed) {
            int imported = content_import_step(&app.content, &app.m);
            if (!imported) {
                app.running = 0;
                PostMessageA(app.window, WM_EMU_EVENT, EMU_IMPORT_FAILED, 0); return;
            }
            /* On acceptance keep running: the guest's flash task still has to store
               the queued record and its data before the next NVM save. */
            if (imported == 2) PostMessageA(app.window, WM_EMU_EVENT, EMU_IMPORT_DONE, 0);
        }
        /* Provisioning and imports borrow live CPU state at exact PCs. Keep
           their original single-step path; ordinary execution can batch up to
           the next frontend sample/input boundary without hiding an edge. */
        if(!app.storage.defaults_boot&&(!app.storage.provision||app.storage.stage==3)&&
           !(app.content.active&&!app.content.done&&!app.content.failed)&&
           (!app.m.battery.enabled||app.m.battery.adc)){
            unsigned boundary=1000-i%1000;batch=64;
            if(batch>boundary)batch=boundary;
            if(batch>limit-i)batch=limit-i;
            if(gui_profile.file){boundary=1024-(i&1023);if(batch>boundary)batch=boundary;}
        }
        if (machine_run(&app.m,batch,app.continuation)!=batch) {
            const char *reason=app.m.cpu.error?app.m.cpu.error:"unmapped access";FILE *files[2]={app.log,gui_profile.file};unsigned f;
            for(f=0;f<2;++f)if(files[f]){unsigned reg;
                fprintf(files[f],"# STOP pc=%08x last_pc=%08x opcode=%08x cpsr=%08x reason=%s\n",
                    app.m.cpu.r[15],app.m.cpu.last_pc,app.m.cpu.last_opcode,app.m.cpu.cpsr,reason);
                for(reg=0;reg<16;++reg)fprintf(files[f],"r%u=%08x%s",reg,app.m.cpu.r[reg],reg%4==3?"\n":" ");fflush(files[f]);}
            app.running = 0; app.state = app.m.cpu.halted ? "CPU stopped - see diagnostics" : "Unknown hardware - stopped"; return;
        }
        i+=batch-1;
        if (!(at % 1000)) {
            QueryPerformanceCounter(&now);
            if (now.QuadPart - start.QuadPart >= budget) break;
        }
    }
}
static unsigned __stdcall emu_main(void *argument)
{
    LARGE_INTEGER frequency;
    (void)argument;
    QueryPerformanceFrequency(&frequency);
    while (!emu_quit) {
        int ran = 0, paced = 0;
        /* Time-out so shutdown never waits on a lock held by a nested GUI call. */
        if (WaitForSingleObject(emu_mutex, 20) != WAIT_OBJECT_0) continue;
        if (app.running && app.m.loaded && !app.m.cpu.halted) {
            paced = app.m.ma3.hw != NULL || app.m.phone_io.output != NULL; ran = 1;
            if(gui_profile.file){LARGE_INTEGER before={0},after={0};uint64_t ticks;QueryPerformanceCounter(&before);
                emu_slice(frequency.QuadPart,paced);QueryPerformanceCounter(&after);ticks=(uint64_t)(after.QuadPart-before.QuadPart);
                ++gui_profile.slices;gui_profile.busy_ticks+=ticks;if(ticks>gui_profile.max_slice)gui_profile.max_slice=ticks;
            }else emu_slice(frequency.QuadPart,paced);
        }
        profile_emit(0);
        ReleaseMutex(emu_mutex);
        if (!ran) Sleep(10);
        else SwitchToThread(); /* No artificial 16 ms throttle without audio. */
    }
    return 0;
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    WNDCLASSEXA cls = {0};
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES|ICC_LISTVIEW_CLASSES};
    MSG message;
    HACCEL accelerator;
    ACCEL keys[] = {{FVIRTKEY | FCONTROL, 'O', ID_OPEN_FOLDER}, {FVIRTKEY | FCONTROL, 'R', ID_RESET},
        {FVIRTKEY | FCONTROL, 'T', ID_TEXT_INPUT},
        {FVIRTKEY, VK_F5, ID_RUN}, {FVIRTKEY, VK_F6, ID_PAUSE}, {FVIRTKEY, VK_F10, ID_STEP}};
    char executable[1024], *slash;
    int exit_code;
    (void)previous; (void)command_line;
    app.sim_override=-1;
    emu_mutex = CreateMutexA(NULL, FALSE, NULL);
    if (!emu_mutex) return 1;
    /* Both file dialogs and config paths use the project working directory. */
    if (GetModuleFileNameA(NULL, executable, sizeof(executable))) {
        slash = strrchr(executable, '\\'); if (slash) { *slash = 0; slash = strrchr(executable, '\\');
            if (slash && !_stricmp(slash + 1, "build")) *slash = 0;
            SetCurrentDirectoryA(executable);
        }
    }
    CreateDirectoryA("build", NULL);
    GetFullPathNameA("state/audio.ini",sizeof(app.audio_settings),app.audio_settings,NULL);
    GetFullPathNameA("state/emulator.ini",sizeof(app.emulator_settings),app.emulator_settings,NULL);
    app.ghosting=GetPrivateProfileIntA("look_and_feel","lcd_ghosting",0,app.emulator_settings)!=0;
    app.ma_detune=GetPrivateProfileIntA("look_and_feel","c100_ma_detune",0,app.emulator_settings)!=0;
    app.icon_led=~0u;
    app.host_time=GetPrivateProfileIntA("emulation","sync_time",1,app.emulator_settings)!=0;
    app.text_input=GetPrivateProfileIntA("emulation","keyboard_text_input",0,app.emulator_settings)!=0;
    app.cell_enabled=GetPrivateProfileIntA("cellular","enabled",1,app.emulator_settings)!=0;
    app.gsm_mode=GetPrivateProfileIntA("cellular","gsm_mode",0,app.emulator_settings);if(app.gsm_mode>2)app.gsm_mode=0;
    GetPrivateProfileStringA("cellular","network","Tele2",app.cell_network,sizeof(app.cell_network),app.emulator_settings);
    app.java_extended=GetPrivateProfileIntA("emulation","extended_java_buffers",0,app.emulator_settings)!=0;
    app.battery_percent=GetPrivateProfileIntA("battery","percent",100,app.emulator_settings);
    if(app.battery_percent>100)app.battery_percent=100;
    app.charging=GetPrivateProfileIntA("battery","charging",0,app.emulator_settings)!=0;
    app.high_priority=GetPrivateProfileIntA("emulation","high_priority",0,app.emulator_settings)!=0;
    if(!host_high_priority(app.high_priority))app.high_priority=GetPriorityClass(GetCurrentProcess())==HIGH_PRIORITY_CLASS;
    GetPrivateProfileStringA("audio","hardware_dll","",app.audio_dll,sizeof(app.audio_dll),app.audio_settings);
    app.audio_enabled=GetPrivateProfileIntA("audio","enabled",0,app.audio_settings)!=0;
    app.audio_ticks_hz=GetPrivateProfileIntA("audio","ticks_hz",2166667,app.audio_settings);
    if(app.audio_ticks_hz<48000||app.audio_ticks_hz>100000000)app.audio_ticks_hz=2166667;
    app.audio_block=GetPrivateProfileIntA("audio","block",96,app.audio_settings); /* frames per DLL step: 48, 96, 120, 160, 240 or 480 */
    app.logging=GetPrivateProfileIntA("diagnostics","logging",0,app.emulator_settings)!=0;
    InitCommonControlsEx(&common);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    strcpy(app.model, "models/c100/c100xeda2.cfg"); app.zoom = 2; app.state = "Paused"; app.last_key = SGH_MAX_KEYS;
    {
        char recent_model[1024],recent_rom[1024],recent_second[1024];
        GetPrivateProfileStringA("recent_firmware","model","",recent_model,sizeof(recent_model),app.emulator_settings);
        GetPrivateProfileStringA("recent_firmware","rom1","",recent_rom,sizeof(recent_rom),app.emulator_settings);
        GetPrivateProfileStringA("recent_firmware","rom2","",recent_second,sizeof(recent_second),app.emulator_settings);
        if(*recent_model&&GetFileAttributesA(recent_model)!=INVALID_FILE_ATTRIBUTES&&
           (!*recent_rom||GetFileAttributesA(recent_rom)!=INVALID_FILE_ATTRIBUTES)&&
           (!*recent_second||GetFileAttributesA(recent_second)!=INVALID_FILE_ATTRIBUTES)){
            strcpy(app.model,recent_model);strcpy(app.rom,recent_rom);strcpy(app.second_rom,recent_second);app.recent_firmware=1;
        }
    }
    release_keys();
    cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); cls.lpszClassName = "SGHemuWindow"; cls.lpfnWndProc = window_proc;
    if (!RegisterClassExA(&cls)) return 1;
    cls.lpszClassName = "SGHemuDiagnostics"; cls.lpfnWndProc = diagnostics_proc;
    if (!RegisterClassExA(&cls)) return 1;
    cls.lpszClassName="SGHemuFilesystem";cls.lpfnWndProc=filesystem_proc;
    if(!RegisterClassExA(&cls))return 1;
    cls.lpszClassName="SGHemuBattery";cls.lpfnWndProc=battery_proc;
    if(!RegisterClassExA(&cls))return 1;
    cls.lpszClassName="SGHemuCellular";cls.lpfnWndProc=cell_editor_proc;if(!RegisterClassExA(&cls))return 1;
    cls.lpszClassName="SGHemuCall";cls.lpfnWndProc=call_proc;if(!RegisterClassExA(&cls))return 1;
    cls.lpszClassName="SGHemuWindow";cls.lpfnWndProc=window_proc;
    if (!CreateWindowExA(0, "SGHemuWindow", "SGHemu - Samsung SGH-C100", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 800, NULL, NULL, instance, NULL)) return 1;
    accelerator = CreateAcceleratorTableA(keys, sizeof(keys) / sizeof(keys[0]));
    ShowWindow(app.window, show); UpdateWindow(app.window);
    {
        uintptr_t thread_handle = _beginthreadex(NULL, 0, emu_main, NULL, 0, NULL);
        if (!thread_handle) return 1;
        emu_thread = (HANDLE)thread_handle;
        SetThreadPriority(emu_thread, THREAD_PRIORITY_ABOVE_NORMAL);
    }
    while ((exit_code = GetMessageA(&message, NULL, 0, 0)) > 0) {
        if (TranslateAcceleratorA(app.window, accelerator, &message)) continue;
        if(!app.menu_active&&(message.hwnd==app.window||IsChild(app.window,message.hwnd))){
            if(message.message==WM_KEYUP&&message.wParam<256&&app.vk_keys[message.wParam]==-2){
                app.vk_keys[message.wParam]=-1;continue;
            }
            if(app.text_input&&!(GetKeyState(VK_CONTROL)&0x8000)&&!(GetKeyState(VK_MENU)&0x8000)){
                if(message.message==WM_CHAR){int accepted;
                    emu_lock();accepted=app.running&&app.m.loaded&&key_text_char(&app.text_keys,(unsigned)message.wParam);emu_unlock();
                    if(!accepted)MessageBeep(MB_OK);continue;
                }
                if(message.message==WM_KEYDOWN){unsigned vk=(unsigned)message.wParam;int key=keyboard_key(message.wParam),queued=0;
                    if((vk>='A'&&vk<='Z')||vk==VK_SPACE||vk==VK_OEM_PERIOD){TranslateMessage(&message);continue;}
                    if(vk==VK_ESCAPE){emu_lock();release_keys();emu_unlock();continue;}
                    /* A quick Enter/softkey/backspace must follow queued text,
                       not overtake it. Preserve normal held keypad input otherwise. */
                    emu_lock();
                    if(key>=0&&app.running&&app.m.loaded&&(vk==VK_BACK||app.text_keys.count||app.text_keys.pressed)){
                        queued=key_text_key(&app.text_keys,(unsigned)key)?1:-1;
                        if(vk<256)app.vk_keys[vk]=-2;
                    }
                    emu_unlock();if(queued){if(queued<0)MessageBeep(MB_OK);continue;}
                }
            }
        }
        if (!app.menu_active && (message.hwnd == app.window || IsChild(app.window, message.hwnd)) && message.wParam < 256 &&
            (message.message == WM_KEYDOWN || message.message == WM_KEYUP)) {
            unsigned vk = (unsigned)message.wParam;
            int key = message.message == WM_KEYUP ? app.vk_keys[vk] : keyboard_key(message.wParam);
            if (key >= 0 && (message.message == WM_KEYUP || !(GetKeyState(VK_CONTROL) & 0x8000))) {
                uint32_t held = 0;
                unsigned i;
                app.vk_keys[vk] = message.message == WM_KEYUP ? -1 : key;
                for (i = 0; i < 256; ++i) if (app.vk_keys[i] >= 0) held |= 1u << app.vk_keys[i];
                emu_lock(); app.keyboard_keys = held;
                phone_key((unsigned)key, !!(held & (1u << key)), 0); emu_unlock(); continue;
            }
        }
        /* Dialog navigation must not consume Alt+menu mnemonics. */
        if (message.message == WM_SYSCHAR || message.message == WM_SYSKEYDOWN || message.message == WM_SYSKEYUP ||
            !IsDialogMessageA(app.window, &message)) { TranslateMessage(&message); DispatchMessageA(&message); }
    }
    InterlockedExchange(&emu_quit, 1);
    if (emu_thread) { WaitForSingleObject(emu_thread, 5000); CloseHandle(emu_thread); emu_thread = NULL; }
    if (accelerator) DestroyAcceleratorTable(accelerator);
    CloseHandle(emu_mutex);
    CoUninitialize();
    return exit_code == -1 ? 1 : (int)message.wParam;
}
