/**
 * @file captive_portal.c
 * @brief HTTP captive portal implementation for adding/switching Wi-Fi networks.
 */

#include "captive_portal.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "wifi_ap.h"

static const char *TAG = "captive_portal";

static httpd_handle_t s_server = NULL;
static captive_portal_connect_cb_t s_connect_cb = NULL;

static int hex_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }

    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

static void url_decode(const char *src, char *dst, size_t dst_len)
{
    size_t di = 0;

    for (size_t si = 0; src[si] != '\0' && di + 1 < dst_len; si++) {
        if (src[si] == '+') {
            dst[di++] = ' ';
        } else if (
            src[si] == '%' &&
            isxdigit((unsigned char)src[si + 1]) &&
            isxdigit((unsigned char)src[si + 2])
        ) {
            int high = hex_to_int(src[si + 1]);
            int low = hex_to_int(src[si + 2]);

            if (high >= 0 && low >= 0) {
                dst[di++] = (char)((high << 4) | low);
                si += 2;
            }
        } else {
            dst[di++] = src[si];
        }
    }

    dst[di] = '\0';
}

static bool form_get_field(const char *body, const char *key, char *out, size_t out_len)
{
    char temp[512];

    strncpy(temp, body, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(temp, "&", &saveptr);

    size_t key_len = strlen(key);

    while (token != NULL) {
        if (strncmp(token, key, key_len) == 0 && token[key_len] == '=') {
            url_decode(token + key_len + 1, out, out_len);
            return true;
        }

        token = strtok_r(NULL, "&", &saveptr);
    }

    return false;
}

static void send_html_escaped(httpd_req_t *req, const char *text)
{
    if (text == NULL) {
        return;
    }

    for (const char *p = text; *p != '\0'; p++) {
        switch (*p) {
            case '&':
                httpd_resp_sendstr_chunk(req, "&amp;");
                break;
            case '<':
                httpd_resp_sendstr_chunk(req, "&lt;");
                break;
            case '>':
                httpd_resp_sendstr_chunk(req, "&gt;");
                break;
            case '"':
                httpd_resp_sendstr_chunk(req, "&quot;");
                break;
            case '\'':
                httpd_resp_sendstr_chunk(req, "&#39;");
                break;
            default: {
                char c[2] = {*p, '\0'};
                httpd_resp_sendstr_chunk(req, c);
                break;
            }
        }
    }
}

static void send_page_header(httpd_req_t *req, const char *title, const char *active_tab)
{
    httpd_resp_set_type(req, "text/html");

    httpd_resp_sendstr_chunk(req,
        "<!doctype html>"
        "<html>"
        "<head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1, viewport-fit=cover'>"
        "<title>"
    );

    send_html_escaped(req, title);

    httpd_resp_sendstr_chunk(req,
        "</title>"
        "<style>"
        ":root{color-scheme:dark;--bg:#080b12;--card:#141925;--line:#2a3448;--text:#eef3ff;--muted:#9aa7bd;--blue:#4da3ff;--green:#37d67a;--red:#ff5c7a;--yellow:#ffd166;}"
        "*{box-sizing:border-box;}"
        "body{margin:0;min-height:100vh;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif;background:radial-gradient(circle at top,#1f3b63 0,#080b12 46%,#05070c 100%);color:var(--text);padding:18px;}"
        ".wrap{max-width:480px;margin:0 auto;}"
        ".hero{padding:20px 4px 18px;text-align:center;}"
        ".badge{display:inline-flex;gap:8px;align-items:center;padding:8px 12px;border:1px solid rgba(255,255,255,.14);border-radius:999px;background:rgba(255,255,255,.06);color:#cfe3ff;font-size:13px;}"
        "h1{font-size:28px;line-height:1.05;margin:14px 0 8px;letter-spacing:-.03em;}"
        ".sub{color:var(--muted);font-size:15px;line-height:1.45;margin:0 auto;max-width:360px;}"
        ".tabs{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:8px 0 14px;}"
        ".tab{display:block;text-align:center;text-decoration:none;color:var(--muted);padding:10px 8px;border:1px solid var(--line);border-radius:14px;background:rgba(20,25,37,.8);font-weight:700;font-size:13px;}"
        ".tab.active{color:white;border-color:rgba(77,163,255,.7);background:linear-gradient(180deg,#245d9a,#16395f);}"
        ".card{background:rgba(20,25,37,.92);border:1px solid rgba(255,255,255,.08);border-radius:22px;padding:18px;margin:12px 0;box-shadow:0 24px 60px rgba(0,0,0,.35);}"
        ".card h2{font-size:18px;margin:0 0 8px;letter-spacing:-.02em;}"
        ".muted{color:var(--muted);font-size:14px;line-height:1.45;}"
        ".steps{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;margin:14px 0;}"
        ".step{border:1px solid var(--line);border-radius:16px;padding:10px;background:rgba(255,255,255,.03);}"
        ".step b{display:block;font-size:12px;color:#cfe3ff;margin-bottom:4px;}"
        ".step span{font-size:12px;color:var(--muted);line-height:1.3;}"
        "label{display:block;margin-top:14px;margin-bottom:7px;color:#c7d2e4;font-weight:700;font-size:14px;}"
        "select,input{width:100%;font-size:16px;padding:13px 14px;border-radius:14px;border:1px solid var(--line);background:#0d121d;color:var(--text);outline:none;}"
        "input::placeholder{color:#647187;}"
        ".pass{position:relative;}"
        ".pass input{padding-right:54px;}"
        ".eye{position:absolute;right:8px;top:8px;width:40px;height:40px;border:0;border-radius:12px;background:#222b3c;color:#d9e6ff;font-size:18px;}"
        ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px;}"
        ".btn{display:inline-flex;width:100%;justify-content:center;align-items:center;text-decoration:none;border:0;border-radius:15px;padding:14px 16px;margin-top:14px;font-size:16px;font-weight:800;color:white;background:linear-gradient(180deg,#55a9ff,#2d7ee8);box-shadow:0 10px 24px rgba(77,163,255,.22);}"
        ".btn.secondary{background:#222b3c;color:#d9e6ff;box-shadow:none;border:1px solid var(--line);}"
        ".pill{display:inline-flex;align-items:center;gap:7px;padding:8px 10px;border-radius:999px;background:#0d121d;border:1px solid var(--line);color:#cbd8ea;font-size:13px;}"
        ".kv{display:flex;justify-content:space-between;gap:12px;border-bottom:1px solid rgba(255,255,255,.07);padding:10px 0;}"
        ".kv:last-child{border-bottom:0;}"
        ".kv span{color:var(--muted);}"
        ".ok{color:var(--green);font-weight:800;}"
        ".warn{color:var(--yellow);font-weight:800;}"
        ".notice{margin-top:12px;color:#cfe3ff;font-size:13px;min-height:20px;}"
        ".footer{text-align:center;color:#647187;font-size:12px;margin:20px 0 8px;}"
        "</style>"
        "<script>"
        "function togglePassword(){var p=document.getElementById('password');var e=document.getElementById('eye');if(!p)return;if(p.type==='password'){p.type='text';if(e)e.textContent='🙈';}else{p.type='password';if(e)e.textContent='👁';}}"
        "function refreshNetworks(){var n=document.getElementById('refreshNotice');if(n)n.textContent='Refreshing networks... this page will reload.';fetch('/refresh').then(function(){setTimeout(function(){location.reload();},3500);}).catch(function(){setTimeout(function(){location.reload();},3500);});}"
        "</script>"
        "</head>"
        "<body>"
        "<div class='wrap'>"
        "<div class='hero'>"
        "<div class='badge'>📡 LookAI new Wi-Fi setup</div>"
        "<h1>Connect a Network</h1>"
        "<p class='sub'>Use this portal to add or switch Wi-Fi. Saved networks are managed from the device screen.</p>"
        "</div>"
        "<nav class='tabs'>"
    );

    httpd_resp_sendstr_chunk(req, "<a class='tab ");
    if (active_tab != NULL && strcmp(active_tab, "connect") == 0) {
        httpd_resp_sendstr_chunk(req, "active");
    }
    httpd_resp_sendstr_chunk(req, "' href='/scan'>Connect</a>");

    httpd_resp_sendstr_chunk(req, "<a class='tab ");
    if (active_tab != NULL && strcmp(active_tab, "status") == 0) {
        httpd_resp_sendstr_chunk(req, "active");
    }
    httpd_resp_sendstr_chunk(req, "' href='/status'>Status</a>");

    httpd_resp_sendstr_chunk(req, "</nav>");
}

