@echo off
rem Baut die FPGA-Companion-Firmware fuer den Pico 2 W (RP2350).
rem Der Projektpfad enthaelt ein Leerzeichen; der Build laeuft deshalb ueber ein virtuelles Laufwerk X:
rem (wird hier angelegt und am Ende wieder entfernt).
rem Aufruf: build_companion.bat [BOARD]    Standard: PICO2

set BOARD=%1
if "%BOARD%"=="" set BOARD=PICO2

subst X: /d >nul 2>&1
subst X: "%~dp0."
if errorlevel 1 (echo subst fehlgeschlagen & exit /b 1)

set PICO_SDK_PATH=X:\pico-sdk
set picotool_DIR=X:\picotool\picotool
set pioasm_DIR=X:\sdktools\pioasm
set PATH=X:\cmake\cmake-4.5.0-windows-x86_64\bin;X:\ninja;X:\armgcc\bin;X:\picotool\picotool;%PATH%

cd /d X:\companion\src\rp2040
if not exist build_%BOARD% mkdir build_%BOARD%
cd build_%BOARD%
cmake -G Ninja -DBOARD=%BOARD% -DPICO_SDK_PATH=%PICO_SDK_PATH% -Dpicotool_DIR=%picotool_DIR% -Dpioasm_DIR=%pioasm_DIR% ..
if errorlevel 1 goto fehler
cmake --build .
if errorlevel 1 goto fehler
echo.
echo Fertig: X:\companion\src\rp2040\build_%BOARD%\fpga_companion.uf2
goto ende

:fehler
echo FEHLER beim Bauen

:ende
cd /d "%~dp0"
subst X: /d >nul 2>&1
