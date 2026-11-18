/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 TinyParts
 * Teil des Projekts NanoMig-Webradio, Erweiterung des FPGA-Companion (Apache-2.0).
 */

/*
  radio.c - internet radio inside the FPGA companion

  Two tasks plus a ring buffer:
    net task    (radio_net_task):   connects to the station, follows redirects, parses the
                                    ICY headers and metadata and fills the ring buffer
    decode task (radio_dec_task):   waits for a pre-buffer, decodes MP3 frames, resamples to
                                    48 kHz and keeps the audio fifo of the core topped up

  The core plays its fifo at a fixed 48 kHz, so the decoder is paced by the free space in
  that fifo. Network hiccups are absorbed by the ring buffer; when it runs dry the core
  plays silence.
*/

#ifdef RADIO_ENABLE

#include <string.h>
#include <strings.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/netif.h"

#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

#include "radio.h"
#include "sysctrl.h"
#include "at_wifi.h"
#include "sdc.h"
#include "debug.h"

#define RING_SIZE      32768             // power of two
#define PREBUF_BYTES   20480             // pre-buffer before playback starts (~1.3 s at 128 kbit/s)
#define MBUF_SIZE      16384             // decoder input window (minimp3 recommends 16 kB)
#define OUT_FRAMES     256               // frames per SPI transfer
#define RADIO_AUTOSTART -1               // station that starts automatically (-1 = none, the Amiga program chooses)

/* ------------------------------------------------------------------ stations */
// built-in defaults, used when the SD card has no Radiosender.txt
struct dstation { const char *name; const char *url; };

static const struct dstation default_stations[] = {
  { "Hitradio Ö3",              "http://orf-live.ors-shoutcast.at/oe3-q2a" },
  { "Ö1",                       "http://orf-live.ors-shoutcast.at/oe1-q2a" },
  { "FM4",                       "http://orf-live.ors-shoutcast.at/fm4-q2a" },
  { "Radio Oberösterreich",     "http://orf-live.ors-shoutcast.at/ooe-q2a" },
  { "Radio Wien",                "http://orf-live.ors-shoutcast.at/wie-q2a" },
  { "Radio Niederösterreich",   "http://orf-live.ors-shoutcast.at/noe-q2a" },
  { "Radio Salzburg",            "http://orf-live.ors-shoutcast.at/sbg-q2a" },
  { "Radio Steiermark",          "http://orf-live.ors-shoutcast.at/stm-q2a" },
  { "Antenne Steiermark",        "http://live.antenne.at/as" },
  { "Antenne Kärnten",          "http://live.antenne.at/ak" },
  { "Kronehit",                  "http://onair-ha1.krone.at/kronehit1058.mp3" },
  { "Radio Arabella",            "http://frontend.streams.arabella.at/arabella-niederoesterreich/stream/mp3" },
  { "Rock Antenne Österreich",  "http://s6-webradio.webradio.de/rockantenne-oesterreich" },
  { "Radio Klassik Stephansdom", "http://radioklassikstephansdom.ice.infomaniak.ch/radioklassikstephansdom.mp3" },
  { "Deutschlandfunk",           "http://st01.dlf.de/dlf/01/128/mp3/stream.mp3" },
  { "Deutschlandfunk Kultur",    "http://st02.dlf.de/dlf/02/128/mp3/stream.mp3" },
  { "Deutschlandfunk Nova",      "http://st03.dlf.de/dlf/03/128/mp3/stream.mp3" },
  { "SWR3",                      "http://dispatcher.rndfnk.com/swr/swr3/live/mp3/128/stream.mp3" },
  { "Bayern 3",                  "http://dispatcher.rndfnk.com/br/br3/live/mp3/mid" },
  { "BR Klassik",                "http://dispatcher.rndfnk.com/br/brklassik/live/mp3/mid" },
  { "WDR 2",                     "http://wdr-wdr2-rheinland.icecast.wdr.de/wdr/wdr2/rheinland/mp3/128/stream.mp3" },
  { "WDR 4",                     "http://wdr-wdr4-live.icecast.wdr.de/wdr/wdr4/live/mp3/128/stream.mp3" },
  { "NDR 2",                     "http://icecast.ndr.de/ndr/ndr2/hamburg/mp3/128/stream.mp3" },
  { "hr3",                       "http://dispatcher.rndfnk.com/hr/hr3/live/mp3/128/stream.mp3" },
  { "MDR Jump",                  "http://mdr-284320-0.sslcast.mdr.de/mdr/284320/0/mp3/high/stream.mp3" },
  { "Radio Eins",                "http://dispatcher.rndfnk.com/rbb/radioeins/live/mp3/mid" },
  { "Antenne Bayern",            "http://mp3channels.webradio.antenne.de/antenne" },
  { "Hit Radio FFH",             "http://mp3.ffh.de/radioffh/hqlivestream.mp3" },
  { "Radio Bob",                 "http://streams.radiobob.de/bob-live/mp3-192/streams.radiobob.de/" },
  { "Rock Antenne",              "http://mp3channels.webradio.rockantenne.de/rockantenne" },
  { "Big FM",                    "http://streams.bigfm.de/bigfm-deutschland-128-mp3" },
};
#define NUM_DEFAULT ((int)(sizeof(default_stations)/sizeof(default_stations[0])))

