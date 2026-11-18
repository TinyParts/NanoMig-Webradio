# Internetradio im Pico (Stand 17.11.2026) - ersetzt die ESP32-Loesung

Der vorhandene Pico 2 W (FPGA-Companion) holt den Stream, dekodiert MP3 und schickt den Ton per SPI in den FPGA.
Kein ESP32, keine zusaetzlichen Drahte. Bedienung am Amiga mit dem Programm "Radio" ueber das virtuelle Modem.

## Teile
- **FPGA-Core** `fpga/NanoMig_PicoRadio_v0.18.fs` (im Tang-Flash). Quellen `src/pico-audio-patch/` (top_pico.sv,
  audio_fifo.v, sysctrl.v mit SPI-Befehl 11 `ENABLE_PICO_AUDIO`, build_pico.tcl). AT-Modem bleibt an.
  Zwischenspeicher 2048 Stereo-Samples (43 ms), Wiedergabe mit festen 48 kHz, Mischung wie beim Amiga-Ton.
  Bauen: `gw_sh scripts/build_pico.tcl` in `src/NanoMig/src`. Rueckweg auf den ESP32-Core: `fpga/NanoMig_Radio_v0.18.fs`
  (`openFPGALoader -b tangnano20k -f ...`), Original: `fpga/NanoMig_1200_v0.18-beta_AT_Modem.bin`.
- **Pico-Firmware** `pico/fpga_companion_PICO2_radio_v1.uf2` (Companion v1.4.33 + radio.c). Original zurueck:
  `pico/fpga_companion_PICO2_v1.4.33.uf2`. Flashen: Tang stromlos, BOOTSEL halten, Pico-USB an den PC.
  Bauen: `tools/pico-dev/build_companion.bat PICO2` (Compiler/SDK in `tools/pico-dev`, Aenderungen als Diff und
  Dateien in `tools/pico-dev/patch/`: radio.c/h, audio.c/h, at_wifi.c/h, sysctrl.c/h, minimp3/).
- **Amiga-Programm** `amiga/radio/radio.c` (VBCC, `build.bat`), Diskette `ADF/Amiga WebRadio.adf`, Konfiguration
  `Workbench_3.1_Magic.amiga.ini` (`drive1=` legt die Diskette ein). Symbol auf der Arbeitsflaeche (Radio.info + .backdrop).

## Ablauf im Pico (radio.c)
Net-Task (HTTP, Weiterleitungen, ICY-Titel) -> Ringpuffer 32 KB -> Decoder-Task (Prebuffer 20 KB, minimp3, Resampler 48 kHz)
-> SPI-Befehl 11 in den FPGA-FIFO. Der FPGA-FIFO bestimmt den Takt. Nur HTTP (kein HTTPS), 31 Sender fest im Code
(`stations[]` in radio.c; Autostart aus).

## Modem-Befehle (Amiga -> Pico, mit CR abgeschlossen)
`ATE0` Echo aus, `ATRN<n>` Sender, `ATRX` Stopp, `ATRV<0-100>` Lautstaerke, `ATRL` Liste, `ATR?` Status,
`ATRA1/0` Meldungen an/aus. Antworten/Meldungen: `S<n>,<name>` `P<n>,<name>` `T<titel>` `M<l>,<r>` (Pegel 0..100, dB-Skala
-40..0 dBFS, 20 Hz) `W<text>` `E<text>`, `OK`.

## Stolpersteine (alle behoben)
- Modem-Eingangsqueue hatte 8 Plaetze -> 512 (verlorene Befehlsbytes, "ERROR").
- Der Core-FIFO Richtung Amiga fasst nur 15 Bytes (uart_port rx_fifo, DEPTH 4): Meldungen werden stueckweise gesendet
  (`at_wifi_puts_wait`), nicht "atomar".
- Beim Amiga-Zeichnen: Rahmen/Beschriftung nur einmal, danach Differenzen (kein Flackern).

## Diagnose
Statusport TCP 7777 auf der Pico-IP (eine Zeile pro Sekunde: Zustand, Ringpuffer, FIFO, Netzrate, Frames, AT-Zaehler,
Pegelmeldungen, Titel). `tools/pico-dev/statuslog.py <Sekunden> <Datei>` schreibt sie mit.

## Stand 17.11.2026 (abgenommen)
- Firmware `pico/fpga_companion_PICO2_radio_v3.uf2`, Core `fpga/NanoMig_PicoRadio_v0.18.fs`, Diskette `ADF/Amiga WebRadio.adf`
  (ueber `drive1=` in `Workbench_3.1_Magic.amiga.ini`; `amiga.ini` startet diese Konfiguration, hotkey F1).
- Senderliste: `Radiosender.txt` im SD-Hauptverzeichnis (`AT - Name | http://adresse`, 53 Sender, je Land alphabetisch),
  Master-Kopie `pico/Radiosender.txt`. Neu einlesen mit Taste R im Programm.
- Weitere Modem-Befehle: `ATRM1` Mono, `ATRM0` Stereo, `ATRR` Senderdatei neu lesen.
- Programm: Knoepfe Lauter/Leiser/Mute/Stereo<->Mono/Ende, Lautstaerkebalken, Pegelanzeige (dB), Scrollbalken + Mausrad,
  Titel mit Laufschrift, Anzahl der Sender oben rechts.
- Bauwerkzeuge: `tools/pico-dev` (ca. 1 GB, nur zum Neubauen noetig), `tools/amiga-dev`, Gowin EDA unter C:\Gowin.

## Stand 18.11.2026: Radio ohne Workbench
- Das Programm erkennt die Workbench (`FindTask("Workbench")`). Ohne sie oeffnet es einen eigenen Bildschirm und eine randlose
  Backdrop-Flaeche, Schrift Topaz 8 (ROM). Mit Workbench bleibt das klassische Fenster.
- Anzeigemodus: PAL + Hires = `0x00029000` (nicht `0x28000`: das ergibt `OpenScreen`-Fehler 6, unbekannter Modus, und das Programm
  oeffnet dann unbemerkt die Workbench). Der Core hat OCS/ECS-Grafik: in Hires 640x256 sind 16 Farben moeglich (4 Bitebenen),
  mehr nur in Lowres (320x256, 32 Farben). Interlace (640x512) flimmerte zu stark und ist abgeschaltet (`TRY_LACE`).
  Reihenfolge: 640x256/16 Farben, sonst 320x256/32 Farben (die Farbverlaeufe nutzen dann Pens 16..31).
- Layout in logischen 320x256-Koordinaten, waagrechter Faktor `hs` (1 oder 2), Texte in echter Zeichenbreite.
- `serial.device` ist bei Kickstart 3.1 nicht im ROM und muss auf der Boot-Diskette unter `Devs/` liegen (nicht weitergeben).
- Titeltexte: typografische UTF-8-Zeichen (Gedankenstrich, Apostroph, Anfuehrungszeichen, Auslassung) werden durch ASCII ersetzt.
