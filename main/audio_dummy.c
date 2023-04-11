/*
 * dummy output driver. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include "audio.h"
#include "driver/i2s.h"
#include "driver/gpio.h"

#define I2S_NUM     (0)

#define WROVER_KIT

#ifdef WROVER_KIT
#define I2S_WS      (GPIO_NUM_27)
#define I2S_BCK     (GPIO_NUM_26)
#define I2S_MCK     (I2S_PIN_NO_CHANGE)
#define I2S_DO      (GPIO_NUM_25)
#else
#define I2S_WS      (GPIO_NUM_21)
#define I2S_BCK     (GPIO_NUM_23)
#define I2S_MCK     (GPIO_NUM_0)
#define I2S_DO      (GPIO_NUM_19)
#define MUTE        (GPIO_NUM_18)
#endif

#define TASK_STACK  (2048)
#define BUF_CNT    (8)
#define BUF_LEN     (355)

int Fs;
long long starttime, samples_played;

static int init(int argc, char **argv) {
    i2s_config_t i2s_config = {
            .mode = I2S_MODE_MASTER | I2S_MODE_TX,
            .sample_rate = 44100,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .dma_buf_count = BUF_CNT,
            .dma_buf_len = BUF_LEN,
            .use_apll = true,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2
    };

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);

    i2s_pin_config_t pin_config = {
            .mck_io_num = I2S_MCK,
            .bck_io_num = I2S_BCK,
            .ws_io_num = I2S_WS,
            .data_out_num = I2S_DO,
            .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_set_pin(I2S_NUM, &pin_config);

#ifndef WROVER_KIT
    gpio_set_direction(MUTE, GPIO_MODE_OUTPUT);
    gpio_set_level(MUTE, 0);
#endif

    return 0;
}

static void deinit(void) {
}

static void start(int sample_rate) {
#ifndef WROVER_KIT
    gpio_set_level(MUTE, 1);
#endif
    Fs = sample_rate;
    starttime = 0;
    samples_played = 0;

    printf("dummy audio output started at Fs=%d Hz\n", sample_rate);
}

static void play(short buf[], int samples) {
//    printf("frame size: %d\n", samples);
    //size_t bytes_written;
    //i2s_write(I2S_NUM, buf, 4*samples, &bytes_written, 10);
    samples_played += samples;
    //vTaskDelay(8/portTICK_RATE_MS);
}

static void stop(void) {
#ifndef WROVER_KIT
    gpio_set_level(MUTE, 0);
#endif
    printf("dummy audio stopped\n");
}

static void help(void) {
    printf("    There are no options for dummy audio.\n");
}

audio_output audio_dummy = {
    .name = "dummy",
    .help = &help,
    .init = &init,
    .deinit = &deinit,
    .start = &start,
    .stop = &stop,
    .play = &play,
    .volume = NULL
};
