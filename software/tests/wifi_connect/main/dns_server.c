#include "dns_server.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "dns_server";

#define DNS_SERVER_PORT 53
#define DNS_TASK_STACK_SIZE 4096
#define DNS_MAX_PACKET_SIZE 512

static TaskHandle_t s_dns_task_handle = NULL;
static int s_dns_socket = -1;
static uint32_t s_redirect_ip_addr = 0;
static bool s_dns_running = false;

static int dns_skip_name(const uint8_t *packet, int packet_len, int offset)
{
    while (offset < packet_len) {
        uint8_t len = packet[offset];

        if (len == 0) {
            return offset + 1;
        }

        /*
         * Compression pointer.
         * We do not expect it in questions, but handle it defensively.
         */
        if ((len & 0xC0) == 0xC0) {
            return offset + 2;
        }

        offset += 1 + len;
    }

    return -1;
}

static int dns_build_response(
    const uint8_t *query,
    int query_len,
    uint8_t *response,
    int response_max_len
)
{
    if (query_len < 12 || response_max_len < query_len + 16) {
        return -1;
    }

    /*
     * DNS header:
     * 0-1: ID
     * 2-3: flags
     * 4-5: QDCOUNT
     * 6-7: ANCOUNT
     * 8-9: NSCOUNT
     * 10-11: ARCOUNT
     */
    uint16_t qdcount = ((uint16_t)query[4] << 8) | query[5];
    if (qdcount == 0) {
        return -1;
    }

    int qname_end = dns_skip_name(query, query_len, 12);
    if (qname_end < 0 || qname_end + 4 > query_len) {
        return -1;
    }

    uint16_t qtype = ((uint16_t)query[qname_end] << 8) | query[qname_end + 1];
    uint16_t qclass = ((uint16_t)query[qname_end + 2] << 8) | query[qname_end + 3];

    int question_len = qname_end + 4 - 12;
    int response_len = 12 + question_len;

    if (response_len + 16 > response_max_len) {
        return -1;
    }

    memset(response, 0, response_max_len);

    /*
     * Copy transaction ID.
     */
    response[0] = query[0];
    response[1] = query[1];

    /*
     * Flags:
     * QR=1 response
     * Opcode=0
     * AA=1 authoritative
     * TC=0
     * RD copied from query
     * RA=0
     * RCODE=0
     */
    response[2] = 0x84 | (query[2] & 0x01);
    response[3] = 0x00;

    /*
     * One question, one answer.
     */
    response[4] = 0x00;
    response[5] = 0x01;
    response[6] = 0x00;
    response[7] = 0x01;
    response[8] = 0x00;
    response[9] = 0x00;
    response[10] = 0x00;
    response[11] = 0x00;

    memcpy(&response[12], &query[12], question_len);

    int answer_offset = response_len;

    /*
     * NAME: pointer to original QNAME at offset 12.
     */
    response[answer_offset++] = 0xC0;
    response[answer_offset++] = 0x0C;

    /*
     * TYPE and CLASS: mirror the request. For captive portal use, A/IN is the
     * important one. Other query types still receive the same simple answer.
     */
    response[answer_offset++] = (uint8_t)(qtype >> 8);
    response[answer_offset++] = (uint8_t)(qtype & 0xFF);
    response[answer_offset++] = (uint8_t)(qclass >> 8);
    response[answer_offset++] = (uint8_t)(qclass & 0xFF);

    /*
     * TTL: 60 seconds.
     */
    response[answer_offset++] = 0x00;
    response[answer_offset++] = 0x00;
    response[answer_offset++] = 0x00;
    response[answer_offset++] = 0x3C;

    /*
     * RDLENGTH + RDATA.
     *
     * For non-A queries this is not a standards-complete DNS response, but it is
     * enough for common captive portal probes. Most devices ask A records for
     * HTTP connectivity-check hosts.
     */
    response[answer_offset++] = 0x00;
    response[answer_offset++] = 0x04;

    memcpy(&response[answer_offset], &s_redirect_ip_addr, 4);
    answer_offset += 4;

    return answer_offset;
}

static void dns_server_task(void *arg)
{
    struct sockaddr_in server_addr = {0};

    s_dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s_dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create DNS socket");
        s_dns_running = false;
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    int reuse = 1;
    setsockopt(s_dns_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DNS_SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s_dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind DNS socket");
        closesocket(s_dns_socket);
        s_dns_socket = -1;
        s_dns_running = false;
        s_dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS captive redirect server started on UDP port %d", DNS_SERVER_PORT);

    uint8_t query[DNS_MAX_PACKET_SIZE];
    uint8_t response[DNS_MAX_PACKET_SIZE];

    while (s_dns_running) {
        struct sockaddr_in client_addr = {0};
        socklen_t client_len = sizeof(client_addr);

        int query_len = recvfrom(
            s_dns_socket,
            query,
            sizeof(query),
            0,
            (struct sockaddr *)&client_addr,
            &client_len
        );

        if (query_len <= 0) {
            continue;
        }

        int response_len = dns_build_response(
            query,
            query_len,
            response,
            sizeof(response)
        );

        if (response_len <= 0) {
            continue;
        }

        sendto(
            s_dns_socket,
            response,
            response_len,
            0,
            (struct sockaddr *)&client_addr,
            client_len
        );
    }

    if (s_dns_socket >= 0) {
        closesocket(s_dns_socket);
        s_dns_socket = -1;
    }

    ESP_LOGI(TAG, "DNS captive redirect server stopped");

    s_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(const char *redirect_ip)
{
    if (s_dns_task_handle != NULL || s_dns_running) {
        return ESP_OK;
    }

    if (redirect_ip == NULL || redirect_ip[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    s_redirect_ip_addr = inet_addr(redirect_ip);
    if (s_redirect_ip_addr == INADDR_NONE) {
        ESP_LOGE(TAG, "Invalid redirect IP: %s", redirect_ip);
        return ESP_ERR_INVALID_ARG;
    }

    s_dns_running = true;

    BaseType_t ok = xTaskCreate(
        dns_server_task,
        "dns_server",
        DNS_TASK_STACK_SIZE,
        NULL,
        5,
        &s_dns_task_handle
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS task");
        s_dns_running = false;
        s_dns_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    s_dns_running = false;

    if (s_dns_socket >= 0) {
        shutdown(s_dns_socket, 0);
        closesocket(s_dns_socket);
        s_dns_socket = -1;
    }

    return ESP_OK;
}
