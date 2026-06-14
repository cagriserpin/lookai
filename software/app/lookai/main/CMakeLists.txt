idf_component_register(
    SRCS
        "main.c"

        "app/app_controller.c"

        "system/runtime_diag.c"

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

        "ui/scaffold/ui_scaffold.c"

        "ui/menus/settings/settings_screen.c"
        "ui/menus/settings/wifi/wifi_settings_screen.c"
        "ui/menus/settings/wifi/manage_networks_screen.c"
        "ui/menus/settings/brightness/brightness_screen.c"
        "ui/menus/settings/stt/stt_screen.c"
        "ui/menus/settings/tts/tts_screen.c"
    INCLUDE_DIRS
        "."
        "app"
        "system"
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
        "ui/menus/settings"
        "ui/menus/settings/brightness"
        "ui/menus/settings/wifi"
        "ui/menus/settings/stt"
        "ui/menus/settings/tts"
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        lwip
        esp_codec_dev
        driver
        spiffs
        json
        esp-tls
        esp_http_client
        esp_timer
)
