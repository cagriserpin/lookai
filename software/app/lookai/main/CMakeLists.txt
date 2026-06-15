idf_component_register(
    SRCS
        "main.c"

        "app/app_controller.c"

        "input/board_buttons.c"

        "system/runtime_diag.c"
        "system/app_settings.c"

        "ai/ai_manager.c"
        "ai/ai_api_client.c"

        "stt/stt_manager.c"
        "stt/stt_api_client.c"

        "tts/tts_manager.c"
        "tts/tts_api_client.c"

        "audio/audio_recorder.c"
        "audio/audio_playback.c"
        "audio/wav_writer.c"

        "wifi/wifi_manager.c"
        "wifi/wifi_ap.c"
        "wifi/wifi_storage.c"

        "portal/captive_portal.c"
        "portal/dns_server.c"

        "ui/assets/ui_icons.c"

        "ui/ui_manager.c"
        "ui/lookai_display.c"
        "ui/ui_theme.c"

        "ui/navigation/menu_controller.c"

        "ui/items/ui_button.c"
        "ui/items/ui_card.c"
        "ui/items/ui_label.c"
        "ui/items/ui_settings_item.c"
        "ui/items/ui_loading_dots.c"

        "ui/scaffold/ui_scaffold.c"

        "ui/menus/home/home_screen.c"
        "ui/menus/settings/settings_screen.c"
        "ui/menus/settings/wifi/wifi_settings_screen.c"
        "ui/menus/settings/wifi/manage_networks_screen.c"
        "ui/menus/settings/brightness/brightness_screen.c"
        "ui/menus/settings/volume/volume_screen.c"
        "ui/menus/settings/ai/ai_screen.c"
        "ui/menus/settings/stt/stt_screen.c"
        "ui/menus/settings/tts/tts_screen.c"
        "ui/menus/settings/api/api_settings_screen.c"
    INCLUDE_DIRS
        "."
        "app"
        "input"
        "system"
        "ai"
        "stt"
        "tts"
        "audio"
        "wifi"
        "portal"
        "ui"
        "ui/assets"
        "ui/navigation"
        "ui/items"
        "ui/scaffold"
        "ui/menus/home"
        "ui/menus/settings"
        "ui/menus/settings/brightness"
        "ui/menus/settings/volume"
        "ui/menus/settings/wifi"
        "ui/menus/settings/ai"
        "ui/menus/settings/stt"
        "ui/menus/settings/tts"
        "ui/menus/settings/api"
    REQUIRES
        esp_common
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        lwip
        esp_codec_dev
        esp_io_expander
        driver
        spiffs
        json
        esp-tls
        esp_http_client
        esp_timer
)