// the live list: defaults at boot, replaced by the file on the SD card when it can be read
#define MAX_STATIONS      64
#define STATION_NAME_MAX  40
#define STATION_URL_MAX   200
#define STATION_FILE      CARD_MOUNTPOINT "/Radiosender.txt"
struct station { char name[STATION_NAME_MAX]; char url[STATION_URL_MAX]; };
static struct station stations[MAX_STATIONS];
static volatile int n_stations;
#define NUM_STATIONS n_stations

static void load_defaults(void) {
  int n = NUM_DEFAULT < MAX_STATIONS ? NUM_DEFAULT : MAX_STATIONS;
  for(int i=0; i<n; i++) {
    strncpy(stations[i].name, default_stations[i].name, STATION_NAME_MAX-1); stations[i].name[STATION_NAME_MAX-1] = 0;
    strncpy(stations[i].url, default_stations[i].url, STATION_URL_MAX-1); stations[i].url[STATION_URL_MAX-1] = 0;
  }
  n_stations = n;
}

// Radiosender.txt: one station per line, "Name | http://address"; empty lines and lines starting with # are ignored
static char list_file[12288];
static struct station list_tmp[MAX_STATIONS];

static char *trim(char *p) {
  while(*p == ' ' || *p == 9) p++;
  char *e = p + strlen(p);
  while(e > p && (e[-1] == ' ' || e[-1] == 9 || e[-1] == 13)) *--e = 0;
  return p;
}

