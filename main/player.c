/*
 * Slave-clocked ALAC stream player. This file is part of Shairport.
 * Copyright (c) James Laird 2011, 2013
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
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "mbedtls/aes.h"
#include <math.h>
#include <assert.h>

#include "common.h"
#include "player.h"
#include "rtp.h"

#ifdef FANCY_RESAMPLING
#include <samplerate.h>
#endif

#include "alac.h"

#include "esp_log.h"
static const char* TAG = "Player";

// parameters from the source
static unsigned char *aesiv;
static mbedtls_aes_context aes;
static int sampling_rate, frame_size;

#define FRAME_BYTES(frame_size) (4*frame_size)
// maximal resampling shift - conservative
#define OUTFRAME_BYTES(frame_size) (4*(frame_size+3))

static TaskHandle_t player_thread;
extern TaskHandle_t* listen_thread;

static alac_file *decoder_info;

#ifdef FANCY_RESAMPLING
static int fancy_resampling = 1;
static SRC_STATE *src;
#endif


// interthread variables
static double volume = 1.0;
static int fix_volume = 0x10000;
static SemaphoreHandle_t vol_mutex;

// default buffer size
// needs to be a power of 2 because of the way BUFIDX(seqno) works
#define BUFFER_FRAMES  512
#define MAX_PACKET      2048

typedef struct audio_buffer_entry {   // decoded audio packets
    int ready;
    signed short *data;
} abuf_t;
static abuf_t audio_buffer[BUFFER_FRAMES];
#define BUFIDX(seqno) ((seq_t)(seqno) % BUFFER_FRAMES)

// mutex-protected variables
static seq_t ab_read, ab_write;
static int ab_buffering = 1, ab_synced = 0;
static SemaphoreHandle_t ab_mutex;

static void bf_est_reset(short fill);

static void ab_resync(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        audio_buffer[i].ready = 0;
    ab_synced = 0;
    ab_buffering = 1;
}

// the sequence numbers will wrap pretty often.
// this returns true if the second arg is after the first
static inline int seq_order(seq_t a, seq_t b) {
    signed short d = b - a;
    return d > 0;
}

static void alac_decode(short *dest, uint8_t *buf, int len) {
    unsigned char packet[MAX_PACKET];
    assert(len<=MAX_PACKET);

    unsigned char iv[16];
    int aeslen = len & ~0xf;
    memcpy(iv, aesiv, sizeof(iv));
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, len, iv, buf, packet);
    memcpy(packet+aeslen, buf+aeslen, len-aeslen);

    int outsize;

    alac_decode_frame(decoder_info, packet, dest, &outsize);

    assert(outsize == FRAME_BYTES(frame_size));
}


static int init_decoder(int32_t fmtp[12]) {
    alac_file *alac;

    frame_size = fmtp[1]; // stereo samples
    sampling_rate = fmtp[11];

    int sample_size = fmtp[3];
    if (sample_size != 16)
        ESP_LOGE(TAG, "only 16-bit samples supported!");

    alac = alac_create(sample_size, 2);
    if (!alac)
        return 1;
    decoder_info = alac;

    alac->setinfo_max_samples_per_frame = frame_size;
    alac->setinfo_7a =      fmtp[2];
    alac->setinfo_sample_size = sample_size;
    alac->setinfo_rice_historymult = fmtp[4];
    alac->setinfo_rice_initialhistory = fmtp[5];
    alac->setinfo_rice_kmodifier = fmtp[6];
    alac->setinfo_7f =      fmtp[7];
    alac->setinfo_80 =      fmtp[8];
    alac->setinfo_82 =      fmtp[9];
    alac->setinfo_86 =      fmtp[10];
    alac->setinfo_8a_rate = fmtp[11];
    alac_allocate_buffers(alac);

    ab_mutex = xSemaphoreCreateBinary();
    xSemaphoreGive(ab_mutex);

    vol_mutex = xSemaphoreCreateBinary();
    xSemaphoreGive(vol_mutex);

    return 0;
}

static void free_decoder(void) {
    alac_free(decoder_info);
}

#ifdef FANCY_RESAMPLING
static int init_src(void) {
    int err;
    if (fancy_resampling)
        src = src_new(SRC_SINC_MEDIUM_QUALITY, 2, &err);
    else
        src = NULL;

    return err;
}
static void free_src(void) {
    src_delete(src);
    src = NULL;
}
#endif

static void init_buffer(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        audio_buffer[i].data = malloc(OUTFRAME_BYTES(frame_size));
    ab_resync();
}

static void free_buffer(void) {
    int i;
    for (i=0; i<BUFFER_FRAMES; i++)
        free(audio_buffer[i].data);
}

void player_put_packet(seq_t seqno, uint8_t *data, int len) {
    abuf_t *abuf = NULL;
    int16_t buf_fill = 0;

    if (xSemaphoreTake(ab_mutex, (TickType_t) 10)) { // RTP thread
        if (!ab_synced) {
            ESP_LOGV(TAG, "syncing to first seqno %04X\n", seqno);
            ab_write = seqno - 1;
            ab_read = seqno;
            ab_synced = 1;
        }
        if (seq_diff(ab_write, seqno) == 1) {                  // expected packet
            abuf = audio_buffer + BUFIDX(seqno);
            ab_write = seqno;
        } else if (seq_order(ab_write, seqno)) {    // newer than expected
            rtp_request_resend(ab_write + 1, seqno - 1);
            abuf = audio_buffer + BUFIDX(seqno);
            ab_write = seqno;
        } else if (seq_order(ab_read, seqno)) {     // late but not yet played
            abuf = audio_buffer + BUFIDX(seqno);
        } else {    // too late.
            ESP_LOGV(TAG, "late packet %04X (%04X:%04X)", seqno, ab_read, ab_write);
        }
        buf_fill = seq_diff(ab_read, ab_write);
        xSemaphoreGive(ab_mutex); // RTP thread
    } else {
        return;
    }

    if (abuf) {
        //alac_decode(abuf->data, data, len);
        memcpy(abuf->data, data, len);
        abuf->ready = 1;
    }
    
    if (xSemaphoreTake(ab_mutex, (TickType_t) 10)) { // RTP thread
        if (ab_buffering && buf_fill >= config.buffer_start_fill) {
            ESP_LOGD(TAG, "buffering over. starting play\n");
            ab_buffering = 0;
            bf_est_reset(buf_fill);
        }
        xSemaphoreGive(ab_mutex); // RTP thread
    }
}


static short lcg_rand(void) {
	static unsigned long lcg_prev = 12345;
	lcg_prev = lcg_prev * 69069 + 3;
	return lcg_prev & 0xffff;
}

static inline short dithered_vol(short sample) {
    static short rand_a, rand_b;
    long out;

    out = (long)sample * fix_volume;
    if (fix_volume < 0x10000) {
        rand_b = rand_a;
        rand_a = lcg_rand();
        out += rand_a;
        out -= rand_b;
    }
    return out>>16;
}

typedef struct {
    double hist[2];
    double a[2];
    double b[3];
} biquad_t;

static void biquad_init(biquad_t *bq, double a[], double b[]) {
    bq->hist[0] = bq->hist[1] = 0.0;
    memcpy(bq->a, a, 2*sizeof(double));
    memcpy(bq->b, b, 3*sizeof(double));
}

static void biquad_lpf(biquad_t *bq, double freq, double Q) {
    double w0 = 2.0 * M_PI * freq * frame_size / (double)sampling_rate;
    double alpha = sin(w0)/(2.0*Q);

    double a_0 = 1.0 + alpha;
    double b[3], a[2];
    b[0] = (1.0-cos(w0))/(2.0*a_0);
    b[1] = (1.0-cos(w0))/a_0;
    b[2] = b[0];
    a[0] = -2.0*cos(w0)/a_0;
    a[1] = (1-alpha)/a_0;

    biquad_init(bq, a, b);
}

static double biquad_filt(biquad_t *bq, double in) {
    double w = in - bq->a[0]*bq->hist[0] - bq->a[1]*bq->hist[1];
    double out = bq->b[1]*bq->hist[0] + bq->b[2]*bq->hist[1] + bq->b[0]*w;
    bq->hist[1] = bq->hist[0];
    bq->hist[0] = w;

    return out;
}

static double bf_playback_rate = 1.0;

static double bf_est_drift = 0.0;   // local clock is slower by
static biquad_t bf_drift_lpf;
static double bf_est_err = 0.0, bf_last_err;
static biquad_t bf_err_lpf, bf_err_deriv_lpf;
static double desired_fill;
static int fill_count;

static void bf_est_reset(short fill) {
    biquad_lpf(&bf_drift_lpf, 1.0/180.0, 0.3);
    biquad_lpf(&bf_err_lpf, 1.0/10.0, 0.25);
    biquad_lpf(&bf_err_deriv_lpf, 1.0/2.0, 0.2);
    fill_count = 0;
    bf_playback_rate = 1.0;
    bf_est_err = bf_last_err = 0;
    desired_fill = fill_count = 0;
}

static void bf_est_update(short fill) {
    // the rate-matching system needs to decide how full to keep the buffer.
    // the initial fill is present when the system starts to output samples,
    // but most output chains will instantly gobble their own buffer's worth of
    // data. we average for a while to decide where to draw the line.
    if (fill_count < 1000) {
        desired_fill += (double)fill/1000.0;
        fill_count++;
        return;
    } else if (fill_count == 1000) {
        // this information could be used to help estimate our effective latency?
        ESP_LOGD(TAG, "established desired fill of %f frames, "
              "so output chain buffered about %f frames\n", desired_fill,
              config.buffer_start_fill - desired_fill);
        fill_count++;
    }

#define CONTROL_A   (1e-4)
#define CONTROL_B   (1e-1)

    double buf_delta = fill - desired_fill;
    bf_est_err = biquad_filt(&bf_err_lpf, buf_delta);
    double err_deriv = biquad_filt(&bf_err_deriv_lpf, bf_est_err - bf_last_err);
    double adj_error = CONTROL_A * bf_est_err;

    bf_est_drift = biquad_filt(&bf_drift_lpf, CONTROL_B*(adj_error + err_deriv) + bf_est_drift);

    ESP_LOGV(TAG, "bf %d err %f drift %f desiring %f ed %f estd %f\n",
          fill, bf_est_err, bf_est_drift, desired_fill, err_deriv, err_deriv + adj_error);
    bf_playback_rate = 1.0 + adj_error + bf_est_drift;

    bf_last_err = bf_est_err;
}

// get the next frame, when available. return 0 if underrun/stream reset.
static short *buffer_get_frame(void) {
    int16_t buf_fill;
    seq_t read, next;
    abuf_t *abuf = 0;
    int i;

    if (ab_buffering)
        return 0;

    abuf_t *curframe = NULL;
    if (xSemaphoreTake(ab_mutex, (TickType_t) 10)) { // Player Thread

        buf_fill = seq_diff(ab_read, ab_write);
        if (buf_fill < 1 || !ab_synced) {
            if (buf_fill < 1)
                ESP_LOGW(TAG, "underrun.");
            ab_buffering = 1;
            xSemaphoreGive(ab_mutex); // Player thread
            return 0;
        }
        if (buf_fill >= BUFFER_FRAMES) {   // overrunning! uh-oh. restart at a sane distance
            ESP_LOGW(TAG, "overrun.");
            ab_read = ab_write - config.buffer_start_fill;
        }
        read = ab_read;
        ab_read++;
        buf_fill = seq_diff(ab_read, ab_write);
        bf_est_update(buf_fill);

        // check if t+16, t+32, t+64, t+128, ... (buffer_start_fill / 2)
        // packets have arrived... last-chance resend
        if (!ab_buffering) {
            for (i = 16; i < (config.buffer_start_fill / 2); i = (i * 2)) {
                next = ab_read + i;
                abuf = audio_buffer + BUFIDX(next);
                if (!abuf->ready) {
                    rtp_request_resend(next, next);
                }
            }
        }

        curframe = audio_buffer + BUFIDX(read);
        if (!curframe->ready) {
            ESP_LOGV(TAG, "missing frame %04X.", read);
            memset(curframe->data, 0, FRAME_BYTES(frame_size));
        }
        curframe->ready = 0;
        xSemaphoreGive(ab_mutex); // Player Thread
    }else {
        printf("Skip\n");
    }
    return curframe->data;
}

static int stuff_buffer(double playback_rate, short *inptr, short *outptr) {
    int i;
    int stuffsamp = frame_size;
    int stuff = 0;
    double p_stuff;

    p_stuff = 1.0 - pow(1.0 - fabs(playback_rate-1.0), frame_size);

    if (rand() < p_stuff * RAND_MAX) {
        stuff = playback_rate > 1.0 ? -1 : 1;
        stuffsamp = rand() % (frame_size - 1);
    }

    if (xSemaphoreTake(vol_mutex, (TickType_t) 10)) {
        for (i = 0; i < stuffsamp; i++) {   // the whole frame, if no stuffing
            *outptr++ = dithered_vol(*inptr++);
            *outptr++ = dithered_vol(*inptr++);
        };
        if (stuff) {
            if (stuff == 1) {
                ESP_LOGD(TAG, "+++++++++\n");
                // interpolate one sample
                *outptr++ = dithered_vol(((long) inptr[-2] + (long) inptr[0]) >> 1);
                *outptr++ = dithered_vol(((long) inptr[-1] + (long) inptr[1]) >> 1);
            } else if (stuff == -1) {
                ESP_LOGD(TAG, "---------\n");
                inptr++;
                inptr++;
            }
            for (i = stuffsamp; i < frame_size + stuff; i++) {
                *outptr++ = dithered_vol(*inptr++);
                *outptr++ = dithered_vol(*inptr++);
            }
        }
        xSemaphoreGive(vol_mutex);
    }

    return frame_size + stuff;
}

static void player_thread_func(void *arg) {
    ESP_LOGD(TAG, "Starting player thread");
    int play_samples;

    signed short *inbuf, *outbuf, *silence;
    outbuf = malloc(OUTFRAME_BYTES(frame_size));
    silence = malloc(OUTFRAME_BYTES(frame_size));
    memset(silence, 0, OUTFRAME_BYTES(frame_size));

#ifdef FANCY_RESAMPLING
    float *frame, *outframe;
    SRC_DATA srcdat;
    if (fancy_resampling) {
        frame = malloc(frame_size*2*sizeof(float));
        outframe = malloc(2*frame_size*2*sizeof(float));

        srcdat.data_in = frame;
        srcdat.data_out = outframe;
        srcdat.input_frames = FRAME_BYTES(frame_size);
        srcdat.output_frames = 2*FRAME_BYTES(frame_size);
        srcdat.src_ratio = 1.0;
        srcdat.end_of_input = 0;
    }
#endif

    while (1) {
        if (ulTaskNotifyTake(pdTRUE, 0))
            break;

        inbuf = buffer_get_frame();
        if (!inbuf)
            inbuf = silence;

#ifdef FANCY_RESAMPLING
        if (fancy_resampling) {
            int i;
            xSemaphoreTake(vol_mutex);
            for (i=0; i<2*FRAME_BYTES(frame_size); i++) {
                frame[i] = (float)inbuf[i] / 32768.0;
                frame[i] *= volume;
            }
            xSemaphoreGive(vol_mutex);
            srcdat.src_ratio = bf_playback_rate;
            src_process(src, &srcdat);
            assert(srcdat.input_frames_used == FRAME_BYTES(frame_size));
            src_float_to_short_array(outframe, outbuf, FRAME_BYTES(frame_size)*2);
            play_samples = srcdat.output_frames_gen;
        } else
#endif
        //play_samples = stuff_buffer(bf_playback_rate, inbuf, outbuf);
        play_samples = frame_size;
        config.output->play(outbuf, play_samples);
    }
    player_thread = NULL;
    xTaskNotifyGive(*listen_thread);
    vTaskDelete(NULL);
}

// takes the volume as specified by the airplay protocol
void player_volume(double f) {
    double linear_volume = pow(10.0, 0.05*f);

    if (config.output->volume) {
        config.output->volume(linear_volume);
    } else {
        if(xSemaphoreTake(vol_mutex, (TickType_t) 10)) {
            volume = linear_volume;
            fix_volume = 65536.0 * volume;
            xSemaphoreGive(vol_mutex);
        }
    }
}
void player_flush(void) {
    if(xSemaphoreTake(ab_mutex, (TickType_t) 10)) { // RTSP thread
        ab_resync();
        xSemaphoreGive(ab_mutex); // RTSP thread
    }
}

int player_play(stream_cfg *stream) {
    if (player_thread != NULL) {
        xTaskNotifyGive(player_thread);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        assert(player_thread == NULL);
    }
    if (config.buffer_start_fill > BUFFER_FRAMES)
        ESP_LOGE(TAG, "specified buffer starting fill %d > buffer size %d",
            config.buffer_start_fill, BUFFER_FRAMES);

    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, stream->aeskey, 128);
    aesiv = stream->aesiv;
    init_decoder(stream->fmtp);
    // must be after decoder init
    init_buffer();
#ifdef FANCY_RESAMPLING
    init_src();
#endif

    config.output->start(sampling_rate);
    BaseType_t ret = xTaskCreate(player_thread_func, "Player Thread", 4096, NULL, 3, &player_thread);
    assert(ret == pdPASS);
    return 0;
}

void player_stop(void) {
    if (player_thread != NULL && *listen_thread == xTaskGetCurrentTaskHandle()) {
        xTaskNotifyGive(player_thread);
        xTaskNotifyWait(0x01, 0x01, NULL, portMAX_DELAY);
        config.output->stop();
        free_buffer();
        free_decoder();
    }
}
