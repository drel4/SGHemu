@echo off
setlocal
cd /d "%~dp0"
if not defined VSCMD_VER (
  call "E:\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
  if errorlevel 1 exit /b 1
)
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
  echo SGHemu requires an x86 developer environment.
  exit /b 1
)

set "SGHEMU_MAKE=mingw32-make.exe"
where /q "%SGHEMU_MAKE%" || set "SGHEMU_MAKE=make.exe"
where /q "%SGHEMU_MAKE%" || (
  echo Native Windows GNU Make is required. Put mingw32-make.exe or make.exe on PATH.
  exit /b 1
)

rem Keep the original build\ output paths and rebuild when switching configs.
set "SGHEMU_CONFIG=debug"
if /i "%~1"=="release" set "SGHEMU_CONFIG=release"
if /i not "%~2"=="gui-only" goto full_build
if not "%SGHEMU_CONFIG%"=="release" exit /b 1
"%SGHEMU_MAKE%" -B PLATFORM=windows CONFIG=release BUILD=build gui
exit /b %ERRORLEVEL%

:full_build
"%SGHEMU_MAKE%" -B PLATFORM=windows CONFIG=%SGHEMU_CONFIG% BUILD=build all tools tests check
exit /b %ERRORLEVEL%
