# NanoMig-Webradio

**Internetradio für den Amiga-Nachbau mit NanoMig (Tang Nano 20K + Raspberry Pi Pico 2 W)**

Der Pico, der im NanoMig-Aufbau ohnehin als FPGA-Companion arbeitet, holt jetzt auch die Radiostreams,
dekodiert MP3 und schickt den Ton über SPI in den FPGA. Der Ton kommt zusammen mit dem Amiga-Ton über HDMI.
Bedient wird alles am Amiga mit einem kleinen Programm, entweder als Fenster auf der Workbench oder als eigenständiges
Vollbild-Radio von einer Boot-Diskette, **ganz ohne Workbench**. **Keine zusätzliche Hardware, keine zusätzlichen Drähte.**

Projektseite: <https://github.com/TinyParts/NanoMig-Webradio>

*English summary: see [README_EN.md](README_EN.md).*

## Was du bekommst

- 53 Sender aus Österreich, Deutschland und der Schweiz in einer Textdatei auf der SD-Karte (`Radiosender.txt`), frei änderbar
- Programm „Radio": Senderliste mit Scrollbalken und Mausrad, Titelanzeige (Laufschrift),
  Pegelanzeige, Lautstärke, Mute, Mono/Stereo, Anzahl der Sender
- **Zwei Darstellungen:** als Fenster auf der Workbench, oder als **eigenständiges Radio** (Diskette einlegen, Konfiguration wählen,
  Radio spielt, ohne Workbench) mit Vollbild-Oberfläche (640x256 mit 16 Farben, Länderkennzeichen, Pegel in Farbe)
- Läuft parallel zu Spielen und zur Workbench, auch bei SD-Karten-Zugriffen (getestet: mehrere Minuten ohne Aussetzer)
- Nur HTTP-Streams im MP3-Format (kein HTTPS)

## Voraussetzungen

- Ein funktionierender **NanoMig**-Aufbau mit **Tang Nano 20K** und **Raspberry Pi Pico 2 W** als FPGA-Companion
  (getestet mit NanoMig **v0.18-beta**, 1200-Variante mit AT-Modem, und Companion **v1.4.33**),
  Kickstart im Flash, WLAN in `config.ini` eingerichtet
- Eigene Workbench-Installation (nicht im Paket enthalten, siehe unten)
- `openFPGALoader` (Tang flashen), ein USB-Kabel für den Pico

## Installation

> Sichere vorher deine bisherigen Dateien (Core-Datei und `PICO2.uf2`). Flashen auf eigene Gefahr.

1. **Core flashen** (Tang per USB-C an den PC):
   ```
   openFPGALoader -b tangnano20k -f core/release/NanoMig_PicoRadio_v0.18.fs
   ```
   Das ersetzt nur den Core am Anfang des Flash. Die Kickstart-ROMs weiter hinten bleiben unberührt.
2. **Pico flashen:** Tang stromlos machen, **BOOTSEL** am Pico halten, Pico per USB an den PC, die Datei
   `pico-firmware/release/fpga_companion_PICO2_radio_v3.uf2` auf das Laufwerk `RPI-RP2` kopieren.
   (Nie Pico-USB und Tang-Strom gleichzeitig anschließen.)
3. **SD-Karte:**
   - `sd-card/Radiosender.txt` in das Hauptverzeichnis kopieren
