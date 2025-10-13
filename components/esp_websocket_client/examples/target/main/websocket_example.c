/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* ESP WebSocket Client - CUSTOMER'S DOUBLE-FREE BUG REPRODUCTION
 *
 * ⚠️  THIS REPRODUCES THE EXACT CUSTOMER SCENARIO ⚠️
 *
 * CUSTOMER'S BUG:
 * ==============
 * When send fails due to network error, abort_connection() is called.
 * Due to LAYERED TRANSPORTS (WebSocket → SSL → TCP), esp_transport_close()
 * gets called MULTIPLE TIMES in the SAME call stack!
 *
 * THE LAYERED TRANSPORT PROBLEM:
 * ==============================
 * WebSocket transport wraps SSL transport:
 *   esp_transport_close(ws_transport)
 *     → ws_close()
 *       → esp_transport_close(ssl_transport)  ← 1st close, frees TLS
 *   [returns to error path]
 *   → esp_transport_close() called again!     ← 2nd close, DOUBLE-FREE!
 *
 * CUSTOMER'S EXACT BACKTRACE:
 * ==========================
 * assert failed: tlsf_free tlsf.c:629 (!block_is_free(block))
 *
 * esp_websocket_client_send_text (send fails)
 *   → esp_websocket_client_abort_connection (line 240)
 *     → esp_transport_close (transport.c:172) ← 1st appearance
 *       → ws_close (transport_ws.c:680)
 *         → esp_transport_close (transport.c:172) ← 2nd appearance (DOUBLE!)
 *           → base_close → esp_tls_conn_destroy → free(tls) ✓
 *     [error path continues...]
 *     → esp_transport_close called AGAIN
 *       → base_close → esp_tls_conn_destroy → free(tls) 💥 DOUBLE-FREE!
 *
 * HOW TO REPRODUCE (SINGLE-THREADED):
 * ===================================
 * 1. Connect to WebSocket server
 * 2. Flood with LARGE messages to fill buffers
 * 3. Server closes connection (or kill server)
 * 4. ESP32 tries to send → network error
 * 5. abort_connection() called
 * 6. Layered transport causes double esp_transport_close()
 * 7. 💥 CRASH: Double-free!
 *
 * THE FIX:
 * ========
 * Add idempotency to esp_transport_close() in transport.c:
 *
 * int esp_transport_close(esp_transport_handle_t t) {
 *     if (t == NULL) return 0;
 *     if (t->_closed) return 0;  // ← ADD THIS!
 *     int ret = t->_close ? t->_close(t) : 0;
 *     t->_closed = true;         // ← AND THIS!
 *     return ret;
 * }
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
*/


#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "esp_crt_bundle.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include "esp_timer.h"
#include <cJSON.h>

#define NO_DATA_TIMEOUT_SEC 10
#define CRASH_TEST_INTERVAL_MS 3000  // Try to trigger crash every 3 seconds
#define AGGRESSIVE_SEND_COUNT 50     // Send many messages rapidly to stress the transport

static const char *TAG = "websocket";

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void shutdown_signaler(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC);
    xSemaphoreGive(shutdown_sema);
}

