/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 TinyParts
 *
 * Radio - Bedienprogramm fuer das Internetradio im FPGA-Companion (Pico) am NanoMig-Amiga.
 * Spricht ueber serial.device (19200 Baud, 8N1) mit dem virtuellen Modem des Pico (AT-Befehle):
 *   ATE0 Echo aus, ATRA1 Meldungen an, ATRL Liste, ATR? Status, ATRN<n> Sender, ATRV<0-100> Lautstaerke,
 *   ATRM1/0 Mono/Stereo, ATRX Stopp
 *   Antworten: S<n>,<name> P<n>,<name> T<titel> M<l>,<r> W<text> E<text>
 *
 * Zwei Darstellungen:
 *   - mit Workbench: normales Fenster auf der Workbench
 *   - ohne Workbench (Start von der Boot-Diskette): eigener Vollbild-Bildschirm 640x256 mit 16 Farben
 *     (ist das nicht moeglich: 320x256 mit 32 Farben), Knoepfe flach oder mit Verlauf (vc -DBTN3D=1)
 *
 * Bedienung: Maus (Sender anklicken, Knoepfe, Scrollbalken, Mausrad) oder Tasten
 *   Cursor hoch/runter = Auswahl, Return = abspielen, + / - = Lautstaerke, M = Mute,
 *   R = Liste neu laden, Q oder Esc = Ende
 *
 * Bauen mit VBCC (m68k-amigaos): siehe build.bat
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <devices/serial.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BAUD        19200
#define MAX_STATIONS 64
#define NAME_LEN    48
static int visRows = 10;
#define VISIBLE_ROWS visRows
#define ROW_H       12

/* Fenster-Layout (Pixel, innerhalb des Rahmens) */
#define INNER_W     400
#define INNER_H     192
#define LIST_X      4
#define LIST_Y      52
#define LIST_W      280
#define SB_X        288
#define SB_W        12
#define SB_ARROW    12
#define SCRDAT_ADDR 0xDFF1F0UL   /* Mausrad-Register des NanoMig-Cores (summierender Zaehler) */
#define BTN_X       304
#define BTN_W       92
#define BTN_H       16

struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase *GfxBase = NULL;

static struct Window *win = NULL;
static struct TextFont *topazFont = NULL;
static struct Screen *ownScreen = NULL;   /* nur im eigenstaendigen Betrieb (ohne Workbench) */
static BOOL modern = FALSE;
#define TRY_LACE 0                      /* 640x512 Interlace flimmert zu stark */
static int  vs = 1;
#ifndef BTN3D
#define BTN3D 0                         /* 1 = Knoepfe mit Farbverlauf und runden Ecken (vc -DBTN3D=1) */
#endif
#define TRY_HIRES 1                     /* 640x256 mit 16 Farben versuchen, sonst 320x256 mit 32 */
static int  hs = 1;                      /* waagrechter Faktor: 2 bei 640 Pixel Breite */                      /* senkrechter Faktor: 2 bei 640x512 (Interlace) */               /* modernes Vollbild-Layout (nur mit eigenem Bildschirm) */

static void m_drawAll(void);
static void m_drawNow(void);
static void m_drawTitleLine(void);
static void m_drawTop(void);
static void m_drawMeters(void);
static void m_drawVolume(void);
static void m_drawButton(int idx, int pressed);
static void m_drawButtons(void);
static void m_drawList(void);
static void m_dragTo(int my);
static BOOL m_handleClick(int mx, int my);
static void c_drawTitle(void);
static void c_drawCount(void);
static void c_drawHeader(void);
static void c_drawMeters(void);
static void c_drawMetersStatic(void);
static void c_drawList(void);
static void c_drawButton(int idx, int pressed);
static void c_drawButtons(void);
static void c_drawStatusStatic(void);
static void c_drawStatus(void);
static void c_drawAll(void);
static void c_dragTo(int my);
static BOOL c_handleClick(int mx, int my);
static struct RastPort *rp;
static int ox, oy;            /* Ursprung des Innenbereichs */
static int bl;                /* Baseline der Schrift */

/* ---------------- Zustand ---------------- */
static char stations[MAX_STATIONS][NAME_LEN];
static int  nStations = 0;
static int  current = -1;     /* spielt gerade */
static int  selected = 0;     /* Auswahl in der Liste */
static int  topRow = 0;       /* erste sichtbare Zeile */
static char curName[NAME_LEN] = "-";
static char title[100] = "";
static char status[60] = "";
static int  levelL = 0, levelR = 0;
static int  listDirty = 0;              /* Ticks, bis die Liste nach dem Einlesen neu gezeichnet wird */
static int  volume = 70;
static int  FW = 8, FH = 8;             /* Zeichenbreite und -hoehe in Pixeln, nach dem Oeffnen gemessen */
static int  titleChars = 49;            /* so viele Zeichen passen in die Titelzeile */
static int  listChars = 34;             /* ... in eine Listenzeile */
static int  volbarX = 76;               /* linker Rand des Lautstaerkebalkens */
static BOOL dragging = FALSE;
static BOOL muted = FALSE;
static BOOL mono = FALSE;
static int  dragOffset = 0;
static UBYTE lastWheel = 0;

/* Farben (Pens) aus der Workbench-Palette, damit es in jedem Farbschema lesbar ist */
static int P_TEXT = 1, P_BACK = 0, P_FILL = 3, P_FTEXT = 0;

/* ---------------- Serielle Schnittstelle ---------------- */
static struct MsgPort *rdPort = NULL, *wrPort = NULL;
static struct IOExtSer *rdReq = NULL, *wrReq = NULL;
static BOOL serOpen = FALSE;
static UBYTE rxByte;
static BOOL readPending = FALSE;

static int serErr = 0;        /* Fehlercode von serial.device (fuer die Anzeige), 0 = ok */

static void serialCleanup(void);

static BOOL serialStart(void)
{
    BYTE e;
    serErr = 0;
    rdPort = CreateMsgPort();
    wrPort = CreateMsgPort();
    if (!rdPort || !wrPort) { serErr = 100; return FALSE; }
    rdReq = (struct IOExtSer *)CreateIORequest(rdPort, sizeof(struct IOExtSer));
    wrReq = (struct IOExtSer *)CreateIORequest(wrPort, sizeof(struct IOExtSer));
    if (!rdReq || !wrReq) { serErr = 101; return FALSE; }

    rdReq->io_SerFlags = SERF_SHARED | SERF_XDISABLED;
    e = OpenDevice((STRPTR)SERIALNAME, 0, (struct IORequest *)rdReq, 0);
    if (e != 0) { serErr = 200 + (e < 0 ? -e : e); return FALSE; }
    serOpen = TRUE;

    /* alle Parameter ausdruecklich setzen: ohne Workbench hat niemand die Vorgaben gesetzt */
    rdReq->io_CtlChar = 0x11130000UL;       /* XON/XOFF, ohne Bedeutung da XDISABLED */
    rdReq->io_RBufLen = 8192;
    rdReq->io_ExtFlags = 0;
    rdReq->io_Baud = BAUD;
    rdReq->io_BrkTime = 250000;
    rdReq->io_ReadLen = 8;
    rdReq->io_WriteLen = 8;
    rdReq->io_StopBits = 1;
    rdReq->io_SerFlags = SERF_SHARED | SERF_XDISABLED;
    rdReq->IOSer.io_Command = SDCMD_SETPARAMS;
    e = DoIO((struct IORequest *)rdReq);
    if (e != 0) {
        serErr = rdReq->IOSer.io_Error;
        if (serErr == 0) serErr = e;
        serialCleanup();
        return FALSE;
    }

    /* Schreib-Request als Kopie des geoeffneten Requests */
    {
        struct MsgPort *keep = wrReq->IOSer.io_Message.mn_ReplyPort;
        *wrReq = *rdReq;
        wrReq->IOSer.io_Message.mn_ReplyPort = keep;
    }
    return TRUE;
}

