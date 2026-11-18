@echo off
rem Baut das Radio-Programm fuer den Amiga mit VBCC (vc, vasm, vlink) und dem Amiga-NDK.
rem Voraussetzung: vc ist im PATH und die Umgebungsvariable VBCC zeigt auf das Ziel "targets\m68k-amigaos"
rem (Windows-Paket: https://github.com/kusma/amiga-dev).
vc +aos68k -O2 -o Radio radio.c -lamiga
if errorlevel 1 (echo FEHLER & exit /b 1)
echo Fertig: Radio
