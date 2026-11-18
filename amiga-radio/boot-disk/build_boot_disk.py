#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 TinyParts
"""
Baut die Boot-Diskette "Amiga WebRadio Boot.adf" (Start ohne Workbench).

Die Diskette enthaelt das Radio-Programm, sein Symbol, eine Startup-Sequence und das serial.device.
Das serial.device ist ein Teil von AmigaOS (Commodore/Hyperion) und gehoert deshalb NICHT zu diesem Paket.
Nimm die Datei aus deiner eigenen, rechtmaessig erworbenen Workbench-Installation (Ordner DEVS:, Datei
"serial.device", zum Beispiel aus deinem Workbench-Image oder deiner Festplatte).

Voraussetzung: Python 3 und amitools ("pip install amitools"), das Programm "xdftool".

Aufruf:
    python build_boot_disk.py --serial-device pfad/zu/serial.device
        [--radio ../release/Radio] [--icon ../Radio.info] [--out "Amiga WebRadio Boot.adf"]
"""
import argparse
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument("--serial-device", required=True, help="Pfad zu DEVS:serial.device aus deiner eigenen Workbench")
ap.add_argument("--radio", default=os.path.join(here, "..", "release", "Radio"), help="Radio-Programm (Amiga-Programm)")
ap.add_argument("--icon", default=os.path.join(here, "..", "Radio.info"), help="Symbol des Programms")
ap.add_argument("--out", default="Amiga WebRadio Boot.adf", help="Name der Diskettendatei")
args = ap.parse_args()

for name, path in (("serial.device", args.serial_device), ("Radio", args.radio), ("Radio.info", args.icon),
                   ("Startup-Sequence", os.path.join(here, "Startup-Sequence"))):
    if not os.path.isfile(path):
        sys.exit("Datei fehlt (%s): %s" % (name, path))

tool = shutil.which("xdftool")
cmd = [tool] if tool else [sys.executable, "-m", "amitools.tools.xdftool"]
if os.path.exists(args.out):
    os.remove(args.out)

steps = [
    ["create"], ["format", "RADIO", "ofs"], ["boot", "install"],
    ["makedir", "S"], ["makedir", "Devs"],
    ["write", os.path.join(here, "Startup-Sequence"), "S/Startup-Sequence"],
    ["write", args.serial_device, "Devs/serial.device"],
    ["write", args.radio, "Radio"],
    ["write", args.icon, "Radio.info"],
]
argv = [args.out]
for i, st in enumerate(steps):
    if i:
        argv.append("+")
    argv.extend(st)
res = subprocess.run(cmd + argv)
if res.returncode != 0:
    sys.exit("xdftool ist fehlgeschlagen (ist amitools installiert?)")
print("Fertig:", args.out)
print("Diese Datei in den Ordner ADF der SD-Karte kopieren und in der .ini als drive0 eintragen.")