// reads the station file; returns the number of stations or -1 when it could not be read
static int load_station_file(void) {
  FIL fil;
  UINT br = 0;
  sdc_lock();
  FRESULT r = f_open(&fil, STATION_FILE, FA_OPEN_EXISTING | FA_READ);
  if(r == FR_OK) {
    r = f_read(&fil, list_file, sizeof(list_file)-1, &br);
    f_close(&fil);
  }
  sdc_unlock();
  if(r != FR_OK) { debugf("radio: no station file (%d)", (int)r); return -1; }
  list_file[br] = 0;

  int n = 0;
  char *p = list_file;
  if((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;   // UTF-8 BOM
  while(*p && n < MAX_STATIONS) {
    char *line = p;
    char *nl = strchr(p, 10);
    if(nl) { *nl = 0; p = nl + 1; } else p += strlen(p);
    line = trim(line);
    if(!*line || *line == '#') continue;
    char *bar = strchr(line, '|');
    if(!bar) continue;
    *bar = 0;
    char *name = trim(line);
    char *url = trim(bar + 1);
    if(!*name || strncasecmp(url, "http://", 7) != 0) continue;
    if(strlen(url) >= STATION_URL_MAX) continue;                  // too long, skip
    strncpy(list_tmp[n].name, name, STATION_NAME_MAX-1); list_tmp[n].name[STATION_NAME_MAX-1] = 0;
    // do not cut a UTF-8 character in half
    int l = strlen(list_tmp[n].name);
    while(l > 0 && ((unsigned char)list_tmp[n].name[l-1] & 0xC0) == 0x80 && strlen(name) >= STATION_NAME_MAX) list_tmp[n].name[--l] = 0;
    strcpy(list_tmp[n].url, url);
    n++;
  }
  if(n == 0) return -1;
  memcpy(stations, list_tmp, sizeof(list_tmp[0]) * n);
  n_stations = n;
  debugf("radio: %d stations from sd card", n);
  return n;
}

/* ------------------------------------------------------------------ state */
static volatile int      req_station = RADIO_AUTOSTART;   // wanted station, -1 = off
static volatile uint32_t req_gen = 1;                     // changes with every request
static volatile uint32_t flush_gen = 0;                   // net task -> decoder: start over
static volatile uint32_t ack_gen = 0;                     // decoder -> net task: done flushing
static volatile int      playing_station = -1;
static volatile int      gain = 256;                      // 0..256 (volume)
static char              title[128] = "";
static volatile uint32_t title_ver = 1;      // changes whenever the title text changes
static volatile uint32_t station_ver = 1;    // changes whenever a station was requested
static char              err_msg[48] = "";
static volatile uint32_t err_ver;
static volatile int      peak_l, peak_r;     // since the last status message
static volatile bool     subscribed;         // the Amiga program wants status messages
static volatile bool     mono;               // downmix to mono (atrm1)
static volatile uint32_t m_ok, m_fail;      // level messages sent / dropped (diagnostics)
static volatile uint32_t m_gap_max;          // longest gap between two level messages in ms

/* diagnostic counters, shown by the status server (tcp port 7777) */
static volatile uint32_t st_netbytes, st_frames, st_connects;
static char              st_msg[48] = "idle";

/* ring buffer, single writer (net task) / single reader (decoder) */
static uint8_t ring[RING_SIZE];
static volatile uint32_t ring_wr, ring_rd;

static inline uint32_t ring_fill(void) { return ring_wr - ring_rd; }

static void ring_get(uint8_t *dst, uint32_t n) {
  uint32_t pos = ring_rd & (RING_SIZE-1);
  uint32_t first = RING_SIZE - pos;
  if(first > n) first = n;
  memcpy(dst, ring + pos, first);
  if(n > first) memcpy(dst + first, ring, n - first);
  ring_rd += n;
}

static bool aborted(uint32_t gen) { return req_gen != gen; }

static void set_err(const char *m) {
  strncpy(err_msg, m, sizeof(err_msg)-1); err_msg[sizeof(err_msg)-1] = 0;
  err_ver++;
}

// blocks while the ring is full (TCP then slows the sender down)
static bool ring_put(const uint8_t *src, uint32_t n, uint32_t gen) {
  while(n) {
    uint32_t space = RING_SIZE - ring_fill();
    if(!space) {
      if(aborted(gen)) return false;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    uint32_t chunk = n < space ? n : space;
    uint32_t pos = ring_wr & (RING_SIZE-1);
    uint32_t first = RING_SIZE - pos;
    if(first > chunk) first = chunk;
    memcpy(ring + pos, src, first);
    if(chunk > first) memcpy(ring, src + first, chunk - first);
    ring_wr += chunk;
    st_netbytes += chunk;
    src += chunk; n -= chunk;
  }
  return true;
}

static bool network_ready(void) {
  struct netif *n = netif_default;
  return n && netif_is_up(n) && netif_is_link_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n));
}

/* ------------------------------------------------------------------ text helpers */
static const char *hdr_value(const char *hdr, const char *name) {
  size_t nl = strlen(name);
  const char *p = hdr;
  while(p && *p) {
    if(strncasecmp(p, name, nl) == 0) {
      p += nl;
      while(*p == ' ') p++;
      return p;
    }
    p = strstr(p, "\r\n");
    if(p) p += 2;
  }
  return NULL;
}

/* ------------------------------------------------------------------ stream parsing */
struct stream_state {
  uint32_t gen;
  int metaint;
  int to_meta;
  int meta_state;      // 0 audio, 1 length byte, 2 metadata
  int meta_len, meta_pos;
  char meta[16*255+1];
};

static struct stream_state ss;

static void parse_meta(const char *m) {
  const char *p = strstr(m, "StreamTitle='");
  if(!p) return;
  p += 13;
  const char *e = strstr(p, "';");
  size_t n = e ? (size_t)(e - p) : strlen(p);
  char raw[128];
  if(n >= sizeof(raw)) n = sizeof(raw) - 1;
  memcpy(raw, p, n); raw[n] = 0;
  strncpy(title, raw, sizeof(title)-1); title[sizeof(title)-1] = 0;
  title_ver++;
  debugf("radio: title %s", title);
}

static bool feed(const uint8_t *p, int n) {
  while(n > 0) {
    if(ss.meta_state == 0) {
      int take = n;
      if(ss.metaint && take > ss.to_meta) take = ss.to_meta;
      if(!ring_put(p, take, ss.gen)) return false;
      p += take; n -= take;
      if(ss.metaint) {
        ss.to_meta -= take;
        if(ss.to_meta == 0) ss.meta_state = 1;
      }
    } else if(ss.meta_state == 1) {
      ss.meta_len = *p++ * 16; n--;
      ss.meta_pos = 0;
      if(ss.meta_len == 0) { ss.meta_state = 0; ss.to_meta = ss.metaint; }
      else ss.meta_state = 2;
    } else {
      int take = ss.meta_len - ss.meta_pos;
      if(take > n) take = n;
      memcpy(ss.meta + ss.meta_pos, p, take);
      ss.meta_pos += take; p += take; n -= take;
      if(ss.meta_pos == ss.meta_len) {
        ss.meta[ss.meta_len] = 0;
        parse_meta(ss.meta);
        ss.meta_state = 0; ss.to_meta = ss.metaint;
      }
    }
  }
  return true;
}

/* ------------------------------------------------------------------ net task */
static uint8_t net_tmp[1460];
static char net_hdr[1536];

// returns when the station was changed, the connection failed or ended
static void run_station(int st, uint32_t gen) {
  char url[300];
  strncpy(url, stations[st].url, sizeof(url)-1); url[sizeof(url)-1] = 0;
  int fd = -1;
  char *body = NULL;
  int blen = 0;

  title[0] = 0;
  title_ver++;
  memset(&ss, 0, sizeof(ss));
  ss.gen = gen;

  for(int hop=0; hop<5 && !aborted(gen); hop++) {
    if(strncmp(url, "http://", 7) != 0) { debugf("radio: only http urls (%s)", url); return; }

    char host[100]; char port[8] = "80"; const char *path = "/";
    const char *h = url + 7;
    size_t hl = 0;
    while(h[hl] && h[hl] != '/' && h[hl] != ':' && hl < sizeof(host)-1) { host[hl] = h[hl]; hl++; }
    host[hl] = 0;
    const char *q = h + hl;
    if(*q == ':') {
      q++; int i = 0;
      while(*q && *q != '/' && i < (int)sizeof(port)-1) port[i++] = *q++;
      port[i] = 0;
    }
    if(*q == '/') path = q;

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if(lwip_getaddrinfo(host, port, &hints, &res) != 0 || !res) { debugf("radio: dns failed for %s", host); set_err("Sender nicht gefunden"); return; }

    fd = lwip_socket(res->ai_family, res->ai_socktype, 0);
    if(fd < 0) { lwip_freeaddrinfo(res); return; }
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500000 };
    lwip_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if(lwip_connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
      debugf("radio: connect failed"); set_err("Keine Verbindung");
      lwip_freeaddrinfo(res); lwip_close(fd); return;
    }
    lwip_freeaddrinfo(res);

    int rl = snprintf(net_hdr, sizeof(net_hdr),
        "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: AmigaRadio/1.0\r\nIcy-MetaData: 1\r\nConnection: close\r\n\r\n",
        path, host);
    lwip_send(fd, net_hdr, rl, 0);

    // read the headers
    int got = 0, idle = 0;
    body = NULL;
    while(got < (int)sizeof(net_hdr)-1 && !aborted(gen)) {
      int n = lwip_recv(fd, net_hdr + got, sizeof(net_hdr)-1-got, 0);
      if(n > 0) {
        got += n; net_hdr[got] = 0; idle = 0;
        char *e = strstr(net_hdr, "\r\n\r\n");
        if(e) { body = e + 4; blen = got - (int)(body - net_hdr); *e = 0; break; }
      } else if(n == 0 || ++idle > 16) break;
    }
    if(!body) { debugf("radio: no response"); set_err("Keine Antwort"); lwip_close(fd); return; }

    int status = 0;
    if(strncmp(net_hdr, "ICY 200", 7) == 0) status = 200;
    else if(strncmp(net_hdr, "HTTP/", 5) == 0) status = atoi(net_hdr + 9);

    if(status >= 300 && status < 400) {
      const char *loc = hdr_value(net_hdr, "location:");
      if(!loc) { lwip_close(fd); return; }
      size_t ll = strcspn(loc, "\r\n");
      if(ll >= sizeof(url)) ll = sizeof(url)-1;
      memcpy(url, loc, ll); url[ll] = 0;
      lwip_close(fd); fd = -1; body = NULL;
      continue;                        // follow the redirect
    }
    if(status != 200) {
      char m[32];
      debugf("radio: http status %d", status);
      snprintf(m, sizeof(m), "HTTP-Status %d", status); set_err(m);
      lwip_close(fd); return;
    }

    const char *mi = hdr_value(net_hdr, "icy-metaint:");
    ss.metaint = mi ? atoi(mi) : 0;
    ss.to_meta = ss.metaint;
    const char *nm = hdr_value(net_hdr, "icy-name:");
    if(nm) {
      char raw[128]; size_t n = strcspn(nm, "\r\n"); if(n >= sizeof(raw)) n = sizeof(raw)-1;
      memcpy(raw, nm, n); raw[n] = 0;
      strncpy(title, raw, sizeof(title)-1); title[sizeof(title)-1] = 0;
      title_ver++;
    }
    break;
  }
  if(fd < 0 || !body || aborted(gen)) { if(fd >= 0) lwip_close(fd); return; }

  debugf("radio: streaming %s (metaint %d)", stations[st].name, ss.metaint);
  st_connects++;
  strcpy(st_msg, "streaming");
  playing_station = st;

  if(blen > 0 && !feed((const uint8_t *)body, blen)) { lwip_close(fd); return; }

  int idle = 0;
  while(!aborted(gen)) {
    int n = lwip_recv(fd, net_tmp, sizeof(net_tmp), 0);
    if(n > 0) {
      idle = 0;
      if(!feed(net_tmp, n)) break;
    } else if(n == 0) break;
    else if(++idle > 24) { debugf("radio: stream stalled"); set_err("Stream haengt"); break; }   // 12 s without data
  }
  lwip_close(fd);
  debugf("radio: connection closed");
}

