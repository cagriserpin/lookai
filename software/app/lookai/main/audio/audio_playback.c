/**
 * @file audio/audio_playback.c
 * @brief Speaker playback helpers for STT audio tests and saved recordings.
 */

#include "audio_playback.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "audio_playback";

#define AUDIO_PLAYBACK_TEST_SAMPLE_RATE 16000
#define AUDIO_PLAYBACK_TEST_CHANNELS 1
#define AUDIO_PLAYBACK_TEST_BITS_PER_SAMPLE 16

#define AUDIO_PLAYBACK_PA_CTRL_GPIO GPIO_NUM_46
#define AUDIO_PLAYBACK_DEFAULT_VOLUME 80

#define AUDIO_PLAYBACK_TASK_STACK_SIZE 4096
#define AUDIO_PLAYBACK_TASK_PRIORITY 5
#define AUDIO_PLAYBACK_BUFFER_SIZE 1024
#define AUDIO_PLAYBACK_WAV_HEADER_SIZE 44
#define AUDIO_PLAYBACK_STREAM_BUFFER_SIZE (12 * 1024)
#define AUDIO_PLAYBACK_STREAM_TRIGGER_LEVEL 1
#define AUDIO_PLAYBACK_STREAM_PREBUFFER_BYTES 4096
#define AUDIO_PLAYBACK_STREAM_TASK_STACK_SIZE 5120
#define AUDIO_PLAYBACK_STREAM_WAIT_FOREVER_MS UINT32_MAX
#define AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT 2
#define AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE (12 * 1024)
#define AUDIO_PLAYBACK_PCM_TASK_STACK_SIZE 4096

#define SINE_440_LUT_COUNT 36
#define SINE_440_CHUNK_REPEATS 20
#define SINE_440_CHUNK_SAMPLES (SINE_440_LUT_COUNT * SINE_440_CHUNK_REPEATS)

typedef struct {
    uint32_t sample_rate;
    uint32_t pcm_bytes;
    uint32_t data_offset;
    uint16_t channels;
    uint16_t bits_per_sample;
} wav_file_info_t;

static const int16_t s_sine_440_lut[SINE_440_LUT_COUNT] = {
     0,  1423,  2802,  4096,  5266,  6275,  7094,
  7698,  8068,  8192,  8068,  7698,  7094,  6275,
  5266,  4096,  2802,  1423,     0, -1423, -2802,
 -4096, -5266, -6275, -7094, -7698, -8068, -8192,
 -8068, -7698, -7094, -6275, -5266, -4096, -2802,
 -1423
};

static esp_codec_dev_handle_t s_speaker_dev = NULL;
static bool s_ready = false;
static bool s_speaker_open = false;
static uint8_t s_volume_percent = AUDIO_PLAYBACK_DEFAULT_VOLUME;

static volatile bool s_should_play_test = false;
static volatile bool s_wav_playing = false;
static TaskHandle_t s_test_task_handle = NULL;
static TaskHandle_t s_stop_waiter_handle = NULL;
static struct audio_playback_pcm_stream *s_active_pcm_stream = NULL;
static volatile bool s_stop_current_requested = false;


struct audio_playback_stream {
    StreamBufferHandle_t stream_buffer;
    StaticStreamBuffer_t static_stream_buffer;
    uint8_t *storage;
    TaskHandle_t task_handle;
    SemaphoreHandle_t done_sem;
    audio_playback_stream_callback_t callback;
    void *user_ctx;
    volatile bool producer_finished;
    esp_err_t producer_result;
    esp_err_t playback_result;
    audio_playback_result_t result;
    uint32_t container_bytes;
};

struct audio_playback_pcm_stream {
    uint8_t *storage;
    size_t block_len[AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT];
    int write_index;
    int read_index;
    int current_write_slot;
    size_t current_write_offset;
    TaskHandle_t task_handle;
    SemaphoreHandle_t free_sem;
    SemaphoreHandle_t ready_sem;
    SemaphoreHandle_t done_sem;
    audio_playback_stream_callback_t callback;
    void *user_ctx;
    volatile bool producer_finished;
    volatile bool abort_requested;
    esp_err_t producer_result;
    esp_err_t playback_result;
    audio_playback_result_t result;
    audio_playback_stream_metrics_t metrics;
    volatile uint32_t ready_blocks;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
};


static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t read_le32(const uint8_t *src)
{
    return (uint32_t)src[0] |
        ((uint32_t)src[1] << 8) |
        ((uint32_t)src[2] << 16) |
        ((uint32_t)src[3] << 24);
}

static uint32_t bytes_to_duration_ms(
    uint32_t pcm_bytes,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample
)
{
    uint32_t bytes_per_second =
        sample_rate * (uint32_t)channels * (uint32_t)bits_per_sample / 8U;

    if (bytes_per_second == 0) {
        return 0;
    }

    return (uint32_t)(((uint64_t)pcm_bytes * 1000ULL) / bytes_per_second);
}

static uint8_t clamp_volume_percent(uint8_t volume_percent)
{
    return volume_percent > 100 ? 100 : volume_percent;
}

esp_err_t audio_playback_set_volume(uint8_t volume_percent)
{
    s_volume_percent = clamp_volume_percent(volume_percent);

    if (s_speaker_dev == NULL || !s_speaker_open) {
        return ESP_OK;
    }

    int codec_ret = esp_codec_dev_set_out_vol(s_speaker_dev, s_volume_percent);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to set speaker volume to %u%%: %d",
                 (unsigned int)s_volume_percent, codec_ret);
        return ESP_FAIL;
    }

    return ESP_OK;
}

uint8_t audio_playback_get_volume(void)
{
    return s_volume_percent;
}