static void serialRead(void)
{
    rdReq->IOSer.io_Command = CMD_READ;
    rdReq->IOSer.io_Length = 1;
    rdReq->IOSer.io_Data = &rxByte;
    SendIO((struct IORequest *)rdReq);
    readPending = TRUE;
}

static void serialSend(const char *s)
{
    if (!serOpen) return;
    wrReq->IOSer.io_Command = CMD_WRITE;
    wrReq->IOSer.io_Length = strlen(s);
    wrReq->IOSer.io_Data = (APTR)s;
    DoIO((struct IORequest *)wrReq);
}

static void serialCleanup(void)
{
    if (readPending) {
        AbortIO((struct IORequest *)rdReq);
        WaitIO((struct IORequest *)rdReq);
        readPending = FALSE;
    }
    if (serOpen) { CloseDevice((struct IORequest *)rdReq); serOpen = FALSE; }
    if (wrReq) { DeleteIORequest((struct IORequest *)wrReq); wrReq = NULL; }
    if (rdReq) { DeleteIORequest((struct IORequest *)rdReq); rdReq = NULL; }
    if (wrPort) { DeleteMsgPort(wrPort); wrPort = NULL; }
    if (rdPort) { DeleteMsgPort(rdPort); rdPort = NULL; }
}

static void serialStop(void)
{
    serialCleanup();
}

static void sendCmd(const char *prefix, int n)
{
    char b[24];
    sprintf(b, "ATR%s%d\r", prefix, n);
    serialSend(b);
}

/* ---------------- Text-Umwandlung ---------------- */
/* UTF-8 (Pico) -> Latin-1 (Amiga) */
static void fromUtf8(char *dst, const char *src, int max)
{
    int o = 0;
    const UBYTE *s = (const UBYTE *)src;
    while (*s && o < max - 1) {
        UBYTE c = *s++;
        if (c < 0x80) dst[o++] = c;
        else if (c == 0xC3 && *s) { dst[o++] = (char)(*s + 0x40); s++; }
        else if (c == 0xC2 && *s) { dst[o++] = (char)*s; s++; }
        else if (c == 0xE2 && s[0] == 0x80 && s[1]) {     /* typografische Zeichen ersetzen */
            UBYTE d = s[1];
            s += 2;
            if (d == 0x93 || d == 0x94 || d == 0x90 || d == 0x91 || d == 0x92) dst[o++] = '-';
            else if (d == 0x98 || d == 0x99 || d == 0x9A || d == 0xB2) dst[o++] = 0x27;
            else if (d == 0x9C || d == 0x9D || d == 0x9E) dst[o++] = 0x22;
            else if (d == 0xA6) { dst[o++] = '.'; if (o < max - 1) dst[o++] = '.'; if (o < max - 1) dst[o++] = '.'; }
            else if (d == 0xA2) dst[o++] = '*';
            else dst[o++] = '?';
        }
        else if (c >= 0xC0) {            /* andere Mehrbyte-Zeichen: ueberspringen */
            while ((*s & 0xC0) == 0x80) s++;
            dst[o++] = '?';
        }
    }
    dst[o] = 0;
}

/* ---------------- Zeichnen ---------------- */
/* im 16-Farben-Modus (640 Pixel) werden die Pens 16..31 der 32-Farben-Palette auf Grundfarben abgebildet */
static const UBYTE gmap[16] = { 2, 2, 11, 11, 6, 6, 2, 2, 3, 3, 15, 15, 3, 15, 10, 11 };
static int mapPen(int p) { return (hs == 2 && p > 15) ? gmap[p - 16] : p; }

static void fillRect(int x, int y, int w, int h, int pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(rp, mapPen(pen));
    RectFill(rp, ox + x * hs, oy + y * vs, ox + (x + w) * hs - 1, oy + (y + h) * vs - 1);
}

static void drawText(int x, int y, const char *s, int maxChars, int pen, int bg)
{
    int len = strlen(s);
    if (len > maxChars) len = maxChars;
    SetAPen(rp, mapPen(pen));
    SetBPen(rp, mapPen(bg));
    Move(rp, ox + x * hs, oy + y * vs + (FH * vs - FH) / 2 + bl);   /* Text in der Zeile mittig */
    Text(rp, (STRPTR)s, len);
}

static void box(int x, int y, int w, int h)
{
    SetAPen(rp, P_TEXT);
    Move(rp, ox + x, oy + y);
    Draw(rp, ox + x + w - 1, oy + y);
    Draw(rp, ox + x + w - 1, oy + y + h - 1);
    Draw(rp, ox + x, oy + y + h - 1);
    Draw(rp, ox + x, oy + y);
}

#define TITLE_CHARS titleChars
static int titleOff = 0;        /* Laufschrift: erstes sichtbares Zeichen */
static int titleWait = 0;       /* Pause in Ticks (1/10 s) am Anfang und Ende */

static void drawTitle(void)
{
    if (modern) { m_drawTitleLine(); return; }
    c_drawTitle();
}

static void c_drawTitle(void)
{
    char b[104];
    int len = strlen(title), i;
    if (titleOff > len) titleOff = 0;
    for (i = 0; i < TITLE_CHARS; i++)
        b[i] = (titleOff + i < len) ? title[titleOff + i] : ' ';
    b[TITLE_CHARS] = 0;
    drawText(4, 14, b, TITLE_CHARS, P_TEXT, P_BACK);
}

static char shownCount[20] = "";

/* Anzahl der Sender rechtsbuendig in der ersten Zeile; zeichnet nur bei Aenderung */
static void drawCount(void)
{
    if (modern) { m_drawTop(); return; }
    c_drawCount();
}

static void c_drawCount(void)
{
    char b[20];
    int w, oldw;
    sprintf(b, "%d Sender", nStations);
    if (strcmp(b, shownCount) == 0) return;
    w = TextLength(rp, (STRPTR)b, strlen(b));
    oldw = shownCount[0] ? TextLength(rp, (STRPTR)shownCount, strlen(shownCount)) : 0;
    if (oldw > w) w = oldw;
    fillRect(INNER_W - 4 - w - 2, 0, w + 6, 13, P_BACK);      /* alten Text samt Rand ersetzen */
    drawText(INNER_W - 4 - TextLength(rp, (STRPTR)b, strlen(b)), 2, b, strlen(b), P_TEXT, P_BACK);
    strcpy(shownCount, b);
}

static void drawHeader(void)
{
    if (modern) { m_drawNow(); m_drawTitleLine(); m_drawTop(); return; }
    c_drawHeader();
}

static void c_drawHeader(void)
{
    char b[80];
    int maxc;
    fillRect(0, 0, INNER_W, 26, P_BACK);
    maxc = (INNER_W - 8 - 10 * FW) / FW;                 /* Platz links der Anzahl "99 Sender" */
    sprintf(b, "Sender: %s", curName);
    if ((int)strlen(b) > maxc) strcpy(b, curName);           /* zu lang: ohne das Wort "Sender:" */
    drawText(4, 2, b, maxc, P_TEXT, P_BACK);
    drawTitle();
    shownCount[0] = 0;                      /* Kopfzeile wurde geloescht: Anzahl neu zeichnen */
    drawCount();
}

/* Laufschrift fuer lange Titel: alle 2 Ticks ein Zeichen, Pause am Anfang und Ende */
static void tickTitle(void)
{
    static int div = 0;
    int len = strlen(title);
    if (len <= TITLE_CHARS) return;
    if (titleWait > 0) { titleWait--; return; }
    if (++div < 2) return;
    div = 0;
    if (titleOff >= len - TITLE_CHARS) { titleOff = 0; titleWait = 20; }
    else { titleOff++; if (titleOff >= len - TITLE_CHARS) titleWait = 25; }
    drawTitle();
}