static void radio_net_task(__attribute__((unused)) void *parms) {
  for(;;) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if(req_station < 0) { strcpy(st_msg, "off"); continue; }
    if(!network_ready()) { strcpy(st_msg, "no network"); continue; }
    // only when the core has the audio fifo (an older core returns 0 free entries)
    if(sys_audio_space() < 1000) { strcpy(st_msg, "core has no audio fifo"); continue; }

    uint32_t gen = req_gen;
    int st = req_station;

    // tell the decoder to start over and wait until it has done so
    flush_gen = gen;
    for(int i=0; i<200 && ack_gen != gen && !aborted(gen); i++) vTaskDelay(pdMS_TO_TICKS(10));
    if(aborted(gen)) continue;

    run_station(st, gen);
    playing_station = -1;

    if(!aborted(gen) && req_station >= 0) {
      strcpy(st_msg, "reconnecting");
      debugf("radio: reconnecting in 3 s");
      for(int i=0; i<300 && !aborted(gen); i++) vTaskDelay(pdMS_TO_TICKS(10));
      // same request again: bump the generation so the decoder flushes
      if(!aborted(gen)) req_gen++;
    }
  }
}

/* ------------------------------------------------------------------ decoder / resampler */
static mp3dec_t dec;
static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static uint8_t mbuf[MBUF_SIZE];
static int16_t out_buf[2 * OUT_FRAMES];
static int out_n;
static uint32_t rs_phase;
static int rs_prev_l, rs_prev_r;
static uint32_t dec_gen;

