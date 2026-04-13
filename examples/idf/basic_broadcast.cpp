/**
 * @file basic_broadcast.cpp
 * @brief Basic ESP-NOW broadcast example (ESP-IDF)
 *
 * Two devices with the same password can communicate via broadcast.
 * Flash this to both devices.
 */
#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "EspNow.h"

static const char *TAG = "example";

static void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    char buf[65];
    int show = len > 64 ? 64 : len;
    memcpy(buf, data, show);
    buf[show] = '\0';
    ESP_LOGI(TAG, "RX [%s] %d bytes: %s", EspNow::macToString(mac).c_str(), len, buf);
}

extern "C" void app_main(void) {
    auto &now = EspNow::instance();

    // Initialize on channel 6
    now.init(6);

    // Set password — both devices must match
    now.setPassword("my_secret");

    // Register receive callback
    now.onReceive(onReceive);

    ESP_LOGI(TAG, "MAC: %s", now.getMyMac().c_str());

    // Broadcast loop
    int counter = 0;
    while (true) {
        char msg[64];
        int len = snprintf(msg, sizeof(msg), "hello #%d", counter++);
        now.broadcast((const uint8_t *)msg, len);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
