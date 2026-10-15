@echo off
setlocal
cd /d "%~dp0.."
if /i "%~1"=="instrument" goto valid
if /i "%~1"=="train" goto valid
if /i "%~1"=="optimize" goto valid
echo Usage: tools\build-pgo.cmd instrument ^| train ^| optimize
exit /b 2
:valid
git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
  echo PGO helper requires a Git checkout so it can copy only tracked model descriptors.
  exit /b 1
)
call "E:\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if errorlevel 1 exit /b 1
set "SGHEMU_PGO_DATABASE=build\pgo\sghemu-gui.pgd"
set "SGHEMU_GUI_OUT=build\pgo\sghemu-gui.exe"
if not exist build\pgo mkdir build\pgo
if /i "%~1"=="train" goto train
if /i "%~1"=="instrument" if exist "build\pgo\sghemu-gui!*.pgc" del /q "build\pgo\sghemu-gui!*.pgc"
set "SGHEMU_PGO_PHASE=%~1"
if /i "%~1"=="optimize" if not exist "%SGHEMU_PGO_DATABASE%" (
  echo Instrument and train this source revision first. No profile database found.
  exit /b 1
)
call build.cmd release gui-only
if errorlevel 1 exit /b 1
rem Copy only tracked model descriptors, never ROMs or saved state.
for /f "delims=" %%F in ('git ls-files models') do (
  call :model "%%F"
  if errorlevel 1 exit /b 1
)
if /i "%~1"=="instrument" echo Run tools\build-pgo.cmd train, exercise the phone, close it, then run tools\build-pgo.cmd optimize.
exit /b 0
:model
set "model_path=%~1"
set "model_path=%model_path:/=\%"
for %%D in ("build\pgo\%model_path%") do if not exist "%%~dpD" mkdir "%%~dpD"
copy /y "%model_path%" "build\pgo\%model_path%" >nul
exit /b %ERRORLEVEL%
:train
if not exist "%SGHEMU_GUI_OUT%" exit /b 1
rem vcvars32 supplies the x86 PGO runtime on PATH; it is not redistributed.
"%SGHEMU_GUI_OUT%"
exit /b %ERRORLEVEL%