static int shownL = 0, shownR = 0;      /* Breite der Balken in Pixeln, wie aktuell gezeichnet */

/* Balken von 'shown' auf 'want' Pixel bringen: nur die Differenz zeichnen (kein Flackern) */
static void drawBar(int y, int *shown, int want)
{
    if (want > 200) want = 200;
    if (want < 0) want = 0;
    if (want > *shown) fillRect(20 + *shown, y, want - *shown, 6, P_FILL);
    else if (want < *shown) fillRect(20 + want, y, *shown - want, 6, P_BACK);
    *shown = want;
}

/* Pegel 0..100 -> 2 Pixel je Prozent */
static void drawMeters(void)
{
    if (modern) { m_drawMeters(); return; }
    c_drawMeters();
}

static void c_drawMeters(void)
{
    drawBar(28, &shownL, levelL * 2);
    drawBar(37, &shownR, levelR * 2);
}

/* Rahmen und Beschriftung: nur beim Aufbau des Fensters */
static void drawMetersStatic(void)
{
    if (modern) { ; return; }
    c_drawMetersStatic();
}

static void c_drawMetersStatic(void)
{
    fillRect(0, 26, INNER_W, 20, P_BACK);
    drawText(4, 27, "L", 1, P_TEXT, P_BACK);
    drawText(4, 36, "R", 1, P_TEXT, P_BACK);
    box(19, 27, 202, 8);
    box(19, 36, 202, 8);
    shownL = shownR = 0;
    drawMeters();
}

static void drawList(void);

static int maxTop(void)
{
    int m = nStations - VISIBLE_ROWS;
    return m > 0 ? m : 0;
}

/* Schieber-Geometrie: Spur zwischen den Pfeilen */
static void thumbGeom(int *ty, int *th)
{
    int trackY = LIST_Y + SB_ARROW;
    int trackH = VISIBLE_ROWS * ROW_H - 2 * SB_ARROW;
    int h = nStations > VISIBLE_ROWS ? trackH * VISIBLE_ROWS / nStations : trackH;
    int mt = maxTop();
    if (h < 8) h = 8;
    *th = h;
    *ty = trackY + (mt > 0 ? (trackH - h) * topRow / mt : 0);
}

static void drawArrow(int y, int up)
{
    int i, cx = SB_X + SB_W / 2;
    fillRect(SB_X, y, SB_W, SB_ARROW, P_BACK);
    box(SB_X, y, SB_W, SB_ARROW);
    for (i = 0; i < 4; i++) {
        int w = up ? 1 + i * 2 : 7 - i * 2;
        fillRect(cx - w / 2, y + 3 + i, w, 1, P_TEXT);
    }
}

static int sbArrows = 0;                /* Pfeile schon gezeichnet? (nur beim Fensteraufbau noetig) */

/* Schieber ohne leeres Zwischenbild: Hintergrund oberhalb/unterhalb und Raender neu, dann der Schieber */
static void drawScrollbar(void)
{
    int ty, th;
    int trackY = LIST_Y + SB_ARROW;
    int trackH = VISIBLE_ROWS * ROW_H - 2 * SB_ARROW;
    int trackEnd = trackY + trackH;
    if (!sbArrows) {
        drawArrow(LIST_Y, 1);
        drawArrow(LIST_Y + VISIBLE_ROWS * ROW_H - SB_ARROW, 0);
        fillRect(SB_X, trackY, SB_W, trackH, P_BACK);
        sbArrows = 1;
    }
    thumbGeom(&ty, &th);
    fillRect(SB_X + 1, trackY + 1, SB_W - 2, ty - trackY, P_BACK);                       /* oberhalb */
    fillRect(SB_X + 1, ty + th - 1, SB_W - 2, trackEnd - 1 - (ty + th - 1), P_BACK);      /* unterhalb */
    fillRect(SB_X + 1, ty + 1, 1, th - 2, P_BACK);                                         /* linker Rand */
    fillRect(SB_X + SB_W - 2, ty + 1, 1, th - 2, P_BACK);                                  /* rechter Rand */
    fillRect(SB_X + 2, ty + 1, SB_W - 4, th - 2, P_FILL);                                  /* Schieber */
    box(SB_X, trackY, SB_W, trackH);
}

static void setTop(int t)
{
    if (t > maxTop()) t = maxTop();
    if (t < 0) t = 0;
    if (t != topRow) { topRow = t; drawList(); }
}

/* Zeilen werden immer komplett (aufgefuellt) mit ihrem Hintergrund gezeichnet: kein leeres Zwischenbild,
   daher kein Flackern, auch wenn die Liste oft neu gezeichnet wird */
static void drawList(void)
{
    if (modern) { m_drawList(); return; }
    c_drawList();
}

static void c_drawList(void)
{
    int i, row;
    for (row = 0; row < VISIBLE_ROWS; row++) {
        char b[NAME_LEN + 8];
        int y = LIST_Y + row * ROW_H;
        int sel = 0, len, pen, bg;
        i = topRow + row;
        if (i < nStations) {
            sprintf(b, "%c %s", i == current ? '>' : ' ', stations[i]);
            sel = (i == selected);
        } else {
            b[0] = 0;
        }
        len = strlen(b);
        while (len < listChars) b[len++] = ' ';
        b[listChars] = 0;
        bg = sel ? P_FILL : P_BACK;
        pen = sel ? P_FTEXT : P_TEXT;
        fillRect(LIST_X, y, LIST_W, 2, bg);                                   /* oberer Rand der Zeile */
        fillRect(LIST_X, y + 2 + FH, LIST_W, ROW_H - 2 - FH, bg);             /* unterer Rand */
        fillRect(LIST_X, y + 2, 2, FH, bg);                                   /* links */
        fillRect(LIST_X + 2 + listChars * FW, y + 2, LIST_W - 2 - listChars * FW, FH, bg);   /* rechts */
        drawText(LIST_X + 2, y + 2, b, listChars, pen, bg);
    }
    box(LIST_X - 1, LIST_Y - 1, LIST_W + 2, VISIBLE_ROWS * ROW_H + 2);
    drawScrollbar();
    drawCount();
}

#define BTN_MUTE 2
#define BTN_MONO 3
static const char *btnLabel[6] = { "Lauter", "Leiser", "Mute", "Stereo", "", "Ende" };
static int pressedBtn = -1;

static void drawButton(int idx, int pressed)
{
    if (modern) { m_drawButton(idx, pressed); return; }
    c_drawButton(idx, pressed);
}

static void c_drawButton(int idx, int pressed)
{
    const char *label = btnLabel[idx];
    int y = LIST_Y + idx * (BTN_H + 4);
    int len;
    if (idx == BTN_MONO && mono) label = "Mono";      /* Beschriftung zeigt die gewaehlte Betriebsart */
    len = strlen(label);
    if (len == 0) return;
    if (idx == BTN_MUTE && muted) pressed = 1;
    if (idx == BTN_MONO && mono) pressed = 1;
    fillRect(BTN_X, y, BTN_W, BTN_H, pressed ? P_FILL : P_BACK);
    box(BTN_X, y, BTN_W, BTN_H);
    drawText(BTN_X + (BTN_W - TextLength(rp, (STRPTR)label, len)) / 2, y + 4, label, 11,
             pressed ? P_FTEXT : P_TEXT, pressed ? P_FILL : P_BACK);
}

static void drawButtons(void)
{
    if (modern) { m_drawButtons(); return; }
    c_drawButtons();
}

static void c_drawButtons(void)
{
    int i;
    for (i = 0; i < 6; i++) drawButton(i, i == pressedBtn);
}

#define VOLBAR_X   volbarX
#define VOLBAR_W   102      /* 100 Pixel Fuellung + Rahmen: 1 Pixel = 1 Prozent */
#define STATUS_Y   (LIST_Y + VISIBLE_ROWS * ROW_H + 4)