// Helper function to simulate customer's scenario: send fails → triggers double-close
static void simulate_customer_crash_scenario(esp_websocket_client_handle_t client)
{
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔═══════════════════════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║  ⚠️  SIMULATING CUSTOMER'S EXACT CRASH SCENARIO ⚠️        ║");
    ESP_LOGW(TAG, "╚═══════════════════════════════════════════════════════════╝");
    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "🎯 SCENARIO:");
    ESP_LOGW(TAG, "   1. Flood with large messages to fill buffers");
    ESP_LOGW(TAG, "   2. Continue sending while buffers are full");
    ESP_LOGW(TAG, "   3. Send will fail → timeout/error");
    ESP_LOGW(TAG, "   4. abort_connection() called");
    ESP_LOGW(TAG, "   5. esp_transport_close(ws_transport)");
    ESP_LOGW(TAG, "      → ws_close() → esp_transport_close(ssl_transport) [1st]");
    ESP_LOGW(TAG, "      → [error path] → esp_transport_close(ssl_transport) [2nd]");
    ESP_LOGW(TAG, "   6. 💥 DOUBLE-FREE!");
    ESP_LOGW(TAG, "");

    // Step 1: Flood to fill all buffers
    ESP_LOGI(TAG, "Step 1: Flooding with 100 x 1KB messages...");
    for (int i = 0; i < 100; i++) {
        char *huge = malloc(1024);
        if (huge) {
            memset(huge, 'X', 1023);
            snprintf(huge, 32, "FLOOD_%04d_", i);
            huge[1023] = '\0';

            // Use VERY short timeout to force failures
            int ret = esp_websocket_client_send_text(client, huge, 1023, 1);  // 1ms timeout
            if (ret < 0) {
                ESP_LOGE(TAG, "   💥 Send failed at message %d! (ret=%d)", i, ret);
                free(huge);
                break;
            }
            free(huge);
        }

        // No delay - flood as fast as possible
        if (i % 20 == 0) {
            ESP_LOGI(TAG, "   Sent %d messages...", i);
        }
    }

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "Step 2: Keep sending to trigger timeout/error...");
    ESP_LOGW(TAG, "        (This simulates customer's ongoing send operations)");

    // Keep trying to send - this will eventually timeout/fail
    for (int i = 0; i < 50; i++) {
        char data[512];
        snprintf(data, sizeof(data), "KEEP_SENDING_%04d", i);

        int ret = esp_websocket_client_send_text(client, data, strlen(data), 10);  // 10ms timeout
        if (ret < 0) {
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "💥💥💥 SEND FAILED! This triggers abort_connection()");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "CALL STACK (predicted):");
            ESP_LOGE(TAG, "  esp_websocket_client_send_text");
            ESP_LOGE(TAG, "    → send fails with timeout/error");
            ESP_LOGE(TAG, "    → esp_websocket_client_abort_connection()");
            ESP_LOGE(TAG, "      → esp_transport_close(client->transport) [ws_transport]");
            ESP_LOGE(TAG, "        → ws_close()");
            ESP_LOGE(TAG, "          → esp_transport_close(parent) [ssl_transport] ✓ 1st close");
            ESP_LOGE(TAG, "      → [continues in abort_connection error path]");
            ESP_LOGE(TAG, "      → esp_transport_close() CALLED AGAIN ← 💥 DOUBLE!");
            ESP_LOGE(TAG, "");
            ESP_LOGE(TAG, "⏰ WAITING 3 SECONDS FOR CRASH...");
            ESP_LOGE(TAG, "");
            break;
        }
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    // Wait for crash
    vTaskDelay(3000 / portTICK_PERIOD_MS);
}

#if CONFIG_WEBSOCKET_URI_FROM_STDIN
static void get_string(char *line, size_t size)
{
    int count = 0;
    while (count < size) {
        int c = fgetc(stdin);
        if (c == '\n') {
            line[count] = '\0';
            break;
        } else if (c > 0 && c < 127) {
            line[count] = c;
            ++count;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

#endif /* CONFIG_WEBSOCKET_URI_FROM_STDIN */

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
    case WEBSOCKET_EVENT_BEGIN:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_BEGIN");
        break;
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
        ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
        if (data->op_code == 0x2) { // Opcode 0x2 indicates binary data
            ESP_LOG_BUFFER_HEX("Received binary data", data->data_ptr, data->data_len);
        } else if (data->op_code == 0x08 && data->data_len == 2) {
            ESP_LOGW(TAG, "Received closed message with code=%d", 256 * data->data_ptr[0] + data->data_ptr[1]);
        } else {
            ESP_LOGW(TAG, "Received=%.*s\n\n", data->data_len, (char *)data->data_ptr);
        }

        // If received data contains json structure it succeed to parse
        cJSON *root = cJSON_Parse(data->data_ptr);
        if (root) {
            for (int i = 0 ; i < cJSON_GetArraySize(root) ; i++) {
                cJSON *elem = cJSON_GetArrayItem(root, i);
                cJSON *id = cJSON_GetObjectItem(elem, "id");
                cJSON *name = cJSON_GetObjectItem(elem, "name");
                ESP_LOGW(TAG, "Json={'id': '%s', 'name': '%s'}", id->valuestring, name->valuestring);
            }
            cJSON_Delete(root);
        }

        ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);

        xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGW(TAG, "*** WEBSOCKET_EVENT_ERROR - Error path will call esp_transport_close() internally ***");
        log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
        if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
        }
        break;
    case WEBSOCKET_EVENT_FINISH:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_FINISH");
        break;
    }
}

