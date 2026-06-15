/**
 * @file system/app_settings.c
 * @brief Runtime user-adjustable LookAI API settings implementation.
 */

#include "app_settings.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "app_settings";

#define APP_SETTINGS_NAMESPACE "lookai_cfg"
#define APP_SETTINGS_KEY_BLOB  "settings"
#define APP_SETTINGS_VERSION   5

typedef struct {
    uint32_t version;
    uint8_t stt_language;
    uint8_t stt_model;
    uint8_t ai_style;
    uint8_t ai_temperature;
    uint8_t tts_voice;
    uint16_t tts_speed_percent;
} app_settings_nvs_blob_t;

typedef struct {
    const char *label;
    const char *value;
} option_pair_t;

static const option_pair_t STT_LANGUAGES[] = {
    {"Auto", ""},
    {"Turkish", "tr"},
    {"English", "en"},
    {"German", "de"},
    {"French", "fr"},
    {"Spanish", "es"},
    {"Italian", "it"},
    {"Portuguese", "pt"},
    {"Russian", "ru"},
    {"Arabic", "ar"},
    {"Chinese", "zh"},
    {"Japanese", "ja"},
    {"Korean", "ko"},
};

static const option_pair_t STT_MODELS[] = {
    {"GPT-4o mini", "gpt-4o-mini-transcribe"},
    {"GPT-4o", "gpt-4o-transcribe"},
    {"GPT-4o diarize", "gpt-4o-transcribe-diarize"},
    {"Whisper", "whisper-1"},
};

static const char AI_STYLE_OPTIONS[] = "Short\nBalanced\nDetailed";
static const char AI_TEMPERATURE_OPTIONS[] = "Precise\nNormal\nCreative";

static const option_pair_t TTS_VOICES[] = {
    {"Alloy", "alloy"},
    {"Ash", "ash"},
    {"Ballad", "ballad"},
    {"Coral", "coral"},
    {"Echo", "echo"},
    {"Fable", "fable"},
    {"Onyx", "onyx"},
    {"Nova", "nova"},
    {"Sage", "sage"},
    {"Shimmer", "shimmer"},
    {"Verse", "verse"},
    {"Marin", "marin"},
    {"Cedar", "cedar"},
};

static const char STT_LANGUAGE_OPTIONS[] =
    "Auto\nTurkish\nEnglish\nGerman\nFrench\nSpanish\nItalian\nPortuguese\nRussian\nArabic\nChinese\nJapanese\nKorean";
static const char STT_MODEL_OPTIONS[] = "GPT-4o mini\nGPT-4o\nGPT-4o diarize\nWhisper";
static const char TTS_VOICE_OPTIONS[] =
    "Alloy\nAsh\nBallad\nCoral\nEcho\nFable\nOnyx\nNova\nSage\nShimmer\nVerse\nMarin\nCedar";
static const char TTS_SPEED_OPTIONS[] = "0.50x\n0.75x\n1.00x\n1.25x\n1.50x\n1.75x\n2.00x";

static lookai_runtime_settings_t s_settings = {
    .stt_language = LOOKAI_STT_LANGUAGE_AUTO,
    .stt_model = LOOKAI_STT_MODEL_GPT_4O_MINI,
    .ai_style = LOOKAI_AI_STYLE_SHORT,
    .ai_temperature = LOOKAI_AI_TEMPERATURE_NORMAL,
    .tts_voice = LOOKAI_TTS_VOICE_MARIN,
    .tts_speed_percent = 100,
};

static bool is_initialized = false;

static int cycle_index(int value, int count, int step)
{
    if (count <= 0) {
        return 0;
    }

    value += step;
    while (value < 0) {
        value += count;
    }
    while (value >= count) {
        value -= count;
    }
    return value;
}

static int clamp_index(int value, int count, int fallback)
{
    if (value < 0 || value >= count) {
        return fallback;
    }
    return value;
}