static int shownVol = 0;                /* Fuellung des Lautstaerkebalkens, wie aktuell gezeichnet */
static char shownStatus[64] = "";       /* Statustext, wie aktuell gezeichnet */
static char shownVolText[10] = "";

/* Rahmen und Gesamtzeile: nur beim Aufbau des Fensters */
static void drawStatusStatic(void)
{
    if (modern) { ; return; }
    c_drawStatusStatic();
}

static void c_drawStatusStatic(void)
{
    fillRect(0, STATUS_Y, INNER_W, 14, P_BACK);
    box(VOLBAR_X, STATUS_Y + 1, VOLBAR_W, 11);
    shownVol = 0;
    shownVolText[0] = 0;
    shownStatus[0] = 0;
}

/* Aktualisierung: zeichnet nur, was sich geaendert hat (kein Flackern) */
static void drawStatus(void)
{
    if (modern) { m_drawVolume(); m_drawTop(); return; }
    c_drawStatus();
}

static void c_drawStatus(void)
{
    char b[80];
    int fill = muted ? 0 : volume;

    if (muted) strcpy(b, "Vol Mute");
    else sprintf(b, "Vol %3d%%", volume);
    if (strcmp(b, shownVolText) != 0) {
        drawText(4, STATUS_Y + 2, b, 8, P_TEXT, P_BACK);     /* gleiche Breite, Hintergrund wird mitgezeichnet */
        strcpy(shownVolText, b);
    }

    if (fill > shownVol) fillRect(VOLBAR_X + 1 + shownVol, STATUS_Y + 2, fill - shownVol, 9, P_FILL);
    else if (fill < shownVol) fillRect(VOLBAR_X + 1 + fill, STATUS_Y + 2, shownVol - fill, 9, P_BACK);
    shownVol = fill;

    if (strcmp(status, shownStatus) != 0) {
        const char *t = status;
        int avail = (INNER_W - 4 - (VOLBAR_X + VOLBAR_W + 8)) / FW;       /* Zeichen rechts vom Balken */
        int n = strlen(t);
        if (n > avail && strncmp(t, "WLAN ", 5) == 0) { t += 5; n -= 5; }   /* zu lang: ohne das Wort WLAN */
        if (n > avail) n = avail;
        /* Bereich rechts vom Balken nur bei geaendertem Text loeschen */
        fillRect(VOLBAR_X + VOLBAR_W + 4, STATUS_Y, INNER_W - (VOLBAR_X + VOLBAR_W + 4), 14, P_BACK);
        drawText(INNER_W - 4 - TextLength(rp, (STRPTR)t, n), STATUS_Y + 2, t, n, P_TEXT, P_BACK);
        strncpy(shownStatus, status, sizeof(shownStatus) - 1);
        shownStatus[sizeof(shownStatus) - 1] = 0;
    }
}

static void drawAll(void)
{
    if (modern) { m_drawAll(); return; }
    c_drawAll();
}

static void c_drawAll(void)
{
    sbArrows = 0;
    fillRect(0, 0, INNER_W, INNER_H, P_BACK);
    drawHeader();
    drawMetersStatic();
    drawList();
    drawButtons();
    drawStatusStatic();
    drawStatus();
}

/* ---------------- Zeilen vom Pico ---------------- */
static void handleLine(char *l)
{
    char tmp[120];
    switch (l[0]) {
    case 'S': {
        int n = atoi(l + 1);
        char *c = strchr(l, ',');
        if (c && n >= 0 && n < MAX_STATIONS) {
            fromUtf8(stations[n], c + 1, NAME_LEN);
            if (n + 1 > nStations) nStations = n + 1;
            listDirty = 3;                                   /* erst zeichnen, wenn eine Weile nichts mehr kommt */
            if ((n & 7) == 7) drawList();                     /* und zwischendurch alle 8 Eintraege */
        }
        break; }
    case 'P': {
        int n = atoi(l + 1);
        char *c = strchr(l, ',');
        current = n;
        if (c) { fromUtf8(curName, c + 1, NAME_LEN); }
        if (n >= 0) { selected = n; if (selected < topRow) topRow = selected;
                      if (selected >= topRow + VISIBLE_ROWS) topRow = selected - VISIBLE_ROWS + 1; }
        title[0] = 0;
        titleOff = 0; titleWait = 20;
        drawHeader(); drawList();
        break; }
    case 'T':
        fromUtf8(tmp, l + 1, sizeof(tmp));
        strncpy(title, tmp, sizeof(title) - 1); title[sizeof(title) - 1] = 0;
        titleOff = 0; titleWait = 20;
        if (modern) drawTitle(); else drawHeader();
        break;
    case 'M': {
        int a = atoi(l + 1);
        char *c = strchr(l, ',');
        int b = c ? atoi(c + 1) : 0;
        if (a < levelL - 6) a = levelL - 6;     /* schneller, aber weicher Abfall */
        if (b < levelR - 6) b = levelR - 6;
        if (a != levelL || b != levelR) { levelL = a; levelR = b; drawMeters(); }
        break; }
    case 'W':
        sprintf(status, "WLAN %s", l + 1);
        drawStatus();
        break;
    case 'E':
        fromUtf8(tmp, l + 1, 40);
        sprintf(status, "Fehler: %s", tmp);
        drawStatus();
        break;
    default:
        break;
    }
}

static void feedByte(UBYTE c)
{
    static char line[200];
    static int len = 0;
    if (c == '\n' || c == '\r') {
        if (len > 0) { line[len] = 0; handleLine(line); len = 0; }
    } else if (c >= 0x20 && len < (int)sizeof(line) - 1) {
        line[len++] = c;
    } else if (c >= 0x80 && len < (int)sizeof(line) - 1) {
        line[len++] = c;
    }
}

/* ---------------- Bedienung ---------------- */
static void playSelected(void)
{
    if (selected >= 0 && selected < nStations) sendCmd("N", selected);
}

static void toggleMono(void)
{
    mono = !mono;
    sendCmd("M", mono ? 1 : 0);
    drawButton(BTN_MONO, 0);
}

static void toggleMute(void)
{
    muted = !muted;
    if (muted) serialSend("ATRV0\r");
    else sendCmd("V", volume);
    drawButton(BTN_MUTE, 0);
    drawStatus();
}

static void setVolume(int v)
{
    if (muted) { muted = FALSE; drawButton(BTN_MUTE, 0); }
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    volume = v;
    sendCmd("V", volume);
    drawStatus();
}

static void moveSelection(int d)
{
    int n = selected + d;
    if (n < 0) n = 0;
    if (n >= nStations) n = nStations - 1;
    if (n < 0) return;
    selected = n;
    if (selected < topRow) topRow = selected;
    if (selected >= topRow + VISIBLE_ROWS) topRow = selected - VISIBLE_ROWS + 1;
    drawList();
}

static void reload(void)
{
    nStations = 0;
    topRow = 0;
    drawList();
    serialSend("\rATE0\rATRA1\rATRL\rATR?\r");
}

/* ================= Modernes Layout 320x256, 32 Farben (nur eigenstaendiger Betrieb) ================= */
#define MC_BG     0
#define MC_PANEL  1
#define MC_TRACK  2
#define MC_ACC    3
#define MC_WHITE  4
#define MC_GRAY   5
#define MC_DIM    6
#define MC_GREEN  7
#define MC_YELLOW 8
#define MC_RED    9
#define MC_SEL    10
#define MC_TOP    11
#define MC_CH     12
#define MC_AT     13
#define MC_DE     14
#define M_ROW_H   12
#define M_ROWS_Y  150
#define M_LIST_H  (VISIBLE_ROWS * M_ROW_H)
#define M_LIST_X  4
#define M_SBX     306
#define M_SBW     7
#define M_SLX     slx
#define M_SLW     slw
#define KW        (6 / hs)
#define M_SLY     107
#define N_SEG     35

