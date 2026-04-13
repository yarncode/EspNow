/**
 * @file node_sensor.ino
 * @brief ESP-NOW Sensor Node using EspNowNode high-level API (Arduino)
 *
 * This sketch demonstrates a sensor node that:
 *   - Listens for named events ("bdk_1", "led_control")
 *   - Handles control commands ("pump") with typed key-value data
 *   - Advertises and publishes properties ("temperature", "humidity")
 *
 * All data is exchanged via typed key-value pairs — no manual byte manipulation.
 * Pair this with the node_controller example on another board.
 *
 * Note the MAC address printed at startup and enter it in the controller sketch.
 */
#include <WiFi.h>
#include "EspNowNode.h"

static int tick = 0;

void setup() {
    Serial.begin(115200);

    auto& node = EspNowNode::instance();

    node.begin(1)
        // ── Event handlers ──
        .on("bdk_1", [](const uint8_t* mac, EspNowNode::DataMap& data) {
            int32_t value = data.getInt("value", 0);
            bool state    = data.getBool("state", false);
            Serial.printf("Event 'bdk_1' from %s: value=%d, state=%d\n",
                          EspNow::macToString(mac).c_str(), (int)value, state);
        })

        .on("led_control", [](const uint8_t* mac, EspNowNode::DataMap& data) {
            bool ledOn = data.getBool("on", false);
            Serial.printf("LED %s (from %s)\n", ledOn ? "ON" : "OFF",
                          EspNow::macToString(mac).c_str());
            // digitalWrite(LED_BUILTIN, ledOn);
        })

        // ── Control handlers ──
        .handle("pump", [](const uint8_t* mac, EspNowNode::DataMap& ctrl) {
            bool on          = ctrl.getBool("on", false);
            int32_t speed    = ctrl.getInt("speed", 0);
            float pressure   = ctrl.getFloat("pressure", 0.0f);
            std::string mode = ctrl.getString("mode", "auto");

            Serial.printf("Pump from %s: on=%d speed=%d pressure=%.1f mode=%s\n",
                          EspNow::macToString(mac).c_str(), on, (int)speed,
                          pressure, mode.c_str());
        })

        // ── Advertise properties ──
        .advertise("temperature")
        .advertise("humidity");

    Serial.printf("Sensor node ready! MAC: %s\n",
                  EspNow::instance().getMyMac().c_str());
}

void loop() {
    float temperature = 20.0f + 5.0f * sin(tick * 0.1f);
    float humidity    = 50.0f + 10.0f * cos(tick * 0.07f);

    auto& node = EspNowNode::instance();

    node.publish("temperature")
        .set("value", temperature)
        .set("unit", std::string("C"))
        .send();

    node.publish("humidity")
        .set("value", humidity)
        .set("unit", std::string("%"))
        .send();

    Serial.printf("Published: temp=%.1f C, humidity=%.1f%%\n",
                  temperature, humidity);

    tick++;
    delay(5000);
}
