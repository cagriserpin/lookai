idf_component_register(
    SRCS
        "main.c"

        "app/app_controller.c"

        "wifi/wifi_manager.c"
        "wifi/wifi_ap.c"
        "wifi/wifi_storage.c"

        "portal/captive_portal.c"
        "portal/dns_server.c"

        "ui/ui_manager.c"
        "ui/ui_theme.c"

        "ui/navigation/menu_controller.c"

        "ui/items/ui_button.c"
        "ui/items/ui_card.c"
        "ui/items/ui_label.c"

        "ui/scaffold/ui_scaffold.c"

        "ui/menus/settings/settings_screen.c"
        "ui/menus/settings/wifi_settings_screen.c"
        "ui/menus/settings/manage_networks_screen.c"
    INCLUDE_DIRS
        "."
        "app"
        "wifi"
        "portal"
        "ui"
        "ui/navigation"
        "ui/items"
        "ui/scaffold"
        "ui/menus/settings"
    REQUIRES
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        esp_http_server
        lwip
)
