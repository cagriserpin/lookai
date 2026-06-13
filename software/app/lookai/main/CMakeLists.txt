idf_component_register(
    SRCS
        "main.c"

        "app/app_controller.c"

        "stt/stt_manager.c"

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
    INCLUDE_DIRS
        "."
        "app"
        "stt"
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
)
