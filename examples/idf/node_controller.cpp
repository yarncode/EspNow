/**
 * @file node_controller.cpp
 * @brief Example: ESP-NOW Controller Node using EspNowNode high-level API.
 *
 * All data is exchanged via typed key-value pairs (DataMap / MessageBuilder).
 * No manual byte arrays or memcpy needed!
 *
 * IMPORTANT: Replace SENSOR_MAC with the MAC printed by the sensor node.
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
    node.begin(1);

    // ── Observe: nhận dữ liệu typed từ sensor ──
    node.observe(SENSOR_MAC, "temperature", [](EspNowNode::DataMap& data) {
        float temp = data.getFloat("value", 0.0f);
        std::string unit = data.getString("unit", "C");
        ESP_LOGI(TAG, "Temperature: %.1f %s", temp, unit.c_str());
    });

    node.observe(SENSOR_MAC, "humidity", [](EspNowNode::DataMap& data) {
        float hum = data.getFloat("value", 0.0f);
        std::string unit = data.getString("unit", "%");
        ESP_LOGI(TAG, "Humidity: %.1f %s", hum, unit.c_str());
    });

    ESP_LOGI(TAG, "Controller ready, observing sensor %s",
             EspNow::macToString(SENSOR_MAC).c_str());

    int tick = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        tick++;

        // ── Emit event: gửi typed data đến sensor ──
        node.emit(SENSOR_MAC, "bdk_1")
            .set("value", (int32_t)tick)
            .set("state", true)
            .send();
        ESP_LOGI(TAG, "Emitted 'bdk_1': value=%d, state=true", tick);

        // ── Toggle LED: chỉ cần set key "on" ──
        bool ledOn = (tick % 2 == 0);
        node.emit(SENSOR_MAC, "led_control")
            .set("on", ledOn)
            .send();
        ESP_LOGI(TAG, "LED -> %s", ledOn ? "ON" : "OFF");

        // ── Smart control: method chaining ──
        node.control(SENSOR_MAC, "pump")
            .set("on", true)
            .set("speed", (int32_t)(tick * 10))
            .set("pressure", 2.5f)
            .set("mode", std::string("manual"))
            .send();
        ESP_LOGI(TAG, "Pump control: on=true, speed=%d, mode=manual", tick * 10);
    }
}