static int slx = 40, slw = 224;                 /* Lautstaerkeschieber */
static int chipW = 26, chipPad = 5, LTX = 40;   /* Laenderkennzeichen und Textanfang */
static int segW = 6, segStep = 8, nSeg = 35;    /* Pegelsegmente */

static int cw(int tw) { return (tw + hs - 1) / hs; }     /* Textbreite in Pixeln -> logische Breite */

static const UWORD modernPal[32] = {
    0x0112, 0x0123, 0x0245, 0x05CF, 0x0FFF, 0x08AC, 0x068A, 0x03C6,
    0x0FC3, 0x0E44, 0x0147, 0x0134, 0x0D21, 0x0FFF, 0x0FC0, 0x039D,
    0x0357, 0x0246, 0x0235, 0x0124,      /* 16-19 Kopfleiste (hell -> dunkel) */
    0x057A, 0x0468, 0x0357, 0x0246,      /* 20-23 Knopf normal */
    0x07DF, 0x05CF, 0x04BE, 0x039D,      /* 24-27 Knopf aktiv */
    0x07DF, 0x039D, 0x0357, 0x0134 };    /* 28-29 Schieber gefuellt, 30-31 Schieber leer */

/* senkrechter Farbverlauf aus n Streifen (Pens p0..p0+n-1) */
static void bands(int x, int y, int w, int h, int p0, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        int y0 = y + h * i / n, y1 = y + h * (i + 1) / n;
        fillRect(x, y0, w, y1 - y0, p0 + i);
    }
}

/* Text ohne Hintergrund (ueber Verlaeufen) */
static void drawTextT(int x, int y, const char *s, int len, int pen)
{
    SetDrMd(rp, JAM1);
    drawText(x, y, s, len, pen, pen);
    SetDrMd(rp, JAM2);
}

/* abgerundete Ecken: je zwei Pixel in der Umgebungsfarbe */
static void notch(int x, int y, int w, int h, int pen)
{
    fillRect(x, y, 2, 1, pen); fillRect(x + w - 2, y, 2, 1, pen);
    fillRect(x, y + h - 1, 2, 1, pen); fillRect(x + w - 2, y + h - 1, 2, 1, pen);
    fillRect(x, y + 1, 1, 1, pen); fillRect(x + w - 1, y + 1, 1, 1, pen);
    fillRect(x, y + h - 2, 1, 1, pen); fillRect(x + w - 1, y + h - 2, 1, 1, pen);
}

/* Text in doppelter Groesse aus den Zeichen der aktuellen Schrift */
static void bigText(int x, int y, const char *s, int len, int pen, int bg)
{
    struct TextFont *tf = rp->Font;
    UBYTE *data = (UBYTE *)tf->tf_CharData;
    ULONG *loc = (ULONG *)tf->tf_CharLoc;
    int mod = tf->tf_Modulo, ys = tf->tf_YSize;
    int i, r, k;
    fillRect(x, y, cw(len * 2 * FW), 2 * ys, bg);
    SetAPen(rp, mapPen(pen));
    for (i = 0; i < len; i++) {
        int c = (UBYTE)s[i], idx, off, w, px = x * hs + i * 2 * FW;
        if (c < tf->tf_LoChar || c > tf->tf_HiChar) idx = tf->tf_HiChar - tf->tf_LoChar + 1;
        else idx = c - tf->tf_LoChar;
        off = (int)(loc[idx] >> 16);
        w = (int)(loc[idx] & 0xFFFF);
        for (r = 0; r < ys; r++) {
            const UBYTE *row = data + r * mod;
            k = 0;
            while (k < w) {
                int bit = off + k;
                if (row[bit >> 3] & (0x80 >> (bit & 7))) {
                    int st = k;
                    while (k < w && (row[(off + k) >> 3] & (0x80 >> ((off + k) & 7)))) k++;
                    RectFill(rp, ox + px + st * 2, oy + y + r * 2, ox + px + k * 2 - 1, oy + y + r * 2 + 1);
                } else k++;
            }
        }
    }
}

/* "AT - Radio X" -> Laenderkuerzel "AT" und Name "Radio X" */
static const char *m_split(const char *name, char *code)
{
    code[0] = 0;
    if (strlen(name) > 5 && name[2] == ' ' && name[3] == '-' && name[4] == ' ') {
        code[0] = name[0]; code[1] = name[1]; code[2] = 0;
        return name + 5;
    }
    return name;
}

/* Laenderkennzeichen (26 Pixel breit, 10 hoch), in Teilstuecken gezeichnet (kein leeres Zwischenbild) */
static void m_chip(int x, int y, const char *code, int surround)
{
    int bg, fg;
    if (!code[0]) { fillRect(x, y, chipW, 10, surround); return; }
    if (strcmp(code, "AT") == 0) { bg = MC_AT; fg = MC_CH; }
    else if (strcmp(code, "DE") == 0) { bg = MC_DE; fg = MC_BG; }
    else if (strcmp(code, "CH") == 0) { bg = MC_CH; fg = MC_WHITE; }
    else { bg = MC_DIM; fg = MC_BG; }
    fillRect(x, y, chipW, 1, bg);
    fillRect(x, y + 1, chipPad, 8, bg);
    drawText(x + chipPad, y + 1, code, 2, fg, bg);
    fillRect(x + chipW - chipPad, y + 1, chipPad, 8, bg);
    fillRect(x, y + 9, chipW, 1, bg);
}

/* Aktueller Sender gross, ganzer Name sichtbar */
static void m_drawNow(void)
{
    char code[4], l1[100], l2[100];
    const char *nm = m_split(curName, code);
    int len = strlen(nm), i, sp;
    int maxw = 296 * hs;                                    /* verfuegbare Breite in Pixeln */
    int maxc = maxw / FW;
    if (maxc > 90) maxc = 90;
    m_chip(10, 24, code, MC_PANEL);
    fillRect(8, 36, 304, 22, MC_PANEL);
    if (len * 2 * FW <= maxw) {
        bigText(10, 38, nm, len, MC_WHITE, MC_PANEL);
    } else if (len * FW + 1 <= maxw) {                      /* fett: zweimal um einen Pixel versetzt */
        drawText(10, 42, nm, len, MC_WHITE, MC_PANEL);
        SetDrMd(rp, JAM1);
        Move(rp, ox + 10 * hs + 1, oy + 42 * vs + (FH * vs - FH) / 2 + bl);
        SetAPen(rp, mapPen(MC_WHITE));
        Text(rp, (STRPTR)nm, len);
        SetDrMd(rp, JAM2);
    } else {                                               /* zwei Zeilen */
        sp = -1;
        for (i = 0; i < len && i <= maxc; i++) if (nm[i] == ' ') sp = i;
        if (sp <= 0 || len - sp - 1 > maxc) sp = maxc;
        strncpy(l1, nm, sp); l1[sp] = 0;
        strncpy(l2, nm + sp + (nm[sp] == ' ' ? 1 : 0), maxc); l2[maxc] = 0;
        drawText(10, 38, l1, maxc, MC_WHITE, MC_PANEL);
        drawText(10, 48, l2, maxc, MC_WHITE, MC_PANEL);
    }
}

static void m_drawTitleLine(void)
{
    char b[104];
    int len = strlen(title), i;
    if (titleOff > len) titleOff = 0;
    for (i = 0; i < TITLE_CHARS; i++)
        b[i] = (titleOff + i < len) ? title[titleOff + i] : ' ';
    b[TITLE_CHARS] = 0;
    drawText(10, 59, b, TITLE_CHARS, MC_ACC, MC_PANEL);
}

static char m_cntShown[24] = "";
static char m_stShown[64] = "";

