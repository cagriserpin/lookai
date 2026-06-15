/**
 * @file app/app_controller.c
 * @brief Top-level application initialization and module wiring.
 */

#include "app_controller.h"

#include "ai_manager.h"
#include "audio_playback.h"
#include "audio_recorder.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "runtime_diag.h"
#include "app_settings.h"

#include "stt_manager.h"
#include "tts_manager.h"
#include "ui_manager.h"
#include "wifi_manager.h"
#include "wifi_storage.h"

static const char *TAG = "app_controller";

#ifndef CONFIG_LOOKAI_RUNTIME_DIAG_ENABLE
#define CONFIG_LOOKAI_RUNTIME_DIAG_ENABLE 0
#endif

/**
 * @brief Reduce expected captive-portal HTTP log noise.
 *
 * Captive portal clients often open and close sockets aggressively while they
 * probe for internet access. These logs are expected and can slow the device
 * when they spam the serial console.
 */
static void configure_log_levels(void)
{
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

#if !CONFIG_LOOKAI_RUNTIME_DIAG_ENABLE
    /*
     * Normal mode: keep serial logging out of the UI/audio/HTTPS hot paths.
     * Warnings and errors are still visible; detailed TIMING/runtime logs can
     * be re-enabled from LookAI diagnostics.
     */
    esp_log_level_set("runtime_diag", ESP_LOG_WARN);
    esp_log_level_set("lookai_display", ESP_LOG_WARN);
    esp_log_level_set("ui_manager", ESP_LOG_WARN);
    esp_log_level_set("stt_manager", ESP_LOG_WARN);
    esp_log_level_set("ai_manager", ESP_LOG_WARN);
    esp_log_level_set("tts_manager", ESP_LOG_WARN);
    esp_log_level_set("stt_api_client", ESP_LOG_WARN);
    esp_log_level_set("ai_api_client", ESP_LOG_WARN);
    esp_log_level_set("tts_api_client", ESP_LOG_WARN);
#endif
}

esp_err_t app_controller_start(void)
{
    ESP_LOGI(TAG, "Starting application");

    configure_log_levels();

    esp_err_t diag_err = runtime_diag_start_task_stack_report();
    if (diag_err != ESP_OK) {
        ESP_LOGW(TAG, "Task stack report not started: %s", esp_err_to_name(diag_err));
    }

    ESP_RETURN_ON_ERROR(ui_manager_init(), TAG, "Failed to initialize UI");
    ESP_RETURN_ON_ERROR(wifi_storage_init(), TAG, "Failed to initialize Wi-Fi storage");
    ESP_RETURN_ON_ERROR(app_settings_init(), TAG, "Failed to initialize app settings");

    esp_err_t audio_err = audio_recorder_init();
    if (audio_err != ESP_OK) {
        /*
         * Keep the app usable even if SPIFFS/audio is not ready yet.
         * The STT screen will show the recorder error when Push to Talk is used.
         */
        ESP_LOGW(TAG, "Audio recorder init failed: %s", esp_err_to_name(audio_err));
    }

    esp_err_t playback_err = audio_playback_init();
    if (playback_err != ESP_OK) {
        /*
         * Keep the app usable even if speaker playback is not ready yet.
         */
        ESP_LOGW(TAG, "Audio playback init failed: %s", esp_err_to_name(playback_err));
    }

    ESP_RETURN_ON_ERROR(stt_manager_start(), TAG, "Failed to start STT manager");
    ESP_RETURN_ON_ERROR(ai_manager_start(), TAG, "Failed to start AI manager");
    ESP_RETURN_ON_ERROR(tts_manager_start(), TAG, "Failed to start TTS manager");

    ui_manager_callbacks_t callbacks = {
        .connect_another = wifi_manager_open_setup_portal,
        .close_portal = wifi_manager_close_setup_portal,
        .wifi_enable = wifi_manager_enable,
        .wifi_disable = wifi_manager_disable,
        .connect_saved = wifi_manager_connect_saved_network,
        .forget_saved = wifi_manager_forget_saved_network,
        .stt_press = stt_manager_press,
        .stt_release = stt_manager_release,
        .stt_test_speaker = stt_manager_toggle_speaker_test,
        .stt_play_recording = stt_manager_play_recording,
        .ai_press = ai_manager_press,
        .ai_release = ai_manager_release,
        .tts_sample_1 = tts_manager_speak_sample_1,
        .tts_sample_2 = tts_manager_speak_sample_2,
    };

    ui_manager_set_callbacks(&callbacks);

    ESP_RETURN_ON_ERROR(wifi_manager_start(), TAG, "Failed to start Wi-Fi manager");

    /* Keep the boot destination deterministic: the device starts on the app picker.
     * Wi-Fi status updates should refresh the status bar/home state, not push a
     * settings page over the launcher.
     */
    ui_manager_show_home();

    return ESP_OK;
}