static void send_page_footer(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req,
        "<div class='footer'>LookAI setup portal · 192.168.4.1</div>"
        "</div>"
        "</body>"
        "</html>"
    );

    httpd_resp_sendstr_chunk(req, NULL);
}

static void send_steps(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req,
        "<div class='steps'>"
        "<div class='step'><b>Step 1</b><span>Select Wi-Fi</span></div>"
        "<div class='step'><b>Step 2</b><span>Enter password</span></div>"
        "<div class='step'><b>Step 3</b><span>Device connects</span></div>"
        "</div>"
    );
}


static esp_err_t no_content_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_probe_handler(httpd_req_t *req)
{
    /*
     * Keep common OS captive-portal probes lightweight.
     * Returning a redirect/small response is enough to make the phone open the
     * captive portal, while avoiding full-page sends to short-lived probe sockets.
     */
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/scan");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t catch_all_handler(httpd_req_t *req)
{
    /*
     * Unknown captive-portal probe paths should not receive the full portal HTML.
     * Redirect to the main setup page instead.
     */
    return captive_probe_handler(req);
}

static esp_err_t scan_page_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET /scan");

    wifi_ap_scan_result_t results[WIFI_AP_SCAN_MAX_RESULTS] = {0};
    uint16_t count = WIFI_AP_SCAN_MAX_RESULTS;

    esp_err_t err = wifi_ap_get_scan_results(results, &count);

    send_page_header(req, "LookAI Wi-Fi Setup", "connect");

    httpd_resp_sendstr_chunk(req,
        "<div class='card'>"
        "<h2>Add or switch Wi-Fi</h2>"
        "<p class='muted'>Choose a network found by the ESP32. Existing saved networks are kept unless you forget them from the device screen.</p>"
    );

    send_steps(req);

    if (err != ESP_OK || count == 0) {
        httpd_resp_sendstr_chunk(req,
            "<p class='warn'>Wi-Fi scan is not ready yet.</p>"
            "<p class='muted'>Wait a few seconds, then refresh the network list.</p>"
            "<button class='btn secondary' type='button' onclick='refreshNetworks()'>Refresh scan</button>"
            "<div id='refreshNotice' class='notice'></div>"
            "</div>"
        );
        send_page_footer(req);
        return ESP_OK;
    }

    httpd_resp_sendstr_chunk(req,
        "<form method='POST' action='/connect'>"
        "<label>Available networks</label>"
        "<select name='ssid'>"
    );

    for (uint16_t i = 0; i < count; i++) {
        if (results[i].ssid[0] == '\0') {
            continue;
        }

        char meta[64];
        snprintf(
            meta,
            sizeof(meta),
            " · %d dBm%s",
            results[i].rssi,
            results[i].secure ? " · locked" : " · open"
        );

        httpd_resp_sendstr_chunk(req, "<option value='");
        send_html_escaped(req, results[i].ssid);
        httpd_resp_sendstr_chunk(req, "'>");

        send_html_escaped(req, results[i].ssid);
        send_html_escaped(req, meta);

        httpd_resp_sendstr_chunk(req, "</option>");
    }

    httpd_resp_sendstr_chunk(req,
        "</select>"
        "<label>Password</label>"
        "<div class='pass'>"
        "<input id='password' name='password' type='password' maxlength='64' placeholder='Wi-Fi password'>"
        "<button id='eye' class='eye' type='button' onclick='togglePassword()'>👁</button>"
        "</div>"
        "<button class='btn' type='submit'>Connect device</button>"
        "</form>"
        "<button class='btn secondary' type='button' onclick='refreshNetworks()'>Refresh networks</button>"
        "<div id='refreshNotice' class='notice'></div>"
        "</div>"
    );

    send_page_footer(req);

    return ESP_OK;
}