static void clamp_settings(void)
{
    s_settings.stt_language = (lookai_stt_language_t)clamp_index(s_settings.stt_language, LOOKAI_STT_LANGUAGE_COUNT, LOOKAI_STT_LANGUAGE_AUTO);
    s_settings.stt_model = (lookai_stt_model_t)clamp_index(s_settings.stt_model, LOOKAI_STT_MODEL_COUNT, LOOKAI_STT_MODEL_GPT_4O_MINI);
    s_settings.ai_style = (lookai_ai_style_t)clamp_index(s_settings.ai_style, LOOKAI_AI_STYLE_COUNT, LOOKAI_AI_STYLE_SHORT);
    s_settings.ai_temperature = (lookai_ai_temperature_t)clamp_index(s_settings.ai_temperature, LOOKAI_AI_TEMPERATURE_COUNT, LOOKAI_AI_TEMPERATURE_NORMAL);
    s_settings.tts_voice = (lookai_tts_voice_t)clamp_index(s_settings.tts_voice, LOOKAI_TTS_VOICE_COUNT, LOOKAI_TTS_VOICE_MARIN);
    if (s_settings.tts_speed_percent < 50 || s_settings.tts_speed_percent > 200) {
        s_settings.tts_speed_percent = 100;
    }
}

static esp_err_t save_settings(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not open settings NVS: %s", esp_err_to_name(err));
        return err;
    }

    app_settings_nvs_blob_t blob = {
        .version = APP_SETTINGS_VERSION,
        .stt_language = (uint8_t)s_settings.stt_language,
        .stt_model = (uint8_t)s_settings.stt_model,
        .ai_style = (uint8_t)s_settings.ai_style,
        .ai_temperature = (uint8_t)s_settings.ai_temperature,
        .tts_voice = (uint8_t)s_settings.tts_voice,
        .tts_speed_percent = s_settings.tts_speed_percent,
    };

    err = nvs_set_blob(handle, APP_SETTINGS_KEY_BLOB, &blob, sizeof(blob));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not save settings: %s", esp_err_to_name(err));
    }

    return err;
}

static esp_err_t load_settings(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(APP_SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Could not open settings NVS for read: %s", esp_err_to_name(err));
        }
        return err;
    }

    app_settings_nvs_blob_t blob = {0};
    size_t size = sizeof(blob);
    err = nvs_get_blob(handle, APP_SETTINGS_KEY_BLOB, &blob, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Could not load settings: %s", esp_err_to_name(err));
        }
        return err;
    }

    if (size != sizeof(blob) || blob.version != APP_SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Ignoring incompatible settings blob");
        return ESP_ERR_INVALID_VERSION;
    }

    s_settings.stt_language = (lookai_stt_language_t)blob.stt_language;
    s_settings.stt_model = (lookai_stt_model_t)blob.stt_model;
    s_settings.ai_style = (lookai_ai_style_t)blob.ai_style;
    s_settings.ai_temperature = (lookai_ai_temperature_t)blob.ai_temperature;
    s_settings.tts_voice = (lookai_tts_voice_t)blob.tts_voice;
    s_settings.tts_speed_percent = blob.tts_speed_percent;
    clamp_settings();

    return ESP_OK;
}

