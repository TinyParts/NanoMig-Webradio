/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 TinyParts
 * Teil des Projekts NanoMig-Webradio, Erweiterung des FPGA-Companion (Apache-2.0).
 */

/*
  radio.h - internet radio inside the FPGA companion

  Fetches an HTTP stream (ICY/shoutcast style, MP3), decodes it with minimp3, resamples to
  48 kHz and pushes the samples into the audio fifo of the core (SPI command 11).
*/

#ifndef RADIO_H
#define RADIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef RADIO_ENABLE

void radio_init(void);                  // creates the radio tasks
void radio_play(int station);           // start station (index) or stop with -1
int  radio_station_count(void);
const char *radio_station_name(int idx);
int  radio_current(void);               // station that is playing or being connected, -1 if off
const char *radio_title(void);          // last stream title (may be empty)
void radio_set_volume(int percent);     // 0..100
void radio_at_command(const char *cmd); // modem command atr... (text after atr)

#endif

#endif // RADIO_H