4. Jetzt gibt es zwei Möglichkeiten:

   **Variante A: mit Workbench** (Fenster)
   - `amiga-radio/release/Amiga WebRadio.adf` in den Ordner `ADF` kopieren.
   - In der `.amiga.ini` deiner Workbench-Konfiguration die Zeile ergänzen:
     ```
     drive1=/sd/ADF/Amiga WebRadio.adf
     ```
   - Tang neu starten, die Konfiguration wählen und die Workbench starten. Auf der Arbeitsfläche liegt das Symbol
     **Radio** (zusätzlich zum Laufwerk „RADIO"). Doppelklick startet das Programm.

   **Variante B: ohne Workbench** (eigenständiges Radio, Vollbild)
   - Die Boot-Diskette gibt es nicht fertig, weil sie das `serial.device` aus AmigaOS enthalten muss (gehört Commodore/Hyperion
     und darf nicht weitergegeben werden). Du baust sie in einer Minute selbst, mit deiner eigenen Kopie der Datei
     (aus dem Ordner `DEVS:` deiner Workbench):
     ```
     pip install amitools
     python amiga-radio/boot-disk/build_boot_disk.py --serial-device pfad/zu/serial.device
     ```
     Ergebnis: `Amiga WebRadio Boot.adf`. Diese Datei in den Ordner `ADF` der SD-Karte kopieren.
   - Eine Konfiguration anlegen: eine vorhandene 1200-Konfiguration kopieren, zum Beispiel als `Amiga_WebRadio.amiga.ini`, und
     darin die Diskette eintragen:
     ```
     drive0=/sd/ADF/Amiga WebRadio Boot.adf
     ```
   - Diese Konfiguration wählen. Der Amiga startet von der Diskette, das Radio öffnet einen eigenen Bildschirm und spielt.
     (Wurde vorher eine andere Diskette in `drive0` eingehängt, zuerst eine andere Konfiguration wählen, dann diese.)

Der Core ist die 1200-Variante (AGA, 68020) mit AT-Modem und **zusätzlichem Ton-Zwischenspeicher**. Das AT-Modem
und die WLAN-Funktionen bleiben erhalten.

## Bedienung

| Bedienelement | Funktion |
|---|---|
| Sender anklicken / Return | Sender abspielen |
| Cursor hoch/runter, Mausrad, Scrollbalken | Liste durchblättern |
| Lauter, Leiser, Klick in den Lautstärkebalken, `+` und `-` | Lautstärke |
| Mute, Taste `M` | Ton aus/an |
| Stereo / Mono | Umschalten auf Mono (Knopf zeigt die gewählte Betriebsart) |
| Taste `R` | Senderliste neu aus `Radiosender.txt` laden |
| Ende, `Q`, `Esc` | Radio ausschalten und beenden |

In der Vollbild-Oberfläche (ohne Workbench) steht oben die Anzahl der Sender, darunter der aktuelle Sender mit Länderkennzeichen und der
Titel, dann Pegel und Lautstärke, die Knöpfe und die Senderliste, unten der WLAN-Status. Ist Mute oder Mono aktiv, ist der Knopf rot.
Im Fenster auf der Workbench ist die Anordnung klassisch (Sender und Titel oben, Liste links, Knöpfe rechts).

### Senderliste

`Radiosender.txt` im SD-Hauptverzeichnis. Eine Zeile pro Sender:

```
AT - Hitradio Ö3 | http://orf-live.ors-shoutcast.at/oe3-q2a
```

- Leere Zeilen und Zeilen mit `#` am Anfang werden ignoriert, Datei als **UTF-8** speichern
- Nur `http://`-Adressen, MP3-Streams, bis zu **64** Sender, Namen bis 39 Zeichen (32 sichtbar)
- Fehlt die Datei, nimmt der Pico eine eingebaute Liste
- Die Adressen sind Beispiele und können sich ändern. Sender, die nur HTTPS anbieten, funktionieren nicht.

## Wie es funktioniert (Kurzfassung)

```
Sender --WLAN--> Pico: HTTP-Abruf -> Ringpuffer -> MP3 (minimp3) -> 48 kHz --SPI--> FPGA: Zwischenspeicher
                                                                             -> Mischung mit dem Amiga-Ton -> HDMI
Amiga-Programm <--serial.device--> AT-Modem im FPGA <--SPI--> Pico (ATR-Befehle: Sender, Lautstärke, Titel, Pegel)
```

Der FPGA spielt den Zwischenspeicher mit festen 48 kHz. Dadurch bestimmt er den Takt, und es gibt keine Drift zwischen
Sender und Ausgabe. Einzelheiten stehen in [docs/TECHNIK-Entwicklernotizen.md](docs/TECHNIK-Entwicklernotizen.md).

## Selbst bauen

Versionen, mit denen gebaut wurde: NanoMig **v0.18-beta** (Tag), FPGA-Companion **v1.4.33** (Tag), Gowin EDA 1.9.11.03
Education, Pico SDK 2.3.1, TinyUSB `master` und Pico-PIO-USB `main` (Stand November 2026), Arm GNU Toolchain 14.3.rel1,
CMake 4.5.0, Ninja, VBCC 0.9g.

### Core (`core/`)
1. NanoMig auf dem Tag `v0.18-beta` klonen (<https://github.com/MiSTle-Dev/NanoMig>).
2. Die Dateien aus `core/src/` an dieselben Stellen im Ordner `src/` von NanoMig kopieren
   (`misc/audio_fifo.v`, `misc/sysctrl.v`, `scripts/build_pico.tcl`, `tang/nano20k/top_pico.sv`).
   `sysctrl.v.patch` zeigt die Änderung an `sysctrl.v` gegenüber dem Original.
3. Im Ordner `src` ausführen: `gw_sh scripts/build_pico.tcl` (Gowin EDA). Ergebnis: `impl/pnr/nanomig_pico.fs`.

Der Chip ist danach zu etwa 99 % voll (Logik), das Timing wird eingehalten.

### Pico-Firmware (`pico-firmware/`)
1. FPGA-Companion auf dem Tag `v1.4.33` klonen (`--recursive`): <https://github.com/MiSTle-Dev/FPGA-Companion>.
2. `webradio-v1.4.33.patch` anwenden (`git apply`) und den Inhalt von `neue-dateien/src/` nach `src/` kopieren.
3. Pico SDK ≥ 2.2 installieren. Die Firmware braucht ein neueres TinyUSB als das im SDK 2.3.1 mitgelieferte
   (`tuh_descriptor_get_device_local`): TinyUSB auf `master` und Pico-PIO-USB auf `main` aktualisieren.
4. Bauen: `cmake -G Ninja -DBOARD=PICO2 ..` und `cmake --build .` im Ordner `src/rp2040/build`.
   `build_companion.bat` zeigt ein Beispiel für Windows (Pfad ohne Leerzeichen über ein virtuelles Laufwerk).

Der Patch fügt außerdem einen **Diagnose-Port** (TCP 7777, nur lesend) hinzu, der den Zustand des Radios als Textzeile
anzeigt. Du kannst ihn in `radio.c` entfernen (`radio_status_task`), wenn du ihn nicht möchtest.

### Amiga-Programm (`amiga-radio/`)
VBCC mit Amiga-NDK: `vc +aos68k -O2 -o Radio radio.c -lamiga` (`-DBTN3D=1` für Knöpfe mit Farbverlauf und runden Ecken).
Zum Zusammenbau der Workbench-Diskette (OFS, Volume „RADIO") mit `Radio`, `Radio.info` und `.backdrop` eignet sich `xdftool`
aus amitools. `.backdrop` legt das Symbol auf die Arbeitsfläche. Die Boot-Diskette baut `boot-disk/build_boot_disk.py`
(siehe Installation, Variante B).

## Nicht enthalten

Dieses Paket enthält **keine** Kickstart-ROMs, keine Workbench- oder AmigaVision-Images, kein `serial.device` und keine Programme
Dritter. Dafür brauchst du eigene, rechtmäßig erworbene Dateien.

## Lizenz

Dieses Projekt besteht aus Teilen mit verschiedenen Lizenzen:

| Teil | Lizenz |
|---|---|
| `core/` (Änderungen an NanoMig/Minimig) | **GPL-3.0** (siehe `core/LICENSE`) |
| `pico-firmware/` (Änderungen am FPGA-Companion, `radio.c` usw.) | **Apache-2.0** (siehe `pico-firmware/LICENSE`) |
| `pico-firmware/neue-dateien/src/minimp3/` | CC0 (gemeinfrei) |
| `amiga-radio/` (Radio-Programm) | **Apache-2.0** (siehe `amiga-radio/LICENSE`) |

Alle Hinweise zu den verwendeten Fremdprojekten, auch zu den Bedingungen für die Weitergabe und Nutzung, stehen in
[NOTICE.md](NOTICE.md). Urheber der Erweiterungen in diesem Projekt: TinyParts.

## Danksagung

- **NanoMig**, **FPGA-Companion** und die MiSTle-Gruppe (<https://github.com/MiSTle-Dev>), insbesondere Till Harbaum
- **Minimig** (Dennis van Weeren und viele andere), **fx68k** (Jorge Cwik), **TG68K** (Tobias Gubener)
- **minimp3** (lieff), **FreeRTOS**, **lwIP**, **TinyUSB**, **Pico SDK**
- Das Projekt wurde mit Unterstützung von Claude (Anthropic) entwickelt.

## Haftungsausschluss

Die Software wird ohne jede Gewährleistung bereitgestellt. Flashen von Firmware und Cores erfolgt auf eigene Gefahr.
Die Sender gehören ihren jeweiligen Anbietern. Das Programm ruft öffentlich zugängliche Streams ab wie ein Webbrowser.
