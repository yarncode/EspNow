/**
 * @file node_controller.cpp
 * @brief Example: ESP-NOW Controller Node using EspNowNode high-level API.
 *
 * This device:
 *   - Observes "temperature" and "humidity" from a sensor node
 *   - Emits events ("bdk_1", "led_control") to the sensor
 *   - Sends smart control commands ("pump") with method chaining
 *
 * IMPORTANT: Replace SENSOR_MAC with the actual MAC address printed
 *            by the sensor node during initialization.
 *
 * Build: Include as a main component in an ESP-IDF project.
 */

#include "EspNowNode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "ControllerNode";

// ╔══════════════════════════════════════════════════════════╗
// ║  THAY ĐỔI MAC NÀY BẰNG MAC CỦA SENSOR NODE            ║
// ║  (copy từ log "DEVICE MAC: XX:XX:XX:XX:XX:XX")          ║
// ╚══════════════════════════════════════════════════════════╝
static const uint8_t SENSOR_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

extern "C" void app_main(void) {
    auto& node = EspNowNode::instance();

    // ── Initialize ──
    node.begin(1);

    // ── Observe sensor properties ──
    // Callback fires whenever the sensor publishes new data
    node.observe(SENSOR_MAC, "temperature", [](const uint8_t* data, int len) {
        if (len >= (int)sizeof(float)) {
            float temp;
            memcpy(&temp, data, sizeof(temp));
            ESP_LOGI(TAG, "🌡  Temperature changed: %.1f°C", temp);
        }
    });

    node.observe(SENSOR_MAC, "humidity", [](const uint8_t* data, int len) {
        if (len >= (int)sizeof(float)) {
            float hum;
            memcpy(&hum, data, sizeof(hum));
            ESP_LOGI(TAG, "💧 Humidity changed: %.1f%%", hum);
        }
    });

    ESP_LOGI(TAG, "Controller ready, observing sensor %s",
             EspNow::macToString(SENSOR_MAC).c_str());

    // ── Demo loop ──
    int tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        tick++;

        // ── Example 1: Emit event to sensor ──
        // Sensor's .on("bdk_1") handler will be triggered
        uint8_t eventData[] = {0x01, 0x02, 0x03};
        node.emit(SENSOR_MAC, "bdk_1", eventData, sizeof(eventData));
        ESP_LOGI(TAG, "Emitted event 'bdk_1' to sensor");

        // ── Example 2: Toggle LED via event ──
        uint8_t ledState = (tick % 2 == 0) ? 1 : 0;
        node.emit(SENSOR_MAC, "led_control", &ledState, 1);
        ESP_LOGI(TAG, "Emitted 'led_control' -> LED %s", ledState ? "ON" : "OFF");

        // ── Example 3: Smart control command (method chaining!) ──
        // Sensor's .handle("pump") receives parsed ControlData
        // No manual packet construction needed!
        node.control(SENSOR_MAC, "pump")
            .set("on", true)
            .set("speed", (int32_t)(tick * 10))
            .set("pressure", 2.5f)
            .send();

        ESP_LOGI(TAG, "Sent pump control: on=true, speed=%d, pressure=2.5", tick * 10);
    }
}
