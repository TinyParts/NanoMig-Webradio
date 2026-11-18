"""Liest den Diagnose-Port des Pico-Radios (TCP 7777) und schreibt eine Zeile pro Sekunde in eine Datei.
Aufruf: python statuslog.py <Pico-IP> <Sekunden> <Datei>"""
import socket, sys, time

ip, secs, name = sys.argv[1], int(sys.argv[2]), sys.argv[3]
out = open(name, "w", encoding="latin-1")
s = socket.create_connection((ip, 7777), timeout=10)
s.settimeout(5)
t0 = time.time()
buf = b""
while time.time() - t0 < secs:
    try:
        d = s.recv(1024)
    except Exception as e:
        out.write("FEHLER %s\n" % e)
        break
    if not d:
        out.write("Verbindung beendet\n")
        break
    buf += d
    while b"\n" in buf:
        l, buf = buf.split(b"\n", 1)
        out.write("%5.0f %s\n" % (time.time() - t0, l.decode("latin-1")))
        out.flush()
out.close()