static void websocket_app_start(void)
{
    esp_websocket_client_config_t websocket_cfg = {};

    // IMPORTANT: Disable auto-reconnect to simplify crash scenario
    websocket_cfg.disable_auto_reconnect = true;

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, NULL, shutdown_signaler);
    shutdown_sema = xSemaphoreCreateBinary();

#if CONFIG_WEBSOCKET_URI_FROM_STDIN
    char line[128];

    ESP_LOGI(TAG, "Please enter WebSocket endpoint URI");
    ESP_LOGI(TAG, "Examples:");
    ESP_LOGI(TAG, "  ws://192.168.1.100:8080     (plain WebSocket)");
    ESP_LOGI(TAG, "  wss://192.168.1.100:8080    (secure WebSocket)");
    ESP_LOGI(TAG, "  wss://echo.websocket.org    (public test server)");
    get_string(line, sizeof(line));

    websocket_cfg.uri = line;
    ESP_LOGI(TAG, "Endpoint uri: %s\n", line);

#else
    websocket_cfg.uri = CONFIG_WEBSOCKET_URI;
#endif /* CONFIG_WEBSOCKET_URI_FROM_STDIN */

#if CONFIG_WS_OVER_TLS_MUTUAL_AUTH
    /* Configuring client certificates for mutual authentification */
    extern const char cacert_start[] asm("_binary_ca_cert_pem_start"); // CA certificate
    extern const char cert_start[] asm("_binary_client_cert_pem_start"); // Client certificate
    extern const char cert_end[]   asm("_binary_client_cert_pem_end");
    extern const char key_start[] asm("_binary_client_key_pem_start"); // Client private key
    extern const char key_end[]   asm("_binary_client_key_pem_end");

    websocket_cfg.cert_pem = cacert_start;
    websocket_cfg.client_cert = cert_start;
    websocket_cfg.client_cert_len = cert_end - cert_start;
    websocket_cfg.client_key = key_start;
    websocket_cfg.client_key_len = key_end - key_start;
#elif CONFIG_WS_OVER_TLS_SERVER_AUTH
    // Using certificate bundle as default server certificate source
    websocket_cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

#if CONFIG_WS_OVER_TLS_SKIP_COMMON_NAME_CHECK
    websocket_cfg.skip_cert_common_name_check = true;
#endif

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    esp_websocket_client_handle_t client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    ESP_LOGW(TAG, "");
    ESP_LOGW(TAG, "╔═══════════════════════════════════════════════════════════╗");
    ESP_LOGW(TAG, "║         CUSTOMER'S DOUBLE-FREE BUG REPRODUCTION          ║");
    ESP_LOGW(TAG, "║              SINGLE-THREADED TEST                        ║");
    ESP_LOGW(TAG, "╚═══════════════════════════════════════════════════════════╝");
    ESP_LOGW(TAG, "");

    int cycle = 0;
    while (true) {
        cycle++;
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "═══════════════════════════════════════════════════════════");
        ESP_LOGW(TAG, "   TEST CYCLE %d", cycle);
        ESP_LOGW(TAG, "═══════════════════════════════════════════════════════════");

        // Start client
        ESP_LOGI(TAG, "Starting WebSocket client...");
        esp_websocket_client_start(client);
        xTimerStart(shutdown_signal_timer, portMAX_DELAY);

        // Wait for connection
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        if (!esp_websocket_client_is_connected(client)) {
            ESP_LOGE(TAG, "❌ Failed to connect. Retrying in 5 seconds...");
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "✅ Connected!");

        // Send a few normal messages first
        for (int i = 0; i < 3; i++) {
            char hello[32];
            snprintf(hello, sizeof(hello), "hello_%d", i);
            esp_websocket_client_send_text(client, hello, strlen(hello), portMAX_DELAY);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }

        // NOW SIMULATE THE CUSTOMER'S SCENARIO
        simulate_customer_crash_scenario(client);

        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "If no crash occurred, stopping client and restarting...");
        esp_websocket_client_stop(client);
        vTaskDelay(3000 / portTICK_PERIOD_MS);

        // Check for shutdown signal
        if (xSemaphoreTake(shutdown_sema, 0) == pdTRUE) {
            break;
        }
    }

    esp_websocket_client_destroy(client);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("websocket_client", ESP_LOG_DEBUG);
    esp_log_level_set("transport_ws", ESP_LOG_DEBUG);
    esp_log_level_set("trans_tcp", ESP_LOG_DEBUG);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    websocket_app_start();
}