/* Kopfleiste rechts: Anzahl der Sender; unten links: Status */
static void m_drawTop(void)
{
    char b[24];
    int n;
    sprintf(b, "%d Sender", nStations);
    if (strcmp(b, m_cntShown) != 0) {
        bands(200, 0, 120, 15, 16, 4);
        fillRect(200, 15, 120, 1, MC_ACC);
        drawTextT(312 - cw(TextLength(rp, (STRPTR)b, strlen(b))), 4, b, strlen(b), MC_WHITE);
        strcpy(m_cntShown, b);
    }
    if (strcmp(status, m_stShown) != 0) {
        n = strlen(status);
        if (n > 240 * hs / FW) n = 240 * hs / FW;
        fillRect(8, 238, 240, 16, MC_BG);
        drawText(8, 242, status, n, MC_GRAY, MC_BG);
        strncpy(m_stShown, status, sizeof(m_stShown) - 1);
        m_stShown[sizeof(m_stShown) - 1] = 0;
    }
}

static int m_seg[2] = { 0, 0 };         /* leuchtende Segmente je Kanal, wie gezeichnet */

static void m_segDraw(int ch, int i, int on)
{
    int k = i * 35 / nSeg;              /* Lage auf der 35er-Skala */
    int c = on ? (k < 24 ? MC_GREEN : (k < 30 ? MC_YELLOW : MC_RED)) : MC_TRACK;
    fillRect(22 + i * segStep, ch ? 86 : 76, segW, 6, c);
}

static void m_drawMeters(void)
{
    int ch, want, i;
    for (ch = 0; ch < 2; ch++) {
        want = (ch ? levelR : levelL) * nSeg / 100;
        if (want > nSeg) want = nSeg;
        if (want < 0) want = 0;
        if (want > m_seg[ch]) for (i = m_seg[ch]; i < want; i++) m_segDraw(ch, i, 1);
        else for (i = want; i < m_seg[ch]; i++) m_segDraw(ch, i, 0);
        m_seg[ch] = want;
    }
}

static void m_drawMetersStatic(void)
{
    int i;
    drawText(10, 75, "L", 1, MC_DIM, MC_PANEL);
    drawText(10, 85, "R", 1, MC_DIM, MC_PANEL);
    for (i = 0; i < nSeg; i++) { m_segDraw(0, i, 0); m_segDraw(1, i, 0); }
    m_seg[0] = m_seg[1] = 0;
    m_drawMeters();
}

static int m_shownVol = -1;
static char m_shownVT[8] = "";

static void m_drawVolume(void)
{
    int fill = muted ? 0 : volume, fw;
    char b[8];
    if (muted) strcpy(b, "Mute"); else sprintf(b, "%d%%", volume);
    if (strcmp(b, m_shownVT) != 0) {
        fillRect(270, 105, 44, 14, MC_PANEL);
        drawText(312 - cw(TextLength(rp, (STRPTR)b, strlen(b))), 108, b, strlen(b), MC_WHITE, MC_PANEL);
        strcpy(m_shownVT, b);
    }
    if (fill != m_shownVol) {
        fw = fill * (M_SLW - KW) / 100;
        fillRect(M_SLX - 1, M_SLY - 2, M_SLW + 2, 2, MC_PANEL);
        fillRect(M_SLX - 1, M_SLY + 10, M_SLW + 2, 2, MC_PANEL);
        fillRect(M_SLX, M_SLY, fw, 5, 28);
        fillRect(M_SLX, M_SLY + 5, fw, 5, 29);
        fillRect(M_SLX + fw, M_SLY, M_SLW - fw, 5, 30);
        fillRect(M_SLX + fw, M_SLY + 5, M_SLW - fw, 5, 31);
        fillRect(M_SLX + fw, M_SLY - 2, KW, 14, MC_WHITE);
        m_shownVol = fill;
    }
}

static const int m_bx[5] = { 4, 82, 160, 238, 252 };
static const int m_bw[5] = { 76, 76, 76, 76, 64 };
static const int m_by[5] = { 126, 126, 126, 126, 238 };
static const int m_bh[5] = { 18, 18, 18, 18, 16 };

static void m_drawButton(int idx, int pressed)
{
    const char *label;
    int len, tw, th, active = 0;
    switch (idx) {
    case 0: label = "Leiser"; break;
    case 1: label = "Lauter"; break;
    case 2: label = muted ? "Mute AN" : "Mute"; break;
    case 3: label = mono ? "Mono AN" : "Stereo"; break;
    default: label = "Ende"; break;
    }
    if ((idx == 2 && muted) || (idx == 3 && mono)) active = 1;
    len = strlen(label);
    tw = TextLength(rp, (STRPTR)label, len);
    th = m_bh[idx];
#if BTN3D
    if (!pressed && active) fillRect(m_bx[idx], m_by[idx], m_bw[idx], th, MC_RED);
    else bands(m_bx[idx], m_by[idx], m_bw[idx], th, pressed ? 24 : 20, 4);      /* Verlauf */
    notch(m_bx[idx], m_by[idx], m_bw[idx], th, MC_BG);                            /* runde Ecken */
#else
    fillRect(m_bx[idx], m_by[idx], m_bw[idx], th, pressed ? MC_ACC : (active ? MC_RED : MC_TRACK));     /* flach, eckig */
#endif
    drawTextT(m_bx[idx] + (m_bw[idx] - cw(tw)) / 2, m_by[idx] + (th - 8) / 2, label, len, pressed ? MC_BG : MC_WHITE);   /* aktiv = rot */
}

static void m_drawButtons(void)
{
    int i;
    for (i = 0; i < 5; i++) m_drawButton(i, i == pressedBtn);
}

static void m_thumb(int *ty, int *th)
{
    int n = nStations, mt = maxTop(), h;
    h = n > VISIBLE_ROWS ? M_LIST_H * VISIBLE_ROWS / n : M_LIST_H;
    if (h < 12) h = 12;
    if (h > M_LIST_H) h = M_LIST_H;
    *th = h;
    *ty = M_ROWS_Y + (mt > 0 ? (M_LIST_H - h) * topRow / mt : 0);
}

static void m_drawScrollbar(void)
{
    int ty, th;
    m_thumb(&ty, &th);
    fillRect(M_SBX, M_ROWS_Y, M_SBW, ty - M_ROWS_Y, MC_TRACK);
    fillRect(M_SBX, ty, M_SBW, th, MC_ACC);
    fillRect(M_SBX, ty + th, M_SBW, M_ROWS_Y + M_LIST_H - ty - th, MC_TRACK);
}

static void m_drawList(void)
{
    int row;
    int nmc = (304 - LTX) * hs / FW;                       /* Zeichen je Name */
    int used = cw(nmc * FW);                               /* davon belegte logische Breite */
    if (nmc > 79) nmc = 79;
    for (row = 0; row < VISIBLE_ROWS; row++) {
        char code[4], b[84];
        const char *nm;
        int y = M_ROWS_Y + row * M_ROW_H;
        int i = topRow + row, sel = 0, cur = 0, bg, len;
        if (i < nStations) {
            sel = (i == selected);
            cur = (i == current);
            nm = m_split(stations[i], code);
        } else {
            nm = "";
            code[0] = 0;
        }
        bg = sel ? MC_SEL : MC_PANEL;
        fillRect(M_LIST_X, y, 3, M_ROW_H, sel ? MC_ACC : bg);
        fillRect(M_LIST_X + 3, y, 3, M_ROW_H, bg);
        fillRect(10, y, chipW, 1, bg);
        m_chip(10, y + 1, code, bg);
        fillRect(10, y + 11, chipW, 1, bg);
        fillRect(10 + chipW, y, LTX - 10 - chipW, M_ROW_H, bg);
        fillRect(LTX, y, 304 - LTX, 2, bg);
        len = strlen(nm);
        if (len > nmc) len = nmc;
        memcpy(b, nm, len);
        while (len < nmc) b[len++] = ' ';
        b[nmc] = 0;
        drawText(LTX, y + 2, b, nmc, cur ? MC_ACC : (sel ? MC_WHITE : MC_GRAY), bg);
        fillRect(LTX + used, y + 2, 304 - LTX - used, 8, bg);
        fillRect(LTX, y + 10, 304 - LTX, 2, bg);
    }
    m_drawScrollbar();
    m_drawTop();
}

