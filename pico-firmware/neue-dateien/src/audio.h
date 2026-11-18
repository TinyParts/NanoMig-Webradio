/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 TinyParts
 * Teil des Projekts NanoMig-Webradio, Erweiterung des FPGA-Companion (Apache-2.0).
 */

/*
  audio.h - audio output via the FPGA core (internet radio support)
*/

#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

#ifdef AUDIO_TEST
// development test: plays a short 440 Hz tone every 20 seconds as long as the core
// accepts audio samples (SPI command 11)
void audio_test_init(void);
#endif

#endif // AUDIO_H