static esp_err_t set_power_amplifier_enabled(bool enabled)
{
    static bool configured = false;

    if (!configured) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << AUDIO_PLAYBACK_PA_CTRL_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };

        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure PA_CTRL GPIO: %s", esp_err_to_name(err));
            return err;
        }

        configured = true;
    }

    esp_err_t err = gpio_set_level(AUDIO_PLAYBACK_PA_CTRL_GPIO, enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PA_CTRL GPIO: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Power amplifier %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

static void build_sine_chunk(int16_t *buffer, size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        buffer[i] = s_sine_440_lut[i % SINE_440_LUT_COUNT];
    }
}

static esp_err_t open_speaker_codec(
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample
)
{
    esp_err_t err = set_power_amplifier_enabled(true);
    if (err != ESP_OK) {
        return err;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channels,
        .bits_per_sample = bits_per_sample,
    };

    int codec_ret = esp_codec_dev_open(s_speaker_dev, &fs);
    if (codec_ret != 0) {
        ESP_LOGE(TAG, "Failed to open speaker codec: %d", codec_ret);
        set_power_amplifier_enabled(false);
        return ESP_FAIL;
    }

    s_speaker_open = true;

    codec_ret = esp_codec_dev_set_out_vol(s_speaker_dev, s_volume_percent);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to set speaker volume: %d", codec_ret);
    }

    return ESP_OK;
}

static void close_speaker_codec(void)
{
    s_speaker_open = false;
    int codec_ret = esp_codec_dev_close(s_speaker_dev);
    if (codec_ret != 0) {
        ESP_LOGW(TAG, "Failed to close speaker codec cleanly: %d", codec_ret);
    }

    set_power_amplifier_enabled(false);
}

