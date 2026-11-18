# NanoMig-Webradio

**Internet radio for the NanoMig Amiga clone (Tang Nano 20K + Raspberry Pi Pico 2 W)**

The Pico that already works as FPGA companion in a NanoMig setup now also fetches radio streams, decodes MP3 and
pushes the samples into the FPGA over SPI. The audio is mixed into the HDMI output together with the Amiga sound.
A small program controls everything, either as a window on Workbench or as a standalone full-screen radio that boots from
a floppy image **without Workbench**. **No extra hardware, no extra wires.**

Project page: <https://github.com/TinyParts/NanoMig-Webradio>

*Deutsche Fassung: [README.md](README.md)*

## Features

- 53 German-language stations (AT, DE, CH) in a text file on the SD card (`Radiosender.txt`), freely editable
- Program "Radio": station list with scroll bar and mouse wheel, stream title (scrolling), level meter,
  volume, mute, mono/stereo
- **Two modes:** window on Workbench, or **standalone radio** (insert the disk, select the configuration, it plays, no Workbench)
  with a full-screen UI (640x256, 16 colors, country badges, colored level meter)
- Keeps playing while Workbench or games access the SD card
- HTTP MP3 streams only (no HTTPS)

## Requirements

A working NanoMig setup with Tang Nano 20K and Pico 2 W as FPGA companion (tested with NanoMig v0.18-beta, 1200 variant
with AT modem, and companion v1.4.33), your own Workbench installation (not included), `openFPGALoader`.

## Installation

1. Flash the core: `openFPGALoader -b tangnano20k -f core/release/NanoMig_PicoRadio_v0.18.fs`
   (replaces the core at the start of the flash; the Kickstart ROMs further back stay untouched).
2. Flash the Pico: power the Tang off, hold BOOTSEL, connect the Pico to the PC, copy
   `pico-firmware/release/fpga_companion_PICO2_radio_v3.uf2` to the `RPI-RP2` drive.
   Never connect the Pico USB and Tang power at the same time.
3. SD card: copy `sd-card/Radiosender.txt` to the root.
4. Choose one of two ways:

   **A: with Workbench (window).** Copy `amiga-radio/release/Amiga WebRadio.adf` to `ADF` and add
   `drive1=/sd/ADF/Amiga WebRadio.adf` to your `.amiga.ini` configuration. Boot Workbench, the **Radio** icon appears on the desktop.

   **B: without Workbench (standalone, full screen).** The boot disk is not included, because it must contain AmigaOS's
   `serial.device` (Commodore/Hyperion, not redistributable). Build it yourself with your own copy of the file (from `DEVS:` of your Workbench):
   ```
   pip install amitools
   python amiga-radio/boot-disk/build_boot_disk.py --serial-device path/to/serial.device
   ```
   Copy the resulting `Amiga WebRadio Boot.adf` to `ADF`, copy one of your 1200 configurations (e.g. to `Amiga_WebRadio.amiga.ini`)
   and set `drive0=/sd/ADF/Amiga WebRadio Boot.adf`. Select that configuration: the Amiga boots from the disk and the radio plays.
   (If another disk was mounted in `drive0` before, select another configuration first, then this one.)

The station file format is `Name | http://address`, one per line, UTF-8, up to 64 stations. Press `R` in the program to reload it.

## Build from source

See the German [README.md](README.md) for the exact versions and steps: `core/` (Gowin EDA, `gw_sh scripts/build_pico.tcl`),
`pico-firmware/` (patch on companion v1.4.33 plus new files, Pico SDK 2.x), `amiga-radio/` (VBCC).

## License

`core/` GPL-3.0, `pico-firmware/` and `amiga-radio/` Apache-2.0, minimp3 CC0. Details and third-party notices: [NOTICE.md](NOTICE.md).
No Kickstart ROMs, Workbench images or `serial.device` are included.

## Disclaimer

No warranty. Flashing firmware and cores is at your own risk. Developed with the assistance of Claude (Anthropic).