esp_err_t app_settings_init(void)
{
    if (is_initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase before settings init");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }

    err = load_settings();
    if (err == ESP_ERR_NVS_NOT_FOUND || err == ESP_ERR_INVALID_VERSION) {
        err = save_settings();
    }

    is_initialized = true;
    return err == ESP_OK ? ESP_OK : ESP_OK;
}

const lookai_runtime_settings_t *app_settings_get(void)
{
    return &s_settings;
}

const char *app_settings_get_stt_language_code(void)
{
    return STT_LANGUAGES[s_settings.stt_language].value;
}

const char *app_settings_get_stt_language_label(void)
{
    return STT_LANGUAGES[s_settings.stt_language].label;
}

const char *app_settings_get_stt_model(void)
{
    return STT_MODELS[s_settings.stt_model].value;
}

const char *app_settings_get_stt_model_label(void)
{
    return STT_MODELS[s_settings.stt_model].label;
}

const char *app_settings_get_ai_model(void)
{
    return CONFIG_LOOKAI_AI_MODEL;
}

const char *app_settings_get_ai_system_prompt(void)
{
    switch (s_settings.ai_style) {
        case LOOKAI_AI_STYLE_DETAILED:
            return "Turkce cevap ver. Net, yardimci ve gerekirse 4-5 cumlelik ayrintili cevap kullan.";
        case LOOKAI_AI_STYLE_BALANCED:
            return "Turkce cevap ver. Kisa, net ve dogal konus. Genelde 2-3 cumle yeterlidir.";
        case LOOKAI_AI_STYLE_SHORT:
        default:
            return "Kisa, net ve Turkce cevap ver. Sesli okunacagi icin en fazla 1-2 cumle kullan.";
    }
}

const char *app_settings_get_ai_style_label(void)
{
    switch (s_settings.ai_style) {
        case LOOKAI_AI_STYLE_DETAILED:
            return "Detailed";
        case LOOKAI_AI_STYLE_BALANCED:
            return "Balanced";
        case LOOKAI_AI_STYLE_SHORT:
        default:
            return "Short";
    }
}

const char *app_settings_get_ai_temperature_json(void)
{
    switch (s_settings.ai_temperature) {
        case LOOKAI_AI_TEMPERATURE_LOW:
            return "0.2";
        case LOOKAI_AI_TEMPERATURE_CREATIVE:
            return "0.7";
        case LOOKAI_AI_TEMPERATURE_NORMAL:
        default:
            return "0.4";
    }
}

const char *app_settings_get_ai_temperature_label(void)
{
    switch (s_settings.ai_temperature) {
        case LOOKAI_AI_TEMPERATURE_LOW:
            return "Precise";
        case LOOKAI_AI_TEMPERATURE_CREATIVE:
            return "Creative";
        case LOOKAI_AI_TEMPERATURE_NORMAL:
        default:
            return "Normal";
    }
}

int app_settings_get_ai_max_tokens(void)
{
    switch (s_settings.ai_style) {
        case LOOKAI_AI_STYLE_DETAILED:
            return 384;
        case LOOKAI_AI_STYLE_BALANCED:
            return 224;
        case LOOKAI_AI_STYLE_SHORT:
        default:
            return 128;
    }
}

int app_settings_get_ai_max_output_chars(void)
{
    switch (s_settings.ai_style) {
        case LOOKAI_AI_STYLE_DETAILED:
            return 1024;
        case LOOKAI_AI_STYLE_BALANCED:
            return 720;
        case LOOKAI_AI_STYLE_SHORT:
        default:
            return 420;
    }
}

const char *app_settings_get_tts_model(void)
{
    return CONFIG_LOOKAI_TTS_MODEL;
}

const char *app_settings_get_tts_voice(void)
{
    return TTS_VOICES[s_settings.tts_voice].value;
}

const char *app_settings_get_tts_voice_label(void)
{
    return TTS_VOICES[s_settings.tts_voice].label;
}

const char *app_settings_get_tts_speed_json(void)
{
    static char speed_json[16];
    snprintf(speed_json, sizeof(speed_json), "%u.%02u", (unsigned)(s_settings.tts_speed_percent / 100), (unsigned)(s_settings.tts_speed_percent % 100));
    return speed_json;
}

const char *app_settings_get_tts_speed_label(void)
{
    static char speed_label[16];
    snprintf(speed_label, sizeof(speed_label), "%u.%02ux", (unsigned)(s_settings.tts_speed_percent / 100), (unsigned)(s_settings.tts_speed_percent % 100));
    return speed_label;
}

uint16_t app_settings_get_tts_speed_percent(void)
{
    return s_settings.tts_speed_percent;
}

const char *app_settings_get_tts_instructions(void)
{
    if (s_settings.tts_speed_percent < 90) {
        return "Turkceyi sakin, net ve yavas tempoda oku. Cumleleri anlasilir bol.";
    }
    if (s_settings.tts_speed_percent > 120) {
        return "Turkceyi dogal, enerjik ve hizli ama anlasilir tonda oku.";
    }
    return "Turkceyi dogal, net ve sicak bir tonda konus. Cumleleri sakin ve anlasilir oku.";
}

const char *app_settings_get_stt_language_dropdown_options(void) { return STT_LANGUAGE_OPTIONS; }
const char *app_settings_get_stt_model_dropdown_options(void) { return STT_MODEL_OPTIONS; }
const char *app_settings_get_ai_style_dropdown_options(void) { return AI_STYLE_OPTIONS; }
const char *app_settings_get_ai_temperature_dropdown_options(void) { return AI_TEMPERATURE_OPTIONS; }
const char *app_settings_get_tts_voice_dropdown_options(void) { return TTS_VOICE_OPTIONS; }
const char *app_settings_get_tts_speed_dropdown_options(void) { return TTS_SPEED_OPTIONS; }

int app_settings_get_stt_language_index(void) { return s_settings.stt_language; }
int app_settings_get_stt_model_index(void) { return s_settings.stt_model; }
int app_settings_get_ai_style_index(void) { return s_settings.ai_style; }
int app_settings_get_ai_temperature_index(void) { return s_settings.ai_temperature; }
int app_settings_get_tts_voice_index(void) { return s_settings.tts_voice; }
int app_settings_get_tts_speed_index(void) {
    if (s_settings.tts_speed_percent <= 50) return LOOKAI_TTS_SPEED_050;
    if (s_settings.tts_speed_percent <= 75) return LOOKAI_TTS_SPEED_075;
    if (s_settings.tts_speed_percent <= 100) return LOOKAI_TTS_SPEED_100;
    if (s_settings.tts_speed_percent <= 125) return LOOKAI_TTS_SPEED_125;
    if (s_settings.tts_speed_percent <= 150) return LOOKAI_TTS_SPEED_150;
    if (s_settings.tts_speed_percent <= 175) return LOOKAI_TTS_SPEED_175;
    return LOOKAI_TTS_SPEED_200;
}

esp_err_t app_settings_set_stt_language_index(int index)
{
    s_settings.stt_language = (lookai_stt_language_t)clamp_index(index, LOOKAI_STT_LANGUAGE_COUNT, LOOKAI_STT_LANGUAGE_AUTO);
    return save_settings();
}

esp_err_t app_settings_set_stt_model_index(int index)
{
    s_settings.stt_model = (lookai_stt_model_t)clamp_index(index, LOOKAI_STT_MODEL_COUNT, LOOKAI_STT_MODEL_GPT_4O_MINI);
    return save_settings();
}

esp_err_t app_settings_set_ai_style_index(int index)
{
    s_settings.ai_style = (lookai_ai_style_t)clamp_index(index, LOOKAI_AI_STYLE_COUNT, LOOKAI_AI_STYLE_SHORT);
    return save_settings();
}

esp_err_t app_settings_set_ai_temperature_index(int index)
{
    s_settings.ai_temperature = (lookai_ai_temperature_t)clamp_index(index, LOOKAI_AI_TEMPERATURE_COUNT, LOOKAI_AI_TEMPERATURE_NORMAL);
    return save_settings();
}

esp_err_t app_settings_set_tts_voice_index(int index)
{
    s_settings.tts_voice = (lookai_tts_voice_t)clamp_index(index, LOOKAI_TTS_VOICE_COUNT, LOOKAI_TTS_VOICE_MARIN);
    return save_settings();
}

esp_err_t app_settings_set_tts_speed_index(int index)
{
    static const uint16_t map[] = {50, 75, 100, 125, 150, 175, 200};
    int safe_index = clamp_index(index, LOOKAI_TTS_SPEED_COUNT, LOOKAI_TTS_SPEED_100);
    s_settings.tts_speed_percent = map[safe_index];
    return save_settings();
}

esp_err_t app_settings_set_tts_speed_percent(uint16_t speed_percent)
{
    if (speed_percent < 50) {
        speed_percent = 50;
    }
    if (speed_percent > 200) {
        speed_percent = 200;
    }
    s_settings.tts_speed_percent = speed_percent;
    return save_settings();
}

esp_err_t app_settings_cycle_stt_language(int step)
{
    return app_settings_set_stt_language_index(cycle_index(s_settings.stt_language, LOOKAI_STT_LANGUAGE_COUNT, step));
}

esp_err_t app_settings_cycle_stt_model(int step)
{
    return app_settings_set_stt_model_index(cycle_index(s_settings.stt_model, LOOKAI_STT_MODEL_COUNT, step));
}

esp_err_t app_settings_cycle_ai_style(int step)
{
    return app_settings_set_ai_style_index(cycle_index(s_settings.ai_style, LOOKAI_AI_STYLE_COUNT, step));
}

esp_err_t app_settings_cycle_ai_temperature(int step)
{
    return app_settings_set_ai_temperature_index(cycle_index(s_settings.ai_temperature, LOOKAI_AI_TEMPERATURE_COUNT, step));
}

esp_err_t app_settings_cycle_tts_voice(int step)
{
    return app_settings_set_tts_voice_index(cycle_index(s_settings.tts_voice, LOOKAI_TTS_VOICE_COUNT, step));
}

esp_err_t app_settings_cycle_tts_speed(int step)
{
    return app_settings_set_tts_speed_index(cycle_index(app_settings_get_tts_speed_index(), LOOKAI_TTS_SPEED_COUNT, step));
}