static void resampler_reset(void) {
  out_n = 0; rs_phase = 0; rs_prev_l = rs_prev_r = 0;
}

static bool out_flush(void) {
  while(sys_audio_space() < (uint16_t)out_n) {
    if(flush_gen != dec_gen) return false;      // station change: drop the rest
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  sys_audio_write(out_buf, out_n);
  out_n = 0;
  return true;
}

static bool output_frames(const mp3d_sample_t *s, int frames, int channels, int hz) {
  uint32_t step = (uint32_t)(((uint64_t)hz << 16) / 48000);
  int g = gain;
  for(int i=0; i<frames; i++) {
    int cl = s[i*channels];
    int cr = (channels > 1) ? s[i*channels+1] : cl;
    if(mono) { int m = (cl + cr) >> 1; cl = cr = m; }
    while(rs_phase < 65536) {
      int l = rs_prev_l + (((cl - rs_prev_l) * (int)rs_phase) >> 16);
      int r = rs_prev_r + (((cr - rs_prev_r) * (int)rs_phase) >> 16);
      out_buf[2*out_n]   = (int16_t)((l * g) >> 8);
      out_buf[2*out_n+1] = (int16_t)((r * g) >> 8);
      if(++out_n == OUT_FRAMES && !out_flush()) return false;
      rs_phase += step;
    }
    rs_phase -= 65536;
    rs_prev_l = cl; rs_prev_r = cr;
  }
  return true;
}

static void radio_dec_task(__attribute__((unused)) void *parms) {
  int mlen = 0;
  bool prebuf = false;
  mp3dec_init(&dec);

  for(;;) {
    // new request from the net task: start over
    if(flush_gen != dec_gen) {
      dec_gen = flush_gen;
      ring_rd = ring_wr;
      mlen = 0; prebuf = false;
      mp3dec_init(&dec);
      resampler_reset();
      ack_gen = dec_gen;
    }
    if(req_station < 0 || req_gen != dec_gen) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

    if(!prebuf) {
      if(ring_fill() >= PREBUF_BYTES) prebuf = true;
      else { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    }

    uint32_t take = MBUF_SIZE - mlen;
    if(take > ring_fill()) take = ring_fill();
    if(take) { ring_get(mbuf + mlen, take); mlen += take; }

    mp3dec_frame_info_t info;
    int samples = mp3dec_decode_frame(&dec, mbuf, mlen, pcm, &info);

    if(info.frame_bytes == 0) {                  // not enough data for a frame
      if(mlen >= MBUF_SIZE) mlen = 0;            // garbage, start over
      else vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    if(info.frame_bytes < mlen) memmove(mbuf, mbuf + info.frame_bytes, mlen - info.frame_bytes);
    mlen -= info.frame_bytes;

    if(samples > 0 && info.hz > 0) {
      st_frames++;
      int pl = 0, pr = 0;
      for(int i=0; i<samples; i++) {
        int l = pcm[i*info.channels];
        int r = (info.channels > 1) ? pcm[i*info.channels+1] : l;
        if(l < 0) l = -l;
        if(r < 0) r = -r;
        if(l > pl) pl = l;
        if(r > pr) pr = r;
      }
      pl = (pl * gain) >> 8; pr = (pr * gain) >> 8;
      if(pl > peak_l) peak_l = pl;
      if(pr > peak_r) peak_r = pr;
    }
    if(samples > 0 && info.hz > 0)
      output_frames(pcm, samples, info.channels, info.hz);
  }
}

/* ------------------------------------------------------------------ status server */
// A tiny diagnostic aid: connect to tcp port 7777 and read one status line per second
static void radio_status_task(__attribute__((unused)) void *parms) {
  int lfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = lwip_htons(7777);
  addr.sin_addr.s_addr = lwip_htonl(INADDR_ANY);
  if(lfd < 0 || lwip_bind(lfd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || lwip_listen(lfd, 1) != 0) {
    vTaskDelete(NULL);
    return;
  }
  for(;;) {
    int cfd = lwip_accept(lfd, NULL, NULL);
    if(cfd < 0) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    uint32_t last_bytes = 0, last_frames = 0;
    for(;;) {
      char line[320];
      uint32_t nb = st_netbytes, fr = st_frames;
      int n = snprintf(line, sizeof(line),
        "%s | station %d | ring %u | fifo free %u | net %u B/s | frames %u/s | connects %u | heap free %u | at cmds %u dropped %u last [%s|%s|%s] | meter ok %u fail %u maxgap %u ms | title: %s\n",
        st_msg, (int)req_station, (unsigned)ring_fill(), (unsigned)sys_audio_space(),
        (unsigned)(nb - last_bytes), (unsigned)(fr - last_frames), (unsigned)st_connects,
        (unsigned)xPortGetFreeHeapSize(), (unsigned)at_wifi_commands(), (unsigned)at_wifi_dropped(),
        at_wifi_last_command((int)at_wifi_commands()-1), at_wifi_last_command((int)at_wifi_commands()-2),
        at_wifi_last_command((int)at_wifi_commands()-3),
        (unsigned)m_ok, (unsigned)m_fail, (unsigned)m_gap_max, title);
      m_gap_max = 0;
      last_bytes = nb; last_frames = fr;
      if(lwip_send(cfd, line, n, 0) < 0) break;
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    lwip_close(cfd);
  }
}

/* ------------------------------------------------------------------ amiga interface (AT commands) */
// status messages for the Amiga program, one line each (same format the ESP32 version used):
//   S<n>,<name>   P<n>,<name>   T<title>   M<l>,<r>   W<text>   E<text>   OK
// mode 0: reply to a command (waits), 1: important message (up to 300 ms), 2: level meter (15 ms)
static bool send_line(const char *prefix, const char *text, int mode) {
  char buf[200];
  snprintf(buf, sizeof(buf), "%s%s\r\n", prefix, text);
  if(mode == 0) { at_wifi_puts(buf); return true; }
  return at_wifi_puts_wait(buf, mode == 1 ? 300 : 15);
}

static bool send_station(int mode) {
  char buf[96];
  int st = req_station;
  snprintf(buf, sizeof(buf), "%d,%s", st, st >= 0 ? stations[st].name : "-");
  return send_line("P", buf, mode);
}

static bool send_wifi(int mode) {
  char buf[64];
  if(network_ready())
    snprintf(buf, sizeof(buf), "verbunden %s", ip4addr_ntoa(netif_ip4_addr(netif_default)));
  else
    snprintf(buf, sizeof(buf), "getrennt");
  return send_line("W", buf, mode);
}

// called from the AT command parser: the text after "atr"
void radio_at_command(const char *c) {
  char buf[96];
  switch(c[0] | 0x20) {                   // case insensitive
    case 'n': {                           // atrn<n>: play station n
      int n = atoi(c + 1);
      if(n >= 0 && n < NUM_STATIONS) { radio_play(n); at_wifi_puts("OK\r\n"); }
      else at_wifi_puts("ERROR\r\n");
      break; }
    case 'm': mono = (c[1] == '1'); at_wifi_puts("OK\r\n"); break;   // atrm1 mono / atrm0 stereo
    case 'x': radio_play(-1); at_wifi_puts("OK\r\n"); break;                  // atrx: stop
    case 'v': radio_set_volume(atoi(c + 1)); at_wifi_puts("OK\r\n"); break;   // atrv<0-100>
    case 'r':                             // atrr: re-read the station file
      if(load_station_file() < 0) at_wifi_puts("ERROR\r\n");
      else at_wifi_puts("OK\r\n");
      break;
    case 'l':                             // atrl: (re)read the station file and send the list
      load_station_file();                // on failure the current list stays
      for(int i=0; i<NUM_STATIONS; i++) {
        snprintf(buf, sizeof(buf), "%d,%s", i, stations[i].name);
        send_line("S", buf, 0);
      }
      break;
    case '?':                             // atr?: current state
      send_station(0);
      if(title[0]) send_line("T", title, 0);
      send_wifi(0);
      break;
    case 'a':                             // atra1 / atra0: unsolicited messages on/off
      subscribed = (c[1] != '0');
      at_wifi_puts("OK\r\n");
      break;
    default: at_wifi_puts("ERROR\r\n");
  }
}

// peak value (0..32767) -> 0..100 on a 40 dB scale (0 = -40 dBFS or less, 100 = full scale)
static int level_db(int peak) {
  if(peak < 328) return 0;                     // below -40 dBFS
  float db = 20.0f * log10f((float)peak / 32768.0f);
  int v = (int)((db + 40.0f) * 2.5f + 0.5f);
  return v < 0 ? 0 : (v > 100 ? 100 : v);
}

// sends unsolicited messages (station, title, levels, errors) while the Amiga program is subscribed
static void radio_ctl_task(__attribute__((unused)) void *parms) {
  uint32_t sent_station = 0, sent_title = 0, sent_err = 0;
  bool was_sub = false, wifi_pending = false;
  TickType_t next = xTaskGetTickCount();
  TickType_t last_m = next;
  for(;;) {
    vTaskDelayUntil(&next, pdMS_TO_TICKS(50));       // fixed 20 Hz grid
    if(!subscribed) { was_sub = false; peak_l = peak_r = 0; continue; }
    if(!was_sub) {                         // freshly subscribed: send everything once
      was_sub = true;
      sent_station = sent_title = 0;
      sent_err = err_ver;
      wifi_pending = true;
      at_wifi_puts_wait("\r\n", 100);   // finish any half line left from earlier
    }
    // important messages are repeated every 100 ms until the core had room for them
    if(wifi_pending && send_wifi(1)) wifi_pending = false;
    if(sent_station != station_ver) {
      uint32_t v = station_ver;
      if(send_station(1)) { sent_station = v; sent_title = 0; }   // station changed: title follows
    }
    if(sent_station == station_ver && sent_title != title_ver) {
      uint32_t v = title_ver;
      if(!title[0] || send_line("T", title, 1)) sent_title = v;
    }
    if(sent_err != err_ver) {
      uint32_t v = err_ver;
      if(send_line("E", err_msg, 1)) sent_err = v;
    }
    int l = level_db(peak_l), r = level_db(peak_r);
    peak_l = peak_r = 0;
    if(mono) { int m = l > r ? l : r; l = r = m; }
    char m[16];
    snprintf(m, sizeof(m), "%d,%d", l, r);
    if(send_line("M", m, 2)) m_ok++; else m_fail++;
    {
      TickType_t now = xTaskGetTickCount();
      uint32_t gap = (uint32_t)(now - last_m) * 1000 / configTICK_RATE_HZ;
      if(gap > m_gap_max) m_gap_max = gap;
      last_m = now;
    }
  }
}

/* ------------------------------------------------------------------ public interface */
void radio_init(void) {
  load_defaults();
  xTaskCreate(radio_net_task, "radio net", 2048, NULL, 3, NULL);
  xTaskCreate(radio_dec_task, "radio dec", 6144, NULL, 3, NULL);
  xTaskCreate(radio_status_task, "radio dbg", 1024, NULL, 2, NULL);
  xTaskCreate(radio_ctl_task, "radio ctl", 1536, NULL, 2, NULL);
}

void radio_play(int station) {
  if(station >= NUM_STATIONS) return;
  req_station = station;
  req_gen++;
  station_ver++;
  if(station < 0) { title[0] = 0; title_ver++; }
}

int radio_station_count(void) { return NUM_STATIONS; }
const char *radio_station_name(int idx) { return (idx >= 0 && idx < NUM_STATIONS) ? stations[idx].name : ""; }
int radio_current(void) { return req_station; }
const char *radio_title(void) { return title; }
void radio_set_volume(int percent) {
  if(percent < 0) percent = 0;
  if(percent > 100) percent = 100;
  gain = percent * 256 / 100;
}

#endif // RADIO_ENABLE
