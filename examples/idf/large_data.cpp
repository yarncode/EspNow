/**
 * @file large_data.cpp
 * @brief Large data transfer example (ESP-IDF)
 *
 * Demonstrates sending data larger than 250 bytes using auto-chunking.
 */
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "EspNow.h"

static const char *TAG = "example";

static void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    ESP_LOGI(TAG, "RX [%s] %d bytes (reassembled)", EspNow::macToString(mac).c_str(), len);
}

extern "C" void app_main(void) {
    auto &now = EspNow::instance();
    now.init(6);
    now.setPassword("my_secret");
    now.onReceive(onReceive);

    // Build a 1KB payload
    char payload[1024];
    memset(payload, 'A', sizeof(payload));
    snprintf(payload, 64, "Large payload (%d bytes)", (int)sizeof(payload));

    while (true) {
        // Auto-chunks into ~5 packets, receiver reassembles
        now.broadcastLarge((const uint8_t *)payload, sizeof(payload));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