static void m_drawAll(void)
{
    fillRect(0, 0, 320, 256, MC_BG);
    bands(0, 0, 320, 15, 16, 4);
    fillRect(0, 15, 320, 1, MC_ACC);
    drawTextT(8, 4, "Internetradio", 13, MC_ACC);
    fillRect(4, 20, 312, 50, MC_PANEL);
    fillRect(4, 72, 312, 28, MC_PANEL);
    fillRect(4, 102, 312, 20, MC_PANEL);
    fillRect(4, 148, 312, 88, MC_PANEL);
    notch(4, 20, 312, 50, MC_BG);
    notch(4, 72, 312, 28, MC_BG);
    notch(4, 102, 312, 20, MC_BG);
    notch(4, 148, 312, 88, MC_BG);
    drawText(LTX, 25, "L\xe4uft gerade", 12, MC_DIM, MC_PANEL);
    drawText(10, 108, "Vol", 3, MC_DIM, MC_PANEL);
    m_cntShown[0] = 0;
    m_stShown[0] = 0;
    m_shownVol = -1;
    m_shownVT[0] = 0;
    m_drawNow();
    m_drawTitleLine();
    m_drawMetersStatic();
    m_drawVolume();
    m_drawButtons();
    m_drawList();
    m_drawTop();
}

static BOOL m_handleClick(int x, int y)
{
    int b;
    for (b = 0; b < 5; b++) {
        if (x >= m_bx[b] && x < m_bx[b] + m_bw[b] && y >= m_by[b] && y < m_by[b] + m_bh[b]) {
            pressedBtn = b;
            m_drawButton(b, 1);
            switch (b) {
            case 0: setVolume(volume - 5); break;
            case 1: setVolume(volume + 5); break;
            case 2: toggleMute(); break;
            case 3: toggleMono(); break;
            default: return FALSE;
            }
            return TRUE;
        }
    }
    if (y >= M_SLY - 4 && y < M_SLY + 14 && x >= M_SLX - 2 && x < M_SLX + M_SLW + 2) {
        int v = ((x - M_SLX - KW / 2) * 100 + (M_SLW - KW) / 2) / (M_SLW - KW);
        setVolume(v);
        return TRUE;
    }
    if (y >= M_ROWS_Y && y < M_ROWS_Y + M_LIST_H) {
        if (x >= M_SBX - 2 && x < M_SBX + M_SBW + 2) {
            int ty, th;
            m_thumb(&ty, &th);
            if (y >= ty && y < ty + th) { dragging = TRUE; dragOffset = y - ty; ReportMouse(TRUE, win); }
            else if (y < ty) setTop(topRow - VISIBLE_ROWS);
            else setTop(topRow + VISIBLE_ROWS);
        } else if (x >= M_LIST_X && x < M_SBX - 2) {
            int i = topRow + (y - M_ROWS_Y) / M_ROW_H;
            if (i < nStations) { selected = i; drawList(); playSelected(); }
        }
    }
    return TRUE;
}

static void m_dragTo(int my)
{
    int ty, th, room, pos;
    m_thumb(&ty, &th);
    room = M_LIST_H - th;
    pos = my - dragOffset - M_ROWS_Y;
    if (room <= 0) return;
    if (pos < 0) pos = 0;
    if (pos > room) pos = room;
    setTop((pos * maxTop() + room / 2) / room);
}

static BOOL handleClick(int mx, int my)
{
    if (modern) return m_handleClick(mx / hs, my / vs);
    return c_handleClick(mx, my);
}

static BOOL c_handleClick(int mx, int my)
{
    int x = mx - ox, y = my - oy;
    if (y >= STATUS_Y && y < STATUS_Y + 13 && x > VOLBAR_X && x < VOLBAR_X + VOLBAR_W - 1) {
        setVolume(x - VOLBAR_X - 1);
        return TRUE;
    }
    if (x >= SB_X && x < SB_X + SB_W && y >= LIST_Y && y < LIST_Y + VISIBLE_ROWS * ROW_H) {
        int ty, th;
        if (y < LIST_Y + SB_ARROW) setTop(topRow - 1);
        else if (y >= LIST_Y + VISIBLE_ROWS * ROW_H - SB_ARROW) setTop(topRow + 1);
        else {
            thumbGeom(&ty, &th);
            if (y >= ty && y < ty + th) { dragging = TRUE; dragOffset = y - ty; ReportMouse(TRUE, win); }
            else if (y < ty) setTop(topRow - VISIBLE_ROWS);
            else setTop(topRow + VISIBLE_ROWS);
        }
        return TRUE;
    }
    if (x >= LIST_X && x < LIST_X + LIST_W && y >= LIST_Y && y < LIST_Y + VISIBLE_ROWS * ROW_H) {
        int i = topRow + (y - LIST_Y) / ROW_H;
        if (i < nStations) { selected = i; drawList(); playSelected(); }
    } else if (x >= BTN_X && x < BTN_X + BTN_W && y >= LIST_Y) {
        int b = (y - LIST_Y) / (BTN_H + 4);
        if (b >= 0 && b < 6 && btnLabel[b][0]) { pressedBtn = b; drawButton(b, 1); }
        switch (b) {
        case 0: setVolume(volume + 5); break;
        case 1: setVolume(volume - 5); break;
        case 2: toggleMute(); break;
        case 3: toggleMono(); break;
        case 5: return FALSE;
        }
    }
    return TRUE;
}

static void dragTo(int my)
{
    if (modern) { m_dragTo(my / vs); return; }
    c_dragTo(my);
}

static void c_dragTo(int my)
{
    int trackY = LIST_Y + SB_ARROW;
    int trackH = VISIBLE_ROWS * ROW_H - 2 * SB_ARROW;
    int ty, th, room, pos;
    thumbGeom(&ty, &th);
    room = trackH - th;
    pos = (my - oy) - dragOffset - trackY;
    if (room <= 0) return;
    if (pos < 0) pos = 0;
    if (pos > room) pos = room;
    setTop((pos * maxTop() + room / 2) / room);
}

static void pollWheel(void)
{
    static int ticks = 0;
    UBYTE v;
    BYTE d;

    tickTitle();
    if (listDirty > 0 && --listDirty == 0) drawList();
    if (nStations == 0 && serOpen && ++ticks >= 30) {   /* alle 3 s, solange keine Liste da ist */
        ticks = 0;
        serialSend("\rATE0\rATRA1\rATRL\rATR?\r");
    }
    v = (UBYTE)(*(volatile UWORD *)SCRDAT_ADDR & 0xFF);
    d = (BYTE)(v - lastWheel);
    lastWheel = v;
    if (d) setTop(topRow - d * 2);       /* Rad nach oben (positiv) = Liste nach oben */
}

static BOOL handleKey(UWORD code, UWORD qual, ULONG cls)
{
    (void)qual;
    if (cls == IDCMP_VANILLAKEY) {
        switch (code) {
        case 'q': case 'Q': case 27: return FALSE;
        case 13: playSelected(); break;
        case '+': case '=': setVolume(volume + 5); break;
        case '-': setVolume(volume - 5); break;
        case 'm': case 'M': case 's': case 'S': toggleMute(); break;
        case 'r': case 'R': reload(); break;
        }
    } else if (cls == IDCMP_RAWKEY) {
        if (code == 0x4C) moveSelection(-1);        /* Cursor hoch */
        else if (code == 0x4D) moveSelection(1);    /* Cursor runter */
    }
    return TRUE;
}