static esp_err_t refresh_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET /refresh");

    esp_err_t err = wifi_ap_scan_refresh_async();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not start refresh scan: %s", esp_err_to_name(err));
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OK");

    return ESP_OK;
}

static esp_err_t connect_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP POST /connect");

    if (req->content_len <= 0 || req->content_len >= 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid form size");
        return ESP_OK;
    }

    char body[512] = {0};

    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);

        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read request");
            return ESP_OK;
        }

        received += ret;
    }

    body[received] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};

    if (!form_get_field(body, "ssid", ssid, sizeof(ssid))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_OK;
    }

    form_get_field(body, "password", password, sizeof(password));

    ESP_LOGI(TAG, "Credentials received for SSID: %s", ssid);

    if (s_connect_cb != NULL) {
        s_connect_cb(ssid, password);
    }

    send_page_header(req, "Connecting", "status");

    httpd_resp_sendstr_chunk(req,
        "<div class='card'>"
        "<h2>Connecting...</h2>"
        "<p class='muted'>The ESP32 is trying to join your Wi-Fi network. If successful, this network will be added to saved networks.</p>"
        "<div class='pill'>Network: "
    );
    send_html_escaped(req, ssid);
    httpd_resp_sendstr_chunk(req,
        "</div>"
        "<a class='btn' href='/status'>Check status</a>"
        "</div>"
    );

    send_page_footer(req);

    return ESP_OK;
}

