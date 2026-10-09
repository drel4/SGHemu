#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "firmware.h"
#include "nvram.h"
#define ID_OPEN_MODEL 100
#define ID_OPEN_ROM 101
#define ID_RESET 102
#define ID_EXIT 103
#define ID_OPEN_SECOND 104
#define ID_OPEN_FOLDER 105
#define ID_NV_LOAD 106
#define ID_RUN 110
#define ID_PAUSE 111
#define ID_STEP 112
#define ID_CONTINUE 113
#define ID_SIM 114
#define ID_PATTERN 120
#define ID_ZOOM1 121
#define ID_ZOOM2 122
#define ID_ZOOM3 123
#define ID_DIAGNOSTICS 124
#define ID_ABOUT 130
#define ID_KEY_BASE 200
#define WM_PHONE_KEY (WM_APP + 1)
typedef struct {
    Machine m;
    NvramStore storage;
    DWORD nv_checkpoint;
    HWND window, buttons[SGH_MAX_KEYS], diagnostics, diagnostic_edit;
    HFONT font;
    FILE *log;
    char model[1024], rom[1024], second_rom[1024], nv_input[1024];
    RECT lcd;
    unsigned zoom;
    uint32_t mouse_keys, keyboard_keys, *test_pixels;
    int vk_keys[256], running, continuation, pattern, menu_active, sim_override;
    const char *state;
    unsigned last_key;
} App;
static App app;
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
        "\r\nSIM: %s; resets %llu, APDUs %llu, RX %llu, TX %llu, DMA bytes %llu\r\n",
        app.m.sim.attached ? (app.m.sim.present ? "synthetic card inserted" : "removed") : "not mapped",
        (unsigned long long)app.m.sim.resets,(unsigned long long)app.m.sim.card.commands,
        (unsigned long long)app.m.sim.received,(unsigned long long)app.m.sim.transmitted,(unsigned long long)app.m.sim.dma_bytes);
    if (length < sizeof(text)) snprintf(text + length, sizeof(text) - length,
        "\r\nSee build/gui-unmapped.log for every unknown access.\r\n"
        "LCD output comes only from guest command/data writes. Keypad scanning/debounce runs in firmware.\r\n"
        "Diagnostic continuation returns zero on unknown reads and drops writes.\r\n"
        "One instruction = one scheduler tick; not calibrated phone time.");
    SetWindowTextA(app.diagnostic_edit, text);
}
static void update_ui(void)
{
    HMENU menu = GetMenu(app.window);
    unsigned i;
    CheckMenuItem(menu, ID_CONTINUE, MF_BYCOMMAND | (app.continuation ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_PATTERN, MF_BYCOMMAND | (app.pattern ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, ID_SIM, MF_BYCOMMAND | (app.m.sim.present ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, ID_SIM, MF_BYCOMMAND | (app.m.sim.attached ? MF_ENABLED : MF_GRAYED));
    CheckMenuRadioItem(menu, ID_ZOOM1, ID_ZOOM3, ID_ZOOM1 + app.zoom - 1, MF_BYCOMMAND);
    EnableMenuItem(menu, ID_RUN, MF_BYCOMMAND | (app.m.loaded && !app.running && !app.m.cpu.halted ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_PAUSE, MF_BYCOMMAND | (app.running ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_STEP, MF_BYCOMMAND | (app.m.loaded && !app.running && !app.m.cpu.halted ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(menu, ID_NV_LOAD, MF_BYCOMMAND | (app.m.loaded && app.m.nvram_data ? MF_ENABLED : MF_GRAYED));
    for (i = 0; i < SGH_MAX_KEYS; ++i) EnableWindow(app.buttons[i], app.m.loaded);
    update_diagnostics();
}
static void release_keys(void)
{
    unsigned i;
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
    machine_key(&app.m, key, !!((app.mouse_keys | app.keyboard_keys) & bit));
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
static void layout(void)
{
    RECT client;
    int width, screen, grid_y, button_width, i;
    GetClientRect(app.window, &client); width = client.right;
    screen = (int)(app.m.loaded ? app.m.width : 128) * (int)app.zoom;
    app.lcd.left = (width - screen) / 2; app.lcd.top = 0;
    app.lcd.right = app.lcd.left + screen;
    app.lcd.bottom = (int)(app.m.loaded ? app.m.height : 128) * (int)app.zoom;
    grid_y = app.lcd.bottom + 4; button_width = (width - 8) / 3;
    for (i = 0; i < 21; ++i) MoveWindow(app.buttons[i], 2 + (i % 3) * (button_width + 2), grid_y + (i / 3) * 36, button_width, 34, TRUE);
    MoveWindow(app.buttons[21], 2, grid_y + 252, width - 4, 34, TRUE);
    InvalidateRect(app.window, NULL, FALSE);
}
static void fit_window(void)
{
    RECT rect, current, work;
    int left, top;
    int width = (int)(app.m.loaded ? app.m.width : 128) * (int)app.zoom;
    int height = (int)(app.m.loaded ? app.m.height : 128) * (int)app.zoom + 292;
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
    if (nvram_store_flush(&app.storage, &app.m)) return 1;
    app.running = 0; app.state = "Paused - NVM save failed";
    MessageBoxA(app.window, app.m.error, "SGHemu - automatic NVM save failed", MB_OK | MB_ICONERROR);
    update_ui(); return 0;
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
    nvram_store_release(&app.storage);
    machine_destroy(&app.m);
    if (app.log) { fflush(app.log); fclose(app.log); app.log = NULL; }
    app.log = fopen("build/gui-unmapped.log", "w");
    if (!app.log) { app.state = "Cannot open diagnostic log"; update_ui(); return 0; }
    if (!machine_load_pair(&app.m, app.model, *app.rom ? app.rom : NULL,
        *app.second_rom ? app.second_rom : NULL, app.log)) {
        app.state = "No firmware loaded";
        MessageBoxA(app.window, app.m.error, "SGHemu - model load failed", MB_OK | MB_ICONERROR);
    } else {
        app.state = "Paused - ready"; app.continuation = app.m.bringup;
        if (app.sim_override >= 0 && app.m.sim.attached) app.m.sim.present = app.sim_override;
        if (!nvram_store_open(&app.storage, &app.m, "state") ||
            (*app.nv_input && !nvram_store_import(&app.storage, &app.m, app.nv_input))) {
            app.state = "Paused - NVM storage rejected";
            app.m.cpu.halted = 1; app.m.cpu.error = "Persistent NVM error; see diagnostics";
            MessageBoxA(app.window, app.m.error, "SGHemu - NVM load failed", MB_OK | MB_ICONERROR);
        }
        *app.nv_input = 0;
        app.nv_checkpoint = GetTickCount();
    }
    free(app.test_pixels); app.test_pixels = NULL; app.pattern = 0;
    if (app.m.loaded) {
        app.test_pixels = calloc((size_t)app.m.width * app.m.height, sizeof(uint32_t));
        pattern.width = app.m.width; pattern.height = app.m.height; pattern.pixels = app.test_pixels;
        machine_pattern(&pattern, 1);
    }
    app.last_key = SGH_MAX_KEYS; fit_window(); update_ui();
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
        if (firmware == 2) strcpy(app.second_rom, path);
        else if (firmware) strcpy(app.rom, path);
        else { strcpy(app.model, path); *app.rom = 0; *app.second_rom = 0; }
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
static void command(unsigned id)
{
    switch (id) {
    case ID_OPEN_MODEL: choose_file(0); break;
    case ID_OPEN_ROM: choose_file(1); break;
    case ID_OPEN_SECOND: choose_file(2); break;
    case ID_OPEN_FOLDER: choose_folder(); break;
    case ID_RESET: load_model(); break;
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
    case ID_RUN:
        if (app.m.loaded && !app.m.cpu.halted) { app.running = 1; app.state = "Running"; }
        break;
    case ID_PAUSE: app.running = 0; app.state = "Paused"; (void)save_storage(); break;
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
            "Audio is currently silent in the emulator; MA-5 has a separate probe.", "About SGHemu", MB_OK | MB_ICONINFORMATION);
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
    update_ui();
}
static HMENU make_menu(void)
{
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), emulation = CreatePopupMenu(), view = CreatePopupMenu(), help = CreatePopupMenu();
    AppendMenuA(file, MF_STRING, ID_OPEN_FOLDER, "Open &firmware folder...\tCtrl+O");
    AppendMenuA(file, MF_STRING, ID_OPEN_MODEL, "Open &model config (advanced)...");
    AppendMenuA(file, MF_STRING, ID_NV_LOAD, "Import NVM snapshot and reset...");
    AppendMenuA(file, MF_STRING, ID_RESET, "&Reset\tCtrl+R"); AppendMenuA(file, MF_SEPARATOR, 0, NULL);
    AppendMenuA(file, MF_STRING, ID_EXIT, "E&xit");
    AppendMenuA(emulation, MF_STRING, ID_RUN, "&Run\tF5"); AppendMenuA(emulation, MF_STRING, ID_PAUSE, "&Pause\tF6");
    AppendMenuA(emulation, MF_STRING, ID_STEP, "&Step instruction\tF10"); AppendMenuA(emulation, MF_SEPARATOR, 0, NULL);
    AppendMenuA(emulation, MF_STRING, ID_CONTINUE, "Diagnostic: continue &unknown accesses");
    AppendMenuA(emulation, MF_STRING, ID_SIM, "Virtual &SIM inserted (reset)");
    AppendMenuA(view, MF_STRING, ID_ZOOM1, "LCD zoom &1x"); AppendMenuA(view, MF_STRING, ID_ZOOM2, "LCD zoom &2x"); AppendMenuA(view, MF_STRING, ID_ZOOM3, "LCD zoom &3x");
    AppendMenuA(view, MF_SEPARATOR, 0, NULL); AppendMenuA(view, MF_STRING, ID_PATTERN, "LCD &test pattern"); AppendMenuA(view, MF_STRING, ID_DIAGNOSTICS, "&Diagnostics...");
    AppendMenuA(help, MF_STRING, ID_ABOUT, "&About / keyboard controls...");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)file, "&File"); AppendMenuA(bar, MF_POPUP, (UINT_PTR)emulation, "&Emulation");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)view, "&View"); AppendMenuA(bar, MF_POPUP, (UINT_PTR)help, "&Help"); return bar;
}
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CREATE) {
        unsigned i;
        app.window = window;
        app.font = CreateFontA(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
        for (i = 0; i < SGH_MAX_KEYS; ++i) {
            app.buttons[i] = CreateWindowA("BUTTON", key_names[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, window, (HMENU)(UINT_PTR)(ID_KEY_BASE + i), NULL, NULL);
            SendMessageA(app.buttons[i], WM_SETFONT, (WPARAM)app.font, TRUE); SetWindowSubclass(app.buttons[i], button_proc, i, 0);
        }
        SetMenu(window, make_menu()); SetTimer(window, 1, 16, NULL); load_model(); return 0;
    }
    if (message == WM_SIZE) { layout(); return 0; }
    if (message == WM_PHONE_KEY) { phone_key((unsigned)wparam, (int)lparam, 1); return 0; }
    if (message == WM_COMMAND) { command(LOWORD(wparam)); return 0; }
    if (message == WM_ENTERMENULOOP) { app.menu_active = 1; release_keys(); return 0; }
    if (message == WM_EXITMENULOOP) { app.menu_active = 0; return 0; }
    if (message == WM_ACTIVATE && LOWORD(wparam) == WA_INACTIVE) { release_keys(); update_ui(); }
    if (message == WM_TIMER) {
        if (app.running) {
            DWORD start = GetTickCount();
            unsigned i;
            for (i = 0; i < 40000; ++i) {
                int provision = nvram_store_step(&app.storage, &app.m);
                if (!provision) {
                    app.running = 0; app.state = "Paused - first-boot provisioning failed";
                    app.m.cpu.halted = 1; app.m.cpu.error = "Guest NVM provisioning failed";
                    MessageBoxA(app.window, app.m.error, "SGHemu - first boot failed", MB_OK | MB_ICONERROR); break;
                }
                if (provision == 2) {
                    int initialize_defaults = app.storage.provision && app.storage.stage == 3;
                    if (load_model()) {
                        app.storage.defaults_boot = initialize_defaults;
                        app.running = 1; app.state = initialize_defaults ? "First boot - creating persistent defaults" : "Running - initialized NVM";
                    }
                    break;
                }
                if (!machine_step(&app.m, app.continuation)) { app.running = 0; app.state = app.m.cpu.halted ? "CPU stopped - see diagnostics" : "Unknown hardware - stopped"; break; }
                if (!(i % 1000) && GetTickCount() - start >= 8) break;
            }
            if (GetTickCount() - app.nv_checkpoint >= 5000) {
                app.nv_checkpoint = GetTickCount(); (void)save_storage();
            }
            if (app.log) fflush(app.log); update_ui(); InvalidateRect(window, &app.lcd, FALSE);
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(window, &ps);
        BITMAPINFO bitmap = {0};
        const uint32_t *pixels = app.pattern ? app.test_pixels : app.m.pixels;
        if (pixels) {
            bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bitmap.bmiHeader.biWidth = (LONG)app.m.width;
            bitmap.bmiHeader.biHeight = -(LONG)app.m.height; bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(dc, COLORONCOLOR);
            StretchDIBits(dc, app.lcd.left, app.lcd.top, app.lcd.right - app.lcd.left, app.lcd.bottom - app.lcd.top,
                0, 0, (int)app.m.width, (int)app.m.height, pixels, &bitmap, DIB_RGB_COLORS, SRCCOPY);
        } else FillRect(dc, &app.lcd, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(window, &ps); return 0;
    }
    if (message == WM_CLOSE) {
        app.running = 0;
        if (save_storage()) DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        KillTimer(window, 1); if (app.diagnostics) DestroyWindow(app.diagnostics);
        nvram_store_release(&app.storage);
        machine_destroy(&app.m); free(app.test_pixels); if (app.log) fclose(app.log);
        if (app.font) DeleteObject(app.font); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    WNDCLASSEXA cls = {0};
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    MSG message;
    HACCEL accelerator;
    ACCEL keys[] = {{FVIRTKEY | FCONTROL, 'O', ID_OPEN_FOLDER}, {FVIRTKEY | FCONTROL, 'R', ID_RESET},
        {FVIRTKEY, VK_F5, ID_RUN}, {FVIRTKEY, VK_F6, ID_PAUSE}, {FVIRTKEY, VK_F10, ID_STEP}};
    char executable[1024], *slash;
    int exit_code;
    (void)previous; (void)command_line;
    app.sim_override=-1;
    /* Both file dialogs and config paths use the project working directory. */
    if (GetModuleFileNameA(NULL, executable, sizeof(executable))) {
        slash = strrchr(executable, '\\'); if (slash) { *slash = 0; slash = strrchr(executable, '\\');
            if (slash && !_stricmp(slash + 1, "build")) *slash = 0;
            SetCurrentDirectoryA(executable);
        }
    }
    CreateDirectoryA("build", NULL);
    InitCommonControlsEx(&common);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    strcpy(app.model, "models/c100/c100xeda2.cfg"); app.zoom = 2; app.state = "Paused"; app.last_key = SGH_MAX_KEYS;
    release_keys();
    cls.cbSize = sizeof(cls); cls.hInstance = instance; cls.hCursor = LoadCursor(NULL, IDC_ARROW);
    cls.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); cls.lpszClassName = "SGHemuWindow"; cls.lpfnWndProc = window_proc;
    if (!RegisterClassExA(&cls)) return 1;
    cls.lpszClassName = "SGHemuDiagnostics"; cls.lpfnWndProc = diagnostics_proc;
    if (!RegisterClassExA(&cls)) return 1;
    if (!CreateWindowExA(0, "SGHemuWindow", "SGHemu - Samsung SGH-C100", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 800, NULL, NULL, instance, NULL)) return 1;
    accelerator = CreateAcceleratorTableA(keys, sizeof(keys) / sizeof(keys[0]));
    ShowWindow(app.window, show); UpdateWindow(app.window);
    while ((exit_code = GetMessageA(&message, NULL, 0, 0)) > 0) {
        if (TranslateAcceleratorA(app.window, accelerator, &message)) continue;
        if (!app.menu_active && (message.hwnd == app.window || IsChild(app.window, message.hwnd)) && message.wParam < 256 &&
            (message.message == WM_KEYDOWN || message.message == WM_KEYUP)) {
            unsigned vk = (unsigned)message.wParam;
            int key = message.message == WM_KEYUP ? app.vk_keys[vk] : keyboard_key(message.wParam);
            if (key >= 0 && (message.message == WM_KEYUP || !(GetKeyState(VK_CONTROL) & 0x8000))) {
                uint32_t held = 0;
                unsigned i;
                app.vk_keys[vk] = message.message == WM_KEYUP ? -1 : key;
                for (i = 0; i < 256; ++i) if (app.vk_keys[i] >= 0) held |= 1u << app.vk_keys[i];
                app.keyboard_keys = held;
                phone_key((unsigned)key, !!(held & (1u << key)), 0); continue;
            }
        }
        /* Dialog navigation must not consume Alt+menu mnemonics. */
        if (message.message == WM_SYSCHAR || message.message == WM_SYSKEYDOWN || message.message == WM_SYSKEYUP ||
            !IsDialogMessageA(app.window, &message)) { TranslateMessage(&message); DispatchMessageA(&message); }
    }
    if (accelerator) DestroyAcceleratorTable(accelerator);
    CoUninitialize();
    return exit_code == -1 ? 1 : (int)message.wParam;
}
