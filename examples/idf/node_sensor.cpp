/**
 * @file node_sensor.cpp
 * @brief Example: ESP-NOW Sensor Node using EspNowNode high-level API.
 *
 * All data is exchanged via typed key-value pairs (DataMap / MessageBuilder).
 * No manual byte arrays needed!
 */

#include "EspNowNode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cmath>

static const char* TAG = "SensorNode";

extern "C" void app_main(void) {
    auto& node = EspNowNode::instance();

    node.begin(1)
        // ── Event: nhận dữ liệu typed từ thiết bị khác ──
        .on("bdk_1", [](const uint8_t* mac, EspNowNode::DataMap& data) {
            int32_t value = data.getInt("value", 0);
            bool state    = data.getBool("state", false);
            ESP_LOGI(TAG, "Event 'bdk_1' from %s: value=%d, state=%d",
                     EspNow::macToString(mac).c_str(), (int)value, state);
        })

        .on("led_control", [](const uint8_t* mac, EspNowNode::DataMap& data) {
            bool ledOn = data.getBool("on", false);
            ESP_LOGI(TAG, "LED %s (from %s)", ledOn ? "ON" : "OFF",
                     EspNow::macToString(mac).c_str());
            // gpio_set_level(LED_PIN, ledOn);
        })

        // ── Control: nhận lệnh điều khiển với key-value ──
        .handle("pump", [](const uint8_t* mac, EspNowNode::DataMap& ctrl) {
            bool on      = ctrl.getBool("on", false);
            int32_t spd  = ctrl.getInt("speed", 0);
            float pres   = ctrl.getFloat("pressure", 0.0f);
            std::string mode = ctrl.getString("mode", "auto");

            ESP_LOGI(TAG, "Pump from %s: on=%d speed=%d pressure=%.1f mode=%s",
                     EspNow::macToString(mac).c_str(), on, (int)spd, pres, mode.c_str());
        })

        // ── Advertise: khai báo property có thể observe ──
        .advertise("temperature")
        .advertise("humidity");

    ESP_LOGI(TAG, "Sensor node ready! Copy the MAC above to use in controller.");

    // ── Sensor loop: publish typed data ──
    int tick = 0;
    while (true) {
        float temperature = 20.0f + 5.0f * sinf(tick * 0.1f);
        float humidity    = 50.0f + 10.0f * cosf(tick * 0.07f);

        // Publish method chaining — observers tự nhận DataMap
        node.publish("temperature")
            .set("value", temperature)
            .set("unit", std::string("C"))
            .send();

        node.publish("humidity")
            .set("value", humidity)
            .set("unit", std::string("%"))
            .send();

        ESP_LOGI(TAG, "Published: temp=%.1f C, humidity=%.1f%%", temperature, humidity);

        tick++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
