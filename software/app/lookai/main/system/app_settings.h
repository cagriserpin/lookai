/**
 * @file system/app_settings.h
 * @brief Runtime user-adjustable LookAI API settings.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOOKAI_SETTING_STEP_PREV = -1,
    LOOKAI_SETTING_STEP_NEXT = 1,
} lookai_setting_step_t;

typedef enum {
    LOOKAI_STT_LANGUAGE_AUTO = 0,
    LOOKAI_STT_LANGUAGE_TR,
    LOOKAI_STT_LANGUAGE_EN,
    LOOKAI_STT_LANGUAGE_DE,
    LOOKAI_STT_LANGUAGE_FR,
    LOOKAI_STT_LANGUAGE_ES,
    LOOKAI_STT_LANGUAGE_IT,
    LOOKAI_STT_LANGUAGE_PT,
    LOOKAI_STT_LANGUAGE_RU,
    LOOKAI_STT_LANGUAGE_AR,
    LOOKAI_STT_LANGUAGE_ZH,
    LOOKAI_STT_LANGUAGE_JA,
    LOOKAI_STT_LANGUAGE_KO,
    LOOKAI_STT_LANGUAGE_COUNT,
} lookai_stt_language_t;

typedef enum {
    LOOKAI_STT_MODEL_GPT_4O_MINI = 0,
    LOOKAI_STT_MODEL_GPT_4O,
    LOOKAI_STT_MODEL_GPT_4O_DIARIZE,
    LOOKAI_STT_MODEL_WHISPER,
    LOOKAI_STT_MODEL_COUNT,
} lookai_stt_model_t;

typedef enum {
    LOOKAI_AI_STYLE_SHORT = 0,
    LOOKAI_AI_STYLE_BALANCED,
    LOOKAI_AI_STYLE_DETAILED,
    LOOKAI_AI_STYLE_COUNT,
} lookai_ai_style_t;

typedef enum {
    LOOKAI_AI_TEMPERATURE_LOW = 0,
    LOOKAI_AI_TEMPERATURE_NORMAL,
    LOOKAI_AI_TEMPERATURE_CREATIVE,
    LOOKAI_AI_TEMPERATURE_COUNT,
} lookai_ai_temperature_t;

typedef enum {
    LOOKAI_TTS_VOICE_ALLOY = 0,
    LOOKAI_TTS_VOICE_ASH,
    LOOKAI_TTS_VOICE_BALLAD,
    LOOKAI_TTS_VOICE_CORAL,
    LOOKAI_TTS_VOICE_ECHO,
    LOOKAI_TTS_VOICE_FABLE,
    LOOKAI_TTS_VOICE_ONYX,
    LOOKAI_TTS_VOICE_NOVA,
    LOOKAI_TTS_VOICE_SAGE,
    LOOKAI_TTS_VOICE_SHIMMER,
    LOOKAI_TTS_VOICE_VERSE,
    LOOKAI_TTS_VOICE_MARIN,
    LOOKAI_TTS_VOICE_CEDAR,
    LOOKAI_TTS_VOICE_COUNT,
} lookai_tts_voice_t;

typedef enum {
    LOOKAI_TTS_SPEED_050 = 0,
    LOOKAI_TTS_SPEED_075,
    LOOKAI_TTS_SPEED_100,
    LOOKAI_TTS_SPEED_125,
    LOOKAI_TTS_SPEED_150,
    LOOKAI_TTS_SPEED_175,
    LOOKAI_TTS_SPEED_200,
    LOOKAI_TTS_SPEED_COUNT,
} lookai_tts_speed_t;

typedef struct {
    lookai_stt_language_t stt_language;
    lookai_stt_model_t stt_model;
    lookai_ai_style_t ai_style;
    lookai_ai_temperature_t ai_temperature;
    lookai_tts_voice_t tts_voice;
    uint16_t tts_speed_percent;
} lookai_runtime_settings_t;

esp_err_t app_settings_init(void);
const lookai_runtime_settings_t *app_settings_get(void);

const char *app_settings_get_stt_language_code(void);
const char *app_settings_get_stt_language_label(void);
const char *app_settings_get_stt_model(void);
const char *app_settings_get_stt_model_label(void);

const char *app_settings_get_ai_model(void);
const char *app_settings_get_ai_system_prompt(void);
const char *app_settings_get_ai_style_label(void);
const char *app_settings_get_ai_temperature_json(void);
const char *app_settings_get_ai_temperature_label(void);
int app_settings_get_ai_max_tokens(void);
int app_settings_get_ai_max_output_chars(void);

const char *app_settings_get_tts_model(void);
const char *app_settings_get_tts_voice(void);
const char *app_settings_get_tts_voice_label(void);
const char *app_settings_get_tts_speed_json(void);
const char *app_settings_get_tts_speed_label(void);
const char *app_settings_get_tts_instructions(void);

const char *app_settings_get_stt_language_dropdown_options(void);
const char *app_settings_get_stt_model_dropdown_options(void);
const char *app_settings_get_ai_style_dropdown_options(void);
const char *app_settings_get_ai_temperature_dropdown_options(void);
const char *app_settings_get_tts_voice_dropdown_options(void);
const char *app_settings_get_tts_speed_dropdown_options(void);
uint16_t app_settings_get_tts_speed_percent(void);

int app_settings_get_stt_language_index(void);
int app_settings_get_stt_model_index(void);
int app_settings_get_ai_style_index(void);
int app_settings_get_ai_temperature_index(void);
int app_settings_get_tts_voice_index(void);
int app_settings_get_tts_speed_index(void);

esp_err_t app_settings_set_stt_language_index(int index);
esp_err_t app_settings_set_stt_model_index(int index);
esp_err_t app_settings_set_ai_style_index(int index);
esp_err_t app_settings_set_ai_temperature_index(int index);
esp_err_t app_settings_set_tts_voice_index(int index);
esp_err_t app_settings_set_tts_speed_index(int index);
esp_err_t app_settings_set_tts_speed_percent(uint16_t speed_percent);

esp_err_t app_settings_cycle_stt_language(int step);
esp_err_t app_settings_cycle_stt_model(int step);
esp_err_t app_settings_cycle_ai_style(int step);
esp_err_t app_settings_cycle_ai_temperature(int step);
esp_err_t app_settings_cycle_tts_voice(int step);
esp_err_t app_settings_cycle_tts_speed(int step);

#ifdef __cplusplus
}
#endif