static esp_err_t status_page_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "HTTP GET /status");

    send_page_header(req, "Status", "status");

    httpd_resp_sendstr_chunk(req,
        "<div class='card'>"
        "<h2>Device status</h2>"
    );

    if (wifi_ap_is_sta_connected()) {
        httpd_resp_sendstr_chunk(req,
            "<p class='ok'>Connected</p>"
            "<div class='kv'><span>SSID</span><b>"
        );
        send_html_escaped(req, wifi_ap_get_sta_ssid());
        httpd_resp_sendstr_chunk(req, "</b></div><div class='kv'><span>IP address</span><b>");
        send_html_escaped(req, wifi_ap_get_sta_ip());
        httpd_resp_sendstr_chunk(req,
            "</b></div>"
            "<p class='muted'>This portal may close automatically after setup.</p>"
        );
    } else {
        httpd_resp_sendstr_chunk(req,
            "<p class='warn'>Not connected yet</p>"
            "<p class='muted'>If connection failed, check the password and try again.</p>"
            "<a class='btn' href='/scan'>Back to Wi-Fi setup</a>"
        );
    }

    httpd_resp_sendstr_chunk(req, "</div>");

    send_page_footer(req);

    return ESP_OK;
}

esp_err_t captive_portal_start(captive_portal_connect_cb_t connect_cb)
{
    if (s_server != NULL) {
        s_connect_cb = connect_cb;
        return ESP_OK;
    }

    s_connect_cb = connect_cb;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.uri_match_fn = httpd_uri_match_wildcard;

    /*
     * We register several explicit captive-portal probe endpoints in addition
     * to the app routes. The ESP-IDF default is small and can return
     * ESP_ERR_HTTPD_HANDLERS_FULL unless this is increased.
     */
    config.max_uri_handlers = 16;

    config.lru_purge_enable = true;
    config.max_open_sockets = 7;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;

    ESP_LOGI(TAG, "Starting HTTP portal");

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t scan_uri = {
        .uri = "/scan",
        .method = HTTP_GET,
        .handler = scan_page_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t refresh_uri = {
        .uri = "/refresh",
        .method = HTTP_GET,
        .handler = refresh_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t connect_uri = {
        .uri = "/connect",
        .method = HTTP_POST,
        .handler = connect_post_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t status_uri = {
        .uri = "/status",
        .method = HTTP_GET,
        .handler = status_page_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = scan_page_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t catch_all_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = catch_all_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = no_content_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t generate_204_uri = {
        .uri = "/generate_204",
        .method = HTTP_GET,
        .handler = captive_probe_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t gen_204_uri = {
        .uri = "/gen_204",
        .method = HTTP_GET,
        .handler = captive_probe_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t hotspot_uri = {
        .uri = "/hotspot-detect.html",
        .method = HTTP_GET,
        .handler = captive_probe_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t connecttest_uri = {
        .uri = "/connecttest.txt",
        .method = HTTP_GET,
        .handler = captive_probe_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t ncsi_uri = {
        .uri = "/ncsi.txt",
        .method = HTTP_GET,
        .handler = captive_probe_handler,
        .user_ctx = NULL,
    };

    const httpd_uri_t *uris[] = {
        &scan_uri,
        &refresh_uri,
        &connect_uri,
        &status_uri,
        &favicon_uri,
        &generate_204_uri,
        &gen_204_uri,
        &hotspot_uri,
        &connecttest_uri,
        &ncsi_uri,
        &root_uri,
        &catch_all_uri,
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        err = httpd_register_uri_handler(s_server, uris[i]);
        if (err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to register URI %s: %s",
                uris[i]->uri,
                esp_err_to_name(err)
            );

            httpd_stop(s_server);
            s_server = NULL;
            s_connect_cb = NULL;

            return err;
        }
    }

    ESP_LOGI(TAG, "HTTP portal started");

    return ESP_OK;
}

esp_err_t captive_portal_stop(void)
{
    if (s_server == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP portal");

    esp_err_t err = httpd_stop(s_server);

    s_server = NULL;
    s_connect_cb = NULL;

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop HTTP portal: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "HTTP portal stopped");

    return ESP_OK;
}
