# Hinweise zu Fremdprojekten und Lizenzen

Dieses Projekt ist eine Erweiterung bestehender freier Projekte. Hier steht, worauf es beruht und was man
beim Weitergeben beachten muss. Es ist keine Rechtsberatung.

## Projekte, die erweitert werden

| Projekt | Version | Lizenz | Hinweis |
|---|---|---|---|
| **NanoMig** (MiSTle-Dev, Till Harbaum) <https://github.com/MiSTle-Dev/NanoMig> | v0.18-beta | GPL-3.0 (Kopfzeilen der Quelldateien) | Der Core in diesem Paket ist ein abgeleitetes Werk. Wer ihn (als `.fs`-Bitstream) weitergibt, muss den vollständigen Quellcode unter der GPL mitgeben. Siehe `core/`. |
| **Minimig** (Dennis van Weeren u. a.) | im NanoMig enthalten | GPL-3.0 | Urheberhinweise in den Quelldateien nicht entfernen. |
| **fx68k** (Jorge Cwik) | im NanoMig enthalten | GPL-3.0 | siehe `core/LICENSE` (GPL-3.0-Volltext) |
| **TG68K** (Tobias Gubener) | im NanoMig enthalten | LGPL | |
| **FPGA-Companion** (MiSTle-Dev, Till Harbaum) <https://github.com/MiSTle-Dev/FPGA-Companion> | v1.4.33 | Apache-2.0 | Änderungen sind als Patch (`pico-firmware/webradio-v1.4.33.patch`) und neue Dateien enthalten. Die Apache-Lizenz verlangt, die Lizenz beizulegen, Urheberhinweise zu behalten und geänderte Dateien kenntlich zu machen. |

## Bestandteile der Pico-Firmware (nicht im Paket, werden beim Bauen eingebunden)

| Bestandteil | Lizenz | Hinweis |
|---|---|---|
| Pico SDK, lwIP, TinyUSB, Pico-PIO-USB, FreeRTOS, FatFS, u8g2 | BSD/MIT-artig | |
| **minimp3** (lieff) | CC0 | im Paket enthalten (`minimp3.h`) |
| **BTstack** (BlueKitchen) | eigene Lizenz: **frei nur für nicht-kommerzielle Nutzung** | Die Firmware enthält den Bluetooth-Stack. Für kostenlose Weitergabe ist das in Ordnung. Wer die Firmware kommerziell einsetzt oder verkauft, braucht eine Lizenz von BlueKitchen. |
| **CYW43-WLAN-Firmware** (im Pico SDK) | Lizenz von Raspberry Pi / Infineon: Nutzung nur mit Chips von Raspberry Pi | In der fertigen `.uf2`-Datei enthalten. Beim Pico 2 W ist das der Fall. |

## Eigene Anteile dieses Projekts

| Teil | Lizenz |
|---|---|
| `core/src/…` (`top_pico.sv`, `audio_fifo.v`, Änderung an `sysctrl.v`, `build_pico.tcl`) | GPL-3.0-or-later |
| `pico-firmware/neue-dateien/src/` (`radio.c`, `radio.h`, `audio.c`, `audio.h`) und der Patch | Apache-2.0 |
| `amiga-radio/` (`radio.c`, Symbol, Skripte) | Apache-2.0 |

## Werkzeuge zum Bauen (nicht im Paket)

Gowin EDA (Education), Arm GNU Toolchain, CMake, Ninja, VBCC/VASM/VLINK, Amiga-NDK, amitools. Sie unterliegen den
jeweiligen eigenen Lizenzen. Das fertige Amiga-Programm enthält Laufzeitcode von VBCC. Dessen Dokumentation
(`vbcc.pdf`) erlaubt die Weitergabe der Laufzeitbibliotheken. Bitte vor der Veröffentlichung selbst kurz nachlesen.

## Nicht enthalten, bitte nicht beilegen

Kickstart-ROMs, Workbench-Disketten und -Festplattenimages, AmigaVision/MegaAGS und weitere Amiga-Software sind
urheberrechtlich geschützt und gehören nicht in dieses Repository.

Dazu gehört auch die Datei `serial.device` (Teil von AmigaOS, Commodore/Hyperion). Die fertige Boot-Diskette (Start ohne
Workbench) enthält sie und darf deshalb nicht veröffentlicht werden. Das Paket liefert nur das Skript
`amiga-radio/boot-disk/build_boot_disk.py`, mit dem man die Diskette aus der eigenen Kopie baut.

## Streams und Sender

Die Senderliste enthält nur öffentlich zugängliche Internetadressen. Das Programm ruft sie ab wie ein Webbrowser.
Die Rechte an den Programmen und den Streams liegen bei den Sendern. Anbieter können Adressen ändern oder sperren.
