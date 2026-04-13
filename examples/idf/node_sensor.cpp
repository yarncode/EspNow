/**
 * @file node_sensor.cpp
 * @brief Example: ESP-NOW Sensor Node using EspNowNode high-level API.
 *
 * This device:
 *   - Registers event handlers ("bdk_1", "led_control")
 *   - Advertises properties ("temperature", "humidity")
 *   - Publishes sensor data every 5 seconds
 *   - Handles control commands ("pump") with method-chaining parsed values
 *
 * Build: Include as a main component in an ESP-IDF project.
 */

#include "EspNowNode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "SensorNode";

extern "C" void app_main(void) {
    auto& node = EspNowNode::instance();

    // ── Initialize & register handlers (method chaining) ──
    node.begin(1)
        // Event handlers — other devices can emit() to these
        .on("bdk_1", [](const uint8_t* mac, const uint8_t* data, int len) {
            ESP_LOGI(TAG, "Event 'bdk_1' received from %s, %d bytes",
                     EspNow::macToString(mac).c_str(), len);
            // Process the data...
        })
        .on("led_control", [](const uint8_t* mac, const uint8_t* data, int len) {
            if (len >= 1) {
                bool ledOn = data[0] != 0;
                ESP_LOGI(TAG, "LED %s (from %s)", ledOn ? "ON" : "OFF",
                         EspNow::macToString(mac).c_str());
                // gpio_set_level(LED_PIN, ledOn);
            }
        })

        // Control handler — parsed TLV key-value pairs, no manual packet parsing!
        .handle("pump", [](const uint8_t* mac, EspNowNode::ControlData& ctrl) {
            bool on     = ctrl.getBool("on", false);
            int32_t spd = ctrl.getInt("speed", 0);
            float pres  = ctrl.getFloat("pressure", 0.0f);

            ESP_LOGI(TAG, "Pump control from %s: on=%d speed=%d pressure=%.1f",
                     EspNow::macToString(mac).c_str(), on, (int)spd, pres);

            // Apply control: set pump state, PWM speed, etc.
        })

        // Advertise properties — other devices can observe() these
        .advertise("temperature")
        .advertise("humidity");

    ESP_LOGI(TAG, "Sensor node ready! Copy the MAC above to use in controller.");

    // ── Sensor loop: publish data every 5 seconds ──
    int tick = 0;
    while (true) {
        // Simulate sensor readings
        float temperature = 20.0f + 5.0f * sinf(tick * 0.1f);
        float humidity    = 50.0f + 10.0f * cosf(tick * 0.07f);

        // Publish to all observers — they receive it automatically
        node.publish("temperature", &temperature, sizeof(temperature));
        node.publish("humidity", &humidity, sizeof(humidity));

        ESP_LOGI(TAG, "Published: temp=%.1f°C, humidity=%.1f%%", temperature, humidity);

        tick++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
