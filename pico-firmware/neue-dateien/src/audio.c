/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 TinyParts
 * Teil des Projekts NanoMig-Webradio, Erweiterung des FPGA-Companion (Apache-2.0).
 */

/*
  audio.c - audio output via the FPGA core (internet radio support)

  The core has a 2048 entry fifo that is played at 48 kHz. Samples are pushed via SPI
  command 11 (see sysctrl.c). Free space is polled; since the core plays at a fixed
  rate, the MCU simply keeps the fifo topped up.
*/

#include <math.h>
#include "FreeRTOS.h"
#include "task.h"

#include "audio.h"
#include "sysctrl.h"
#include "debug.h"

#ifdef AUDIO_TEST

#define TONE_FREQ_HZ   440
#define TONE_SECONDS   3
#define TONE_PAUSE_S   17
#define TONE_AMPLITUDE 6000
#define BLOCK_FRAMES   256

static void audio_test_task(__attribute__((unused)) void *parms) {
  static int16_t table[256];
  static int16_t block[2 * BLOCK_FRAMES];
  uint32_t phase = 0;
  const uint32_t inc = (uint32_t)((double)TONE_FREQ_HZ / 48000.0 * 4294967296.0);

  for(int i=0;i<256;i++)
    table[i] = (int16_t)(TONE_AMPLITUDE * sinf(2.0f * 3.14159265f * i / 256.0f));

  for(;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    // only run when the FPGA is there and its core knows the audio command
    // (a core without it returns 0 free entries)
    if(!sys_status_is_valid()) continue;
    uint16_t space = sys_audio_space();
    if(space < 1000) continue;

    debugf("audio test: playing tone");
    uint32_t sent = 0;
    while(sent < 48000UL * TONE_SECONDS) {
      space = sys_audio_space();
      if(space >= BLOCK_FRAMES) {
        for(int i=0;i<BLOCK_FRAMES;i++) {
          int16_t v = table[phase >> 24];
          phase += inc;
          block[2*i] = v;          // left
          block[2*i+1] = v;        // right
        }
        sys_audio_write(block, BLOCK_FRAMES);
        sent += BLOCK_FRAMES;
      } else
        vTaskDelay(pdMS_TO_TICKS(3));
    }
    debugf("audio test: done");
    vTaskDelay(pdMS_TO_TICKS(TONE_PAUSE_S * 1000));
  }
}

void audio_test_init(void) {
  xTaskCreate(audio_test_task, "audio test", 2048, NULL, 2, NULL);
}

#endif // AUDIO_TEST