/* ---------------- Hauptprogramm ---------------- */
int main(void)
{
    BOOL running = TRUE;
    static char wtitle[64];
    ULONG scrErr = 0, scrErr2 = 0;
    ULONG winSig, serSig;

    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 37);
    GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 37);
    if (!IntuitionBase || !GfxBase) goto done;

    /* Ohne Workbench (Start von der Boot-Diskette) laeuft kein Desktop: eigenen Bildschirm oeffnen,
       PAL hires 640x256 ohne Interlace, 4 Farben im Stil der Workbench */
    if (FindTask((STRPTR)"Workbench") == NULL) {
        static UWORD pens[] = { 0, 1, 1, 2, 1, 3, 2, 0, 2, 1, 2, 1, (UWORD)~0 };
        if (TRY_HIRES) {
            ownScreen = OpenScreenTags(NULL,
                SA_Width, 640, SA_Height, 256, SA_Depth, 4,
                SA_DisplayID, 0x00029000UL,          /* PAL, hires */
                SA_Type, CUSTOMSCREEN,
                SA_Title, (ULONG)"Internetradio",
                SA_Pens, (ULONG)pens,
                SA_ErrorCode, (ULONG)&scrErr2,
                TAG_DONE);
            if (ownScreen) {
                hs = 2; chipW = 14; chipPad = 3; LTX = 26;
                segW = 3; segStep = 4; nSeg = 70;
                slx = 26; slw = 238;
            }
        }
        if (!ownScreen) ownScreen = OpenScreenTags(NULL,
            SA_Width, 320, SA_Height, 256, SA_Depth, 5,
            SA_DisplayID, 0x00021000UL,          /* PAL, lowres */
            SA_Type, CUSTOMSCREEN,
            SA_Title, (ULONG)"Internetradio",
            SA_Pens, (ULONG)pens,
            SA_ErrorCode, (ULONG)&scrErr,
            TAG_DONE);
        if (ownScreen) {
            LoadRGB4(&ownScreen->ViewPort, (UWORD *)modernPal, hs == 2 ? 16 : 32);
            modern = TRUE;
            visRows = 7;
        }
        if (ownScreen) ShowTitle(ownScreen, FALSE);
    }

    strcpy(wtitle, "Internetradio");
    if (modern)
        win = OpenWindowTags(NULL,
            WA_Left, 0, WA_Top, 0, WA_Width, 320 * hs, WA_Height, 256,
            WA_CustomScreen, (ULONG)ownScreen,
            WA_Borderless, TRUE, WA_Backdrop, TRUE,
            WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY |
                      IDCMP_RAWKEY | IDCMP_REFRESHWINDOW | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS,
            WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_RMBTrap, TRUE,
            TAG_DONE);
    else
    win = OpenWindowTags(NULL,
        WA_Left, ownScreen ? 110 : 40, WA_Top, ownScreen ? 24 : 30,
        ownScreen ? WA_CustomScreen : TAG_IGNORE, (ULONG)ownScreen,
        WA_InnerWidth, INNER_W, WA_InnerHeight, INNER_H,
        WA_Title, (ULONG)wtitle,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_VANILLAKEY |
                  IDCMP_RAWKEY | IDCMP_REFRESHWINDOW | IDCMP_MOUSEMOVE | IDCMP_INTUITICKS,
        WA_CloseGadget, TRUE, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
        WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
        TAG_DONE);
    if (!win) goto done;

    {
        struct DrawInfo *dri = GetScreenDrawInfo(win->WScreen);
        if (dri) {
            P_TEXT  = dri->dri_Pens[TEXTPEN];
            P_BACK  = dri->dri_Pens[BACKGROUNDPEN];
            P_FILL  = dri->dri_Pens[FILLPEN];
            P_FTEXT = dri->dri_Pens[FILLTEXTPEN];
            FreeScreenDrawInfo(win->WScreen, dri);
        }
    }
    rp = win->RPort;
    if (ownScreen) {                      /* feste Schriftbreite 8 Pixel, auf die das Layout ausgelegt ist */
        static struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
        topazFont = OpenFont(&ta);
        if (topazFont) SetFont(rp, topazFont);
    }
    ox = win->BorderLeft;
    oy = win->BorderTop;
    bl = rp->TxBaseline;
    FW = TextLength(rp, (STRPTR)"M", 1);
    if (FW < 4) FW = 8;
    FH = rp->TxHeight;
    titleChars = (INNER_W - 8) / FW;
    if (titleChars > 100) titleChars = 100;
    listChars = (LIST_W - 4) / FW;
    if (listChars > 54) listChars = 54;
    volbarX = 4 + 8 * FW + 4;                  /* hinter "Vol 100%" bzw. "Vol Mute" */
    if (modern) { titleChars = 296 * hs / FW; if (titleChars > 99) titleChars = 99; }
    SetDrMd(rp, JAM2);

    lastWheel = (UBYTE)(*(volatile UWORD *)SCRDAT_ADDR & 0xFF);
    strcpy(status, "starte...");
    drawAll();

    if (!serialStart()) {
        if (serErr == 201) strcpy(status, "Devs:serial.device fehlt");
        else sprintf(status, "serial.device Fehler %d", serErr);
        drawStatus();
    } else {
        serialRead();
        serialSend("\r\rATE0\rATRA1\rATRL\rATR?\r");
        sendCmd("V", volume);                /* Lautstaerke des Radios mit der Anzeige abgleichen */
        sendCmd("M", mono ? 1 : 0);          /* ebenso Mono/Stereo */
        strcpy(status, "warte auf Radio...");
        drawStatus();
    }

    winSig = 1UL << win->UserPort->mp_SigBit;
    serSig = serOpen ? (1UL << rdPort->mp_SigBit) : 0;

    while (running) {
        ULONG got = Wait(winSig | serSig);

        if (got & serSig) {
            struct Message *m;
            while ((m = GetMsg(rdPort)) != NULL) {
                readPending = FALSE;
                feedByte(rxByte);
                serialRead();
            }
        }
        if (got & winSig) {
            struct IntuiMessage *im;
            while ((im = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
                ULONG cls = im->Class;
                UWORD code = im->Code, qual = im->Qualifier;
                WORD mx = im->MouseX, my = im->MouseY;
                ReplyMsg((struct Message *)im);
                switch (cls) {
                case IDCMP_CLOSEWINDOW: running = FALSE; break;
                case IDCMP_REFRESHWINDOW:
                    BeginRefresh(win); drawAll(); EndRefresh(win, TRUE);
                    break;
                case IDCMP_MOUSEBUTTONS:
                    if (code == SELECTDOWN) { if (!handleClick(mx, my)) running = FALSE; }
                    else if (code == SELECTUP && dragging) { dragging = FALSE; ReportMouse(FALSE, win); }
                    else if (code == SELECTUP && pressedBtn >= 0) { int pb = pressedBtn; pressedBtn = -1; drawButton(pb, 0); }
                    break;
                case IDCMP_MOUSEMOVE:
                    if (dragging) dragTo(my);
                    break;
                case IDCMP_INTUITICKS:
                    pollWheel();
                    break;
                case IDCMP_VANILLAKEY:
                case IDCMP_RAWKEY:
                    if (!(cls == IDCMP_RAWKEY && (code & 0x80)))   /* nur Taste gedrueckt */
                        if (!handleKey(code, qual, cls)) running = FALSE;
                    break;
                }
            }
        }
    }

done:
    if (serOpen) serialSend("ATRX\rATRA0\r\r");        /* Ende = Radio aus, Meldungen abbestellen */
    serialStop();
    if (win) CloseWindow(win);
    if (topazFont) CloseFont(topazFont);
    if (ownScreen) CloseScreen(ownScreen);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    return 0;
}
