@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_VER (
  set "SGHEMU_VS=E:\Microsoft Visual Studio\18\BuildTools"
  call "E:\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
  if errorlevel 1 exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  echo SGHemu requires an x86 developer environment.
  exit /b 1
)
if not exist build mkdir build
set "CFLAGS=/nologo /std:c17 /W4 /WX /Od /Zi /RTC1 /MD /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Isrc /Fobuild\ /Fdbuild\sghemu.pdb"
if /i "%~1"=="release" set "CFLAGS=/nologo /std:c17 /W4 /WX /O2 /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0601 /Isrc /Fobuild\"
cl %CFLAGS% tests\adpcm_test.c /Febuild\adpcm_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01
if errorlevel 1 exit /b 1
build\adpcm_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\phone_io_test.c src\phone_io.c src\arm7.c src\bus.c src\file.c /Febuild\phone_io_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 winmm.lib
if errorlevel 1 exit /b 1
build\phone_io_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\rtc_test.c src\rtc.c src\bus.c src\file.c /Febuild\rtc_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01
if errorlevel 1 exit /b 1
build\rtc_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\jar_test.c src\jar.c src\jar_inflate.c src\file.c /Febuild\jar_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib
if errorlevel 1 exit /b 1
build\jar_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\jarinspect.c src\jar.c src\jar_inflate.c src\file.c /Febuild\jarinspect.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\fwinspect.c src\file.c /Febuild\fwinspect.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\ma5probe.c src\file.c /Febuild\ma5probe.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib version.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\ma5hwprobe.c src\ma5_hw.c src\file.c /Febuild\ma5hwprobe.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\ma5seqprobe.c src\ma5_hw.c src\file.c /Febuild\ma5seqprobe.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\ma3pcmprobe.c src\ma3.c src\ma5_hw.c src\bus.c src\file.c /Febuild\ma3pcmprobe.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\ma3_test.c src\ma3.c src\bus.c src\file.c /Febuild\ma3_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01
if errorlevel 1 exit /b 1
build\ma3_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\userfs_test.c src\userfs.c src\bus.c src\file.c /Febuild\userfs_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\userfs_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\content_test.c src\content.c src\jar.c src\jar_inflate.c src\arm7.c src\bus.c src\file.c /Febuild\content_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 windowscodecs.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
build\content_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% src\main.c src\userfs.c src\nvram.c src\host.c src\content.c src\jar.c src\jar_inflate.c src\firmware.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\sghemu.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib windowscodecs.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tools\phoneprobe.c src\nvram.c src\host.c src\content.c src\jar.c src\jar_inflate.c src\firmware.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\phoneprobe.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib windowscodecs.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
if not defined SGHEMU_GUI_OUT set "SGHEMU_GUI_OUT=build\sghemu-gui.exe"
cl %CFLAGS% src\gui.c src\userfs.c src\nvram.c src\host.c src\content.c src\jar.c src\jar_inflate.c src\firmware.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Fe%SGHEMU_GUI_OUT% /link /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /MANIFEST:EMBED /MANIFESTINPUT:src\gui.manifest user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib uuid.lib bcrypt.lib version.lib winmm.lib windowscodecs.lib ole32.lib uuid.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\cpu_test.c src\arm7.c src\bus.c src\file.c /Febuild\cpu_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\cpu_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\machine_test.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\machine_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\machine_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\c100_identity_test.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\c100_identity_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\peripheral_test.c src\lcd.c src\flash.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\peripheral_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\peripheral_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\firmware_test.c src\firmware.c /Febuild\firmware_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\firmware_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\c100_boot_test.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\c100_boot_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\nvram_test.c src\nvram.c src\firmware.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\nvram_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\nvram_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\keypad_test.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\keypad_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\keypad_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\c100_keypad_test.c src\nvram.c src\firmware.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\c100_keypad_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\irom_test.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\irom_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\irom_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\sim_test.c src\sim.c src\sim_card.c src\intc.c src\bus.c /Febuild\sim_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
if errorlevel 1 exit /b 1
build\sim_test.exe
if errorlevel 1 exit /b 1
cl %CFLAGS% tests\c100_sim_test.c src\nvram.c src\firmware.c src\machine.c src\phone_io.c src\rtc.c src\battery.c src\keypad.c src\sim.c src\sim_card.c src\sds_uart.c src\sds_dma.c src\ma3.c src\ma5_hw.c src\irom.c src\flash.c src\lcd.c src\intc.c src\pmic.c src\arm7.c src\bus.c src\file.c /Febuild\c100_sim_test.exe /link /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 bcrypt.lib version.lib winmm.lib
exit /b %ERRORLEVEL%