static esp_err_t read_wav_info(FILE *file, wav_file_info_t *out_info)
{
    if (file == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t riff_header[12];

    if (fseek(file, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }

    if (fread(riff_header, 1, sizeof(riff_header), file) != sizeof(riff_header)) {
        ESP_LOGE(TAG, "WAV header is too small");
        return ESP_ERR_INVALID_SIZE;
    }

    if (
        memcmp(&riff_header[0], "RIFF", 4) != 0 ||
        memcmp(&riff_header[8], "WAVE", 4) != 0
    ) {
        ESP_LOGE(TAG, "Unsupported WAV container");
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool fmt_found = false;
    bool data_found = false;

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;
    uint32_t pcm_bytes = 0;
    uint32_t data_offset = 0;

    while (!fmt_found || !data_found) {
        uint8_t chunk_header[8];

        if (fread(chunk_header, 1, sizeof(chunk_header), file) != sizeof(chunk_header)) {
            break;
        }

        uint32_t chunk_size = read_le32(&chunk_header[4]);
        long chunk_data_pos = ftell(file);
        if (chunk_data_pos < 0) {
            return ESP_FAIL;
        }

        uint32_t padded_chunk_size = chunk_size + (chunk_size & 1U);

        if (memcmp(&chunk_header[0], "fmt ", 4) == 0) {
            uint8_t fmt[40] = {0};
            size_t to_read = chunk_size < sizeof(fmt) ? (size_t)chunk_size : sizeof(fmt);

            if (chunk_size < 16U) {
                ESP_LOGE(TAG, "Invalid WAV fmt chunk size: %lu", (unsigned long)chunk_size);
                return ESP_ERR_INVALID_RESPONSE;
            }

            if (fread(fmt, 1, to_read, file) != to_read) {
                ESP_LOGE(TAG, "Could not read WAV fmt chunk");
                return ESP_FAIL;
            }

            audio_format = read_le16(&fmt[0]);
            channels = read_le16(&fmt[2]);
            sample_rate = read_le32(&fmt[4]);
            bits_per_sample = read_le16(&fmt[14]);

            /*
             * Groq and other cloud TTS providers may return WAV files with a
             * non-44-byte RIFF layout or WAVE_FORMAT_EXTENSIBLE. Accept normal
             * PCM and extensible PCM, then locate the real data chunk below.
             */
            bool extensible_pcm =
                audio_format == 0xFFFE &&
                to_read >= 40 &&
                read_le16(&fmt[24]) == 1;

            if (audio_format != 1 && !extensible_pcm) {
                ESP_LOGE(TAG, "Unsupported WAV format: %u", (unsigned int)audio_format);
                return ESP_ERR_NOT_SUPPORTED;
            }

            fmt_found = true;

            long consumed = (long)to_read;
            long remaining = (long)padded_chunk_size - consumed;
            if (remaining > 0 && fseek(file, remaining, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
        } else if (memcmp(&chunk_header[0], "data", 4) == 0) {
            data_found = true;
            data_offset = (uint32_t)chunk_data_pos;
            pcm_bytes = chunk_size;

            if (fmt_found) {
                break;
            }

            if (fseek(file, (long)padded_chunk_size, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
        } else {
            if (fseek(file, (long)padded_chunk_size, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
        }
    }

    if (!fmt_found) {
        ESP_LOGE(TAG, "WAV fmt chunk not found");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (!data_found || pcm_bytes == 0) {
        ESP_LOGE(TAG, "WAV data chunk not found");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (bits_per_sample != 16) {
        ESP_LOGE(TAG, "Unsupported bit depth: %u", (unsigned int)bits_per_sample);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (channels != 1 && channels != 2) {
        ESP_LOGE(TAG, "Unsupported channel count: %u", (unsigned int)channels);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sample_rate == 0) {
        ESP_LOGE(TAG, "Invalid WAV sample rate");
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->sample_rate = sample_rate;
    out_info->channels = channels;
    out_info->bits_per_sample = bits_per_sample;
    out_info->pcm_bytes = pcm_bytes;
    out_info->data_offset = data_offset;

    ESP_LOGI(
        TAG,
        "WAV info: %lu Hz, %u ch, %u bit, data_offset=%lu, data=%lu bytes",
        (unsigned long)sample_rate,
        (unsigned int)channels,
        (unsigned int)bits_per_sample,
        (unsigned long)data_offset,
        (unsigned long)pcm_bytes
    );

    return ESP_OK;
}


static TickType_t timeout_ms_to_ticks(uint32_t timeout_ms)
{
    return timeout_ms == AUDIO_PLAYBACK_STREAM_WAIT_FOREVER_MS ?
        portMAX_DELAY :
        pdMS_TO_TICKS(timeout_ms);
}

static esp_err_t stream_receive_exact(
    audio_playback_stream_t *stream,
    uint8_t *buffer,
    size_t len
)
{
    if (stream == NULL || buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t total = 0;

    while (total < len) {
        size_t received = xStreamBufferReceive(
            stream->stream_buffer,
            buffer + total,
            len - total,
            pdMS_TO_TICKS(100)
        );

        if (received > 0) {
            total += received;
            continue;
        }

        if (stream->producer_finished) {
            return stream->producer_result == ESP_OK ?
                ESP_ERR_INVALID_SIZE :
                stream->producer_result;
        }
    }

    return ESP_OK;
}

static esp_err_t stream_discard_bytes(audio_playback_stream_t *stream, uint32_t len)
{
    uint8_t discard[128];
    uint32_t remaining = len;

    while (remaining > 0) {
        size_t to_read = remaining > sizeof(discard) ? sizeof(discard) : (size_t)remaining;
        esp_err_t err = stream_receive_exact(stream, discard, to_read);
        if (err != ESP_OK) {
            return err;
        }
        remaining -= (uint32_t)to_read;
    }

    return ESP_OK;
}

static esp_err_t read_wav_info_from_stream(
    audio_playback_stream_t *stream,
    wav_file_info_t *out_info
)
{
    if (stream == NULL || out_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t riff_header[12];
    esp_err_t err = stream_receive_exact(stream, riff_header, sizeof(riff_header));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not read streaming WAV RIFF header");
        return err;
    }

    if (
        memcmp(&riff_header[0], "RIFF", 4) != 0 ||
        memcmp(&riff_header[8], "WAVE", 4) != 0
    ) {
        ESP_LOGE(TAG, "Unsupported streaming WAV container");
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool fmt_found = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;
    uint32_t pcm_bytes = 0;

    while (true) {
        uint8_t chunk_header[8];
        err = stream_receive_exact(stream, chunk_header, sizeof(chunk_header));
        if (err != ESP_OK) {
            return err;
        }

        uint32_t chunk_size = read_le32(&chunk_header[4]);
        uint32_t padded_chunk_size = chunk_size + (chunk_size & 1U);

        if (memcmp(&chunk_header[0], "fmt ", 4) == 0) {
            uint8_t fmt[40] = {0};
            size_t to_read = chunk_size < sizeof(fmt) ? (size_t)chunk_size : sizeof(fmt);

            if (chunk_size < 16U) {
                ESP_LOGE(TAG, "Invalid streaming WAV fmt chunk size: %lu", (unsigned long)chunk_size);
                return ESP_ERR_INVALID_RESPONSE;
            }

            err = stream_receive_exact(stream, fmt, to_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Could not read streaming WAV fmt chunk");
                return err;
            }

            audio_format = read_le16(&fmt[0]);
            channels = read_le16(&fmt[2]);
            sample_rate = read_le32(&fmt[4]);
            bits_per_sample = read_le16(&fmt[14]);

            bool extensible_pcm =
                audio_format == 0xFFFE &&
                to_read >= 40 &&
                read_le16(&fmt[24]) == 1;

            if (audio_format != 1 && !extensible_pcm) {
                ESP_LOGE(TAG, "Unsupported streaming WAV format: %u", (unsigned int)audio_format);
                return ESP_ERR_NOT_SUPPORTED;
            }

            fmt_found = true;

            if (padded_chunk_size > to_read) {
                err = stream_discard_bytes(stream, padded_chunk_size - (uint32_t)to_read);
                if (err != ESP_OK) {
                    return err;
                }
            }
        } else if (memcmp(&chunk_header[0], "data", 4) == 0) {
            if (!fmt_found) {
                ESP_LOGE(TAG, "Streaming WAV data chunk arrived before fmt chunk");
                return ESP_ERR_INVALID_RESPONSE;
            }

            pcm_bytes = chunk_size;
            break;
        } else {
            err = stream_discard_bytes(stream, padded_chunk_size);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    if (bits_per_sample != 16) {
        ESP_LOGE(TAG, "Unsupported streaming WAV bit depth: %u", (unsigned int)bits_per_sample);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (channels != 1 && channels != 2) {
        ESP_LOGE(TAG, "Unsupported streaming WAV channel count: %u", (unsigned int)channels);
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (sample_rate == 0) {
        ESP_LOGE(TAG, "Invalid streaming WAV sample rate");
        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->sample_rate = sample_rate;
    out_info->channels = channels;
    out_info->bits_per_sample = bits_per_sample;
    out_info->pcm_bytes = pcm_bytes;
    out_info->data_offset = 0;

    ESP_LOGI(
        TAG,
        "Streaming WAV info: %lu Hz, %u ch, %u bit, data=%lu bytes",
        (unsigned long)sample_rate,
        (unsigned int)channels,
        (unsigned int)bits_per_sample,
        (unsigned long)pcm_bytes
    );

    return ESP_OK;
}

static void stream_wait_for_prebuffer(audio_playback_stream_t *stream)
{
    while (!stream->producer_finished) {
        if (xStreamBufferBytesAvailable(stream->stream_buffer) >= AUDIO_PLAYBACK_STREAM_PREBUFFER_BYTES) {
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static size_t stream_receive_pcm(
    audio_playback_stream_t *stream,
    uint8_t *buffer,
    size_t max_len
)
{
    while (true) {
        size_t received = xStreamBufferReceive(
            stream->stream_buffer,
            buffer,
            max_len,
            pdMS_TO_TICKS(100)
        );

        if (received > 0) {
            return received;
        }

        if (stream->producer_finished) {
            return 0;
        }
    }
}

static void stream_playback_task(void *arg)
{
    audio_playback_stream_t *stream = (audio_playback_stream_t *)arg;
    wav_file_info_t wav = {0};
    bool codec_open = false;
    uint8_t *buffer = NULL;
    uint32_t pcm_played = 0;
    esp_err_t result = ESP_OK;

    if (stream == NULL) {
        vTaskDelete(NULL);
        return;
    }

    result = read_wav_info_from_stream(stream, &wav);
    if (result != ESP_OK) {
        goto done;
    }

    stream_wait_for_prebuffer(stream);

    result = open_speaker_codec(wav.sample_rate, wav.channels, wav.bits_per_sample);
    if (result != ESP_OK) {
        goto done;
    }
    codec_open = true;

    if (stream->callback != NULL) {
        stream->callback(AUDIO_PLAYBACK_STREAM_EVENT_STARTED, NULL, stream->user_ctx);
    }

    ESP_LOGI(TAG, "Playing streaming WAV audio");

    buffer = (uint8_t *)malloc(AUDIO_PLAYBACK_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate streaming playback buffer");
        result = ESP_ERR_NO_MEM;
        goto done;
    }

    bool open_ended = wav.pcm_bytes == UINT32_MAX;
    uint32_t remaining = wav.pcm_bytes;

    while (open_ended || remaining > 0) {
        size_t want = AUDIO_PLAYBACK_BUFFER_SIZE;
        if (!open_ended && remaining < want) {
            want = (size_t)remaining;
        }

        size_t received = stream_receive_pcm(stream, buffer, want);
        if (received == 0) {
            if (stream->producer_result != ESP_OK) {
                result = stream->producer_result;
            }
            break;
        }

        int codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, received);
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Streaming speaker write failed: %d", codec_ret);
            result = ESP_FAIL;
            break;
        }

        pcm_played += (uint32_t)received;
        if (!open_ended) {
            remaining -= (uint32_t)received;
        }
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Streaming WAV playback done, pcm=%lu bytes", (unsigned long)pcm_played);
    }

done:
    if (buffer != NULL) {
        free(buffer);
    }

    if (codec_open) {
        close_speaker_codec();
    } else {
        set_power_amplifier_enabled(false);
    }

    memset(&stream->result, 0, sizeof(stream->result));
    strlcpy(stream->result.path, "stream", sizeof(stream->result.path));
    stream->result.duration_ms = bytes_to_duration_ms(
        pcm_played,
        wav.sample_rate,
        wav.channels,
        wav.bits_per_sample
    );
    stream->result.pcm_bytes = pcm_played;
    stream->result.wav_bytes = stream->container_bytes;
    stream->result.sample_rate = wav.sample_rate;
    stream->result.channels = wav.channels;
    stream->result.bits_per_sample = wav.bits_per_sample;
    stream->playback_result = result;

    s_wav_playing = false;
    if (s_stop_current_requested) {
        s_stop_current_requested = false;
    }

    if (stream->callback != NULL) {
        stream->callback(AUDIO_PLAYBACK_STREAM_EVENT_DONE, &stream->result, stream->user_ctx);
    }

    xSemaphoreGive(stream->done_sem);
    vTaskDelete(NULL);
}

esp_err_t audio_playback_stream_wav_start(
    audio_playback_stream_t **out_stream,
    audio_playback_stream_callback_t callback,
    void *user_ctx
)
{
    if (out_stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_stream = NULL;

    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    audio_playback_stream_t *stream = (audio_playback_stream_t *)calloc(1, sizeof(*stream));
    if (stream == NULL) {
        return ESP_ERR_NO_MEM;
    }

    stream->storage = (uint8_t *)heap_caps_malloc(
        AUDIO_PLAYBACK_STREAM_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (stream->storage == NULL) {
        ESP_LOGE(TAG, "Could not allocate stream buffer in PSRAM");
    }

    if (stream->storage == NULL) {
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    stream->stream_buffer = xStreamBufferCreateStatic(
        AUDIO_PLAYBACK_STREAM_BUFFER_SIZE,
        AUDIO_PLAYBACK_STREAM_TRIGGER_LEVEL,
        stream->storage,
        &stream->static_stream_buffer
    );
    if (stream->stream_buffer == NULL) {
        free(stream->storage);
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    stream->done_sem = xSemaphoreCreateBinary();
    if (stream->done_sem == NULL) {
        vStreamBufferDelete(stream->stream_buffer);
        free(stream->storage);
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    stream->callback = callback;
    stream->user_ctx = user_ctx;
    stream->producer_result = ESP_OK;
    stream->playback_result = ESP_ERR_INVALID_STATE;
    s_wav_playing = true;

    BaseType_t ok = xTaskCreate(
        stream_playback_task,
        "wav_stream_play",
        AUDIO_PLAYBACK_STREAM_TASK_STACK_SIZE,
        stream,
        AUDIO_PLAYBACK_TASK_PRIORITY,
        &stream->task_handle
    );

    if (ok != pdPASS) {
        s_wav_playing = false;
        vSemaphoreDelete(stream->done_sem);
        vStreamBufferDelete(stream->stream_buffer);
        free(stream->storage);
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    *out_stream = stream;
    return ESP_OK;
}

esp_err_t audio_playback_stream_wav_write(
    audio_playback_stream_t *stream,
    const void *data,
    size_t len,
    uint32_t timeout_ms
)
{
    if (stream == NULL || (data == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *cursor = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        size_t sent = xStreamBufferSend(
            stream->stream_buffer,
            cursor,
            remaining,
            timeout_ms_to_ticks(timeout_ms)
        );

        if (sent == 0) {
            return stream->playback_result != ESP_ERR_INVALID_STATE ?
                stream->playback_result :
                ESP_ERR_TIMEOUT;
        }

        cursor += sent;
        remaining -= sent;
        stream->container_bytes += (uint32_t)sent;
    }

    return ESP_OK;
}

void audio_playback_stream_wav_finish(
    audio_playback_stream_t *stream,
    esp_err_t producer_result
)
{
    if (stream == NULL) {
        return;
    }

    stream->producer_result = producer_result;
    stream->producer_finished = true;
}

esp_err_t audio_playback_stream_wav_wait(
    audio_playback_stream_t *stream,
    uint32_t timeout_ms,
    audio_playback_result_t *out_result
)
{
    if (stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t notified = xSemaphoreTake(stream->done_sem, timeout_ms_to_ticks(timeout_ms));
    if (notified != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (out_result != NULL) {
        *out_result = stream->result;
    }

    esp_err_t result = stream->playback_result;

    vSemaphoreDelete(stream->done_sem);
    vStreamBufferDelete(stream->stream_buffer);
    free(stream->storage);
    free(stream);

    return result;
}




static void pcm_stream_update_fill_metrics(audio_playback_pcm_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    uint32_t ready_blocks = stream->ready_blocks;
    uint32_t fill_bytes = ready_blocks * AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE;
    if (stream->current_write_slot >= 0) {
        fill_bytes += (uint32_t)stream->current_write_offset;
    }

    if (fill_bytes > stream->metrics.max_fill_bytes) {
        stream->metrics.max_fill_bytes = fill_bytes;
    }

    if (ready_blocks > stream->metrics.max_ready_blocks) {
        stream->metrics.max_ready_blocks = ready_blocks;
    }
}

static uint8_t *pcm_stream_block_ptr(audio_playback_pcm_stream_t *stream, int slot)
{
    return stream->storage + ((size_t)slot * AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE);
}

static esp_err_t pcm_stream_publish_current_block(audio_playback_pcm_stream_t *stream)
{
    if (stream == NULL || stream->current_write_slot < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int slot = stream->current_write_slot;
    if (stream->current_write_offset == 0) {
        stream->current_write_slot = -1;
        xSemaphoreGive(stream->free_sem);
        return ESP_OK;
    }

    stream->block_len[slot] = stream->current_write_offset;
    stream->current_write_slot = -1;
    stream->current_write_offset = 0;
    stream->ready_blocks++;
    pcm_stream_update_fill_metrics(stream);
    xSemaphoreGive(stream->ready_sem);

    return ESP_OK;
}

static esp_err_t pcm_stream_acquire_write_block(audio_playback_pcm_stream_t *stream, uint32_t timeout_ms)
{
    if (stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (stream->current_write_slot >= 0) {
        return ESP_OK;
    }

    if (xSemaphoreTake(stream->free_sem, 0) != pdTRUE) {
        stream->metrics.ring_full_wait_count++;
        if (xSemaphoreTake(stream->free_sem, timeout_ms_to_ticks(timeout_ms)) != pdTRUE) {
            return stream->playback_result != ESP_ERR_INVALID_STATE ?
                stream->playback_result :
                ESP_ERR_TIMEOUT;
        }
    }

    int slot = stream->write_index % AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT;
    stream->write_index++;
    stream->current_write_slot = slot;
    stream->current_write_offset = 0;
    stream->block_len[slot] = 0;

    return ESP_OK;
}

static void pcm_stream_playback_task(void *arg)
{
    audio_playback_pcm_stream_t *stream = (audio_playback_pcm_stream_t *)arg;
    bool codec_open = false;
    bool playback_started = false;
    uint32_t pcm_played = 0;
    esp_err_t result = ESP_OK;

    if (stream == NULL) {
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        if (stream->abort_requested || s_stop_current_requested) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }

        if (xSemaphoreTake(stream->ready_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (stream->producer_finished) {
                break;
            }

            if (playback_started) {
                stream->metrics.ring_empty_wait_count++;
                stream->metrics.underrun_count++;
            }
            continue;
        }

        if (stream->ready_blocks == 0) {
            if (stream->producer_finished) {
                break;
            }
            continue;
        }

        if (stream->abort_requested || s_stop_current_requested) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }

        int slot = stream->read_index % AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT;
        stream->read_index++;
        size_t len = stream->block_len[slot];
        stream->block_len[slot] = 0;
        if (stream->ready_blocks > 0) {
            stream->ready_blocks--;
        }

        if (len == 0) {
            xSemaphoreGive(stream->free_sem);
            continue;
        }

        if (!codec_open) {
            result = open_speaker_codec(
                stream->sample_rate,
                stream->channels,
                stream->bits_per_sample
            );
            if (result != ESP_OK) {
                xSemaphoreGive(stream->free_sem);
                break;
            }
            codec_open = true;
            playback_started = true;

            if (stream->callback != NULL) {
                stream->callback(AUDIO_PLAYBACK_STREAM_EVENT_STARTED, NULL, stream->user_ctx);
            }

            ESP_LOGI(
                TAG,
                "Playing direct PCM stream: %lu Hz, %u ch, %u bit",
                (unsigned long)stream->sample_rate,
                (unsigned int)stream->channels,
                (unsigned int)stream->bits_per_sample
            );
        }

        int codec_ret = esp_codec_dev_write(s_speaker_dev, pcm_stream_block_ptr(stream, slot), len);
        xSemaphoreGive(stream->free_sem);

        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Direct PCM stream speaker write failed: %d", codec_ret);
            result = ESP_FAIL;
            break;
        }

        pcm_played += (uint32_t)len;
        stream->metrics.consumer_bytes = pcm_played;
        pcm_stream_update_fill_metrics(stream);
    }

    if (result == ESP_OK && stream->producer_result != ESP_OK) {
        result = stream->producer_result;
    }

    if (result == ESP_OK) {
        ESP_LOGI(TAG, "Direct PCM streaming playback done, pcm=%lu bytes", (unsigned long)pcm_played);
    }

    if (codec_open) {
        close_speaker_codec();
    } else {
        set_power_amplifier_enabled(false);
    }

    memset(&stream->result, 0, sizeof(stream->result));
    strlcpy(stream->result.path, "pcm_stream", sizeof(stream->result.path));
    stream->result.duration_ms = bytes_to_duration_ms(
        pcm_played,
        stream->sample_rate,
        stream->channels,
        stream->bits_per_sample
    );
    stream->result.pcm_bytes = pcm_played;
    stream->result.wav_bytes = 0;
    stream->result.sample_rate = stream->sample_rate;
    stream->result.channels = stream->channels;
    stream->result.bits_per_sample = stream->bits_per_sample;
    stream->playback_result = result;

    s_wav_playing = false;
    if (s_active_pcm_stream == stream) {
        s_active_pcm_stream = NULL;
    }
    if (stream->abort_requested || s_stop_current_requested) {
        s_stop_current_requested = false;
    }

    if (stream->callback != NULL) {
        stream->callback(AUDIO_PLAYBACK_STREAM_EVENT_DONE, &stream->result, stream->user_ctx);
    }

    xSemaphoreGive(stream->done_sem);
    vTaskDelete(NULL);
}

esp_err_t audio_playback_stream_pcm_start(
    audio_playback_pcm_stream_t **out_stream,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    audio_playback_stream_callback_t callback,
    void *user_ctx
)
{
    if (out_stream == NULL || sample_rate == 0 || bits_per_sample != 16 || (channels != 1 && channels != 2)) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_stream = NULL;

    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    audio_playback_pcm_stream_t *stream = (audio_playback_pcm_stream_t *)calloc(1, sizeof(*stream));
    if (stream == NULL) {
        return ESP_ERR_NO_MEM;
    }

    stream->storage = (uint8_t *)heap_caps_malloc(
        AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT * AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (stream->storage == NULL) {
        ESP_LOGE(TAG, "Could not allocate PCM ring in PSRAM");
    }

    if (stream->storage == NULL) {
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    stream->free_sem = xSemaphoreCreateCounting(AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT, AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT);
    stream->ready_sem = xSemaphoreCreateCounting(AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT + 1, 0);
    stream->done_sem = xSemaphoreCreateBinary();

    if (stream->free_sem == NULL || stream->ready_sem == NULL || stream->done_sem == NULL) {
        if (stream->free_sem != NULL) {
            vSemaphoreDelete(stream->free_sem);
        }
        if (stream->ready_sem != NULL) {
            vSemaphoreDelete(stream->ready_sem);
        }
        if (stream->done_sem != NULL) {
            vSemaphoreDelete(stream->done_sem);
        }
        free(stream->storage);
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    stream->current_write_slot = -1;
    stream->producer_result = ESP_OK;
    stream->playback_result = ESP_ERR_INVALID_STATE;
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->bits_per_sample = bits_per_sample;
    stream->callback = callback;
    stream->user_ctx = user_ctx;
    stream->metrics.block_count = AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT;
    stream->metrics.block_size = AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE;
    s_wav_playing = true;
    s_stop_current_requested = false;
    s_active_pcm_stream = stream;

    BaseType_t ok = xTaskCreate(
        pcm_stream_playback_task,
        "pcm_stream_play",
        AUDIO_PLAYBACK_PCM_TASK_STACK_SIZE,
        stream,
        AUDIO_PLAYBACK_TASK_PRIORITY,
        &stream->task_handle
    );

    if (ok != pdPASS) {
        s_wav_playing = false;
        if (s_active_pcm_stream == stream) {
            s_active_pcm_stream = NULL;
        }
        vSemaphoreDelete(stream->done_sem);
        vSemaphoreDelete(stream->ready_sem);
        vSemaphoreDelete(stream->free_sem);
        free(stream->storage);
        free(stream);
        return ESP_ERR_NO_MEM;
    }

    *out_stream = stream;
    return ESP_OK;
}

esp_err_t audio_playback_stream_pcm_write(
    audio_playback_pcm_stream_t *stream,
    const void *data,
    size_t len,
    uint32_t timeout_ms
)
{
    if (stream == NULL || (data == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *cursor = (const uint8_t *)data;
    size_t remaining = len;

    while (remaining > 0) {
        if (stream->abort_requested || s_stop_current_requested) {
            return ESP_ERR_INVALID_STATE;
        }

        if (stream->playback_result != ESP_ERR_INVALID_STATE) {
            return stream->playback_result;
        }

        esp_err_t err = pcm_stream_acquire_write_block(stream, timeout_ms);
        if (err != ESP_OK) {
            return err;
        }

        size_t space = AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE - stream->current_write_offset;
        size_t to_copy = remaining < space ? remaining : space;
        memcpy(
            pcm_stream_block_ptr(stream, stream->current_write_slot) + stream->current_write_offset,
            cursor,
            to_copy
        );

        stream->current_write_offset += to_copy;
        stream->metrics.producer_bytes += (uint32_t)to_copy;
        cursor += to_copy;
        remaining -= to_copy;
        pcm_stream_update_fill_metrics(stream);

        if (stream->current_write_offset >= AUDIO_PLAYBACK_PCM_RING_BLOCK_SIZE) {
            err = pcm_stream_publish_current_block(stream);
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    return ESP_OK;
}

void audio_playback_stream_pcm_finish(
    audio_playback_pcm_stream_t *stream,
    esp_err_t producer_result
)
{
    if (stream == NULL) {
        return;
    }

    stream->producer_result = producer_result;

    if (stream->current_write_slot >= 0 && stream->current_write_offset > 0) {
        pcm_stream_publish_current_block(stream);
    } else if (stream->current_write_slot >= 0) {
        xSemaphoreGive(stream->free_sem);
        stream->current_write_slot = -1;
        stream->current_write_offset = 0;
    }

    stream->producer_finished = true;
    xSemaphoreGive(stream->ready_sem);
}

esp_err_t audio_playback_stream_pcm_wait(
    audio_playback_pcm_stream_t *stream,
    uint32_t timeout_ms,
    audio_playback_result_t *out_result,
    audio_playback_stream_metrics_t *out_metrics
)
{
    if (stream == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t notified = xSemaphoreTake(stream->done_sem, timeout_ms_to_ticks(timeout_ms));
    if (notified != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (out_result != NULL) {
        *out_result = stream->result;
    }

    if (out_metrics != NULL) {
        *out_metrics = stream->metrics;
    }

    esp_err_t result = stream->playback_result;

    vSemaphoreDelete(stream->done_sem);
    vSemaphoreDelete(stream->ready_sem);
    vSemaphoreDelete(stream->free_sem);
    free(stream->storage);
    free(stream);

    return result;
}



bool audio_playback_is_test_tone_playing(void)
{
    return s_test_task_handle != NULL;
}

esp_err_t audio_playback_stop_current(void)
{
    if (s_test_task_handle != NULL) {
        return audio_playback_stop_sine_440();
    }

    audio_playback_pcm_stream_t *stream = s_active_pcm_stream;
    if (stream != NULL) {
        s_stop_current_requested = true;
        stream->abort_requested = true;
        stream->producer_result = ESP_ERR_INVALID_STATE;
        stream->producer_finished = true;
        for (int i = 0; i < AUDIO_PLAYBACK_PCM_RING_BLOCK_COUNT + 1; i++) {
            xSemaphoreGive(stream->ready_sem);
            xSemaphoreGive(stream->free_sem);
        }
        return ESP_OK;
    }

    if (s_wav_playing) {
        s_stop_current_requested = true;
        return ESP_OK;
    }

    return ESP_OK;
}

bool audio_playback_is_busy(void)
{
    return s_test_task_handle != NULL || s_wav_playing;
}

esp_err_t audio_playback_init(void)
{
    if (s_ready && s_speaker_dev != NULL) {
        return ESP_OK;
    }

    /*
     * audio_recorder_init() already calls bsp_audio_init() for the shared I2S
     * bus during app startup. Here we only create the speaker codec device.
     */
    s_speaker_dev = bsp_audio_codec_speaker_init();
    if (s_speaker_dev == NULL) {
        ESP_LOGE(TAG, "Failed to initialize speaker codec");
        return ESP_FAIL;
    }

    esp_err_t err = set_power_amplifier_enabled(false);
    if (err != ESP_OK) {
        return err;
    }

    s_ready = true;
    return ESP_OK;
}

static void sine_task(void *arg)
{
    (void)arg;

    int16_t buffer[SINE_440_CHUNK_SAMPLES];
    build_sine_chunk(buffer, SINE_440_CHUNK_SAMPLES);

    esp_err_t err = open_speaker_codec(
        AUDIO_PLAYBACK_TEST_SAMPLE_RATE,
        AUDIO_PLAYBACK_TEST_CHANNELS,
        AUDIO_PLAYBACK_TEST_BITS_PER_SAMPLE
    );

    if (err != ESP_OK) {
        s_should_play_test = false;
    } else {
        ESP_LOGI(TAG, "Started 440 Hz sine playback test");
    }

    while (s_should_play_test) {
        int codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, sizeof(buffer));
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Speaker write failed: %d", codec_ret);
            break;
        }
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Stopped sine playback test");
        close_speaker_codec();
    }

    TaskHandle_t waiter = s_stop_waiter_handle;
    s_stop_waiter_handle = NULL;
    s_should_play_test = false;
    s_test_task_handle = NULL;

    if (waiter != NULL) {
        xTaskNotifyGive(waiter);
    }

    vTaskDelete(NULL);
}

esp_err_t audio_playback_start_sine_440(void)
{
    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    s_should_play_test = true;
    s_stop_waiter_handle = NULL;

    BaseType_t ok = xTaskCreate(
        sine_task,
        "sine_play",
        AUDIO_PLAYBACK_TASK_STACK_SIZE,
        NULL,
        AUDIO_PLAYBACK_TASK_PRIORITY,
        &s_test_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sine playback task");
        s_should_play_test = false;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t audio_playback_stop_sine_440(void)
{
    if (s_test_task_handle == NULL) {
        return ESP_OK;
    }

    s_stop_waiter_handle = xTaskGetCurrentTaskHandle();
    s_should_play_test = false;

    uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1500));
    if (notified == 0) {
        ESP_LOGE(TAG, "Timed out while stopping sine playback");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}


esp_err_t audio_playback_play_pcm_file(
    const char *path,
    uint32_t sample_rate,
    uint16_t channels,
    uint16_t bits_per_sample,
    audio_playback_result_t *out_result
)
{
    if (path == NULL || sample_rate == 0 || channels == 0 || bits_per_sample == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for PCM playback", path);
        return ESP_ERR_NOT_FOUND;
    }

    struct stat st = {0};
    uint32_t pcm_bytes = 0;
    if (stat(path, &st) == 0 && st.st_size > 0) {
        pcm_bytes = (uint32_t)st.st_size;
    }

    err = open_speaker_codec(sample_rate, channels, bits_per_sample);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    ESP_LOGI(TAG, "Playing PCM file: %s", path);
    s_wav_playing = true;

    uint8_t *buffer = (uint8_t *)malloc(AUDIO_PLAYBACK_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate PCM playback buffer");
        close_speaker_codec();
        s_wav_playing = false;
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    uint32_t played_bytes = 0;
    esp_err_t play_result = ESP_OK;

    while (true) {
        size_t bytes_read = fread(buffer, 1, AUDIO_PLAYBACK_BUFFER_SIZE, file);

        if (bytes_read == 0) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "Failed while reading PCM data");
                play_result = ESP_FAIL;
            }
            break;
        }

        int codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, bytes_read);
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Speaker write failed: %d", codec_ret);
            play_result = ESP_FAIL;
            break;
        }

        played_bytes += (uint32_t)bytes_read;
    }

    free(buffer);
    close_speaker_codec();
    s_wav_playing = false;
    fclose(file);

    if (out_result != NULL) {
        memset(out_result, 0, sizeof(*out_result));
        strlcpy(out_result->path, path, sizeof(out_result->path));
        out_result->duration_ms = bytes_to_duration_ms(
            played_bytes,
            sample_rate,
            channels,
            bits_per_sample
        );
        out_result->pcm_bytes = played_bytes;
        out_result->wav_bytes = pcm_bytes;
        out_result->sample_rate = sample_rate;
        out_result->channels = channels;
        out_result->bits_per_sample = bits_per_sample;
    }

    if (play_result == ESP_OK) {
        ESP_LOGI(TAG, "PCM playback done: %s", path);
    }

    return play_result;
}

esp_err_t audio_playback_play_wav_file(
    const char *path,
    audio_playback_result_t *out_result
)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (audio_playback_is_busy()) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = audio_playback_init();
    if (err != ESP_OK) {
        return err;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open %s for playback", path);
        return ESP_ERR_NOT_FOUND;
    }

    wav_file_info_t wav = {0};
    err = read_wav_info(file, &wav);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    struct stat st = {0};
    uint32_t wav_bytes = wav.pcm_bytes + wav.data_offset;
    if (stat(path, &st) == 0 && st.st_size > 0) {
        wav_bytes = (uint32_t)st.st_size;

        if ((uint32_t)st.st_size > wav.data_offset) {
            uint32_t available_pcm_bytes = (uint32_t)st.st_size - wav.data_offset;
            if (wav.pcm_bytes > available_pcm_bytes) {
                ESP_LOGW(
                    TAG,
                    "WAV data size looks open-ended (%lu), clamping to file payload: %lu bytes",
                    (unsigned long)wav.pcm_bytes,
                    (unsigned long)available_pcm_bytes
                );
                wav.pcm_bytes = available_pcm_bytes;
            }
        }
    }

    if (fseek(file, (long)wav.data_offset, SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    err = open_speaker_codec(wav.sample_rate, wav.channels, wav.bits_per_sample);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    ESP_LOGI(TAG, "Playing WAV file: %s", path);

    s_wav_playing = true;

    uint8_t *buffer = (uint8_t *)malloc(AUDIO_PLAYBACK_BUFFER_SIZE);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate WAV playback buffer");
        close_speaker_codec();
        s_wav_playing = false;
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    uint32_t remaining = wav.pcm_bytes;
    esp_err_t play_result = ESP_OK;

    while (remaining > 0) {
        size_t to_read = remaining > AUDIO_PLAYBACK_BUFFER_SIZE ?
            AUDIO_PLAYBACK_BUFFER_SIZE :
            (size_t)remaining;
        size_t bytes_read = fread(buffer, 1, to_read, file);

        if (bytes_read == 0) {
            if (ferror(file)) {
                ESP_LOGE(TAG, "Failed while reading WAV data");
                play_result = ESP_FAIL;
            }
            break;
        }

        int codec_ret = esp_codec_dev_write(s_speaker_dev, buffer, bytes_read);
        if (codec_ret != 0) {
            ESP_LOGE(TAG, "Speaker write failed: %d", codec_ret);
            play_result = ESP_FAIL;
            break;
        }

        remaining -= (uint32_t)bytes_read;
    }

    free(buffer);

    close_speaker_codec();
    s_wav_playing = false;

    if (out_result != NULL) {
        memset(out_result, 0, sizeof(*out_result));
        strlcpy(out_result->path, path, sizeof(out_result->path));
        out_result->duration_ms = bytes_to_duration_ms(
            wav.pcm_bytes,
            wav.sample_rate,
            wav.channels,
            wav.bits_per_sample
        );
        out_result->pcm_bytes = wav.pcm_bytes;
        out_result->wav_bytes = wav_bytes;
        out_result->sample_rate = wav.sample_rate;
        out_result->channels = wav.channels;
        out_result->bits_per_sample = wav.bits_per_sample;
    }

    fclose(file);

    if (play_result == ESP_OK) {
        ESP_LOGI(TAG, "WAV playback done: %s", path);
    }

    return play_result;
}
