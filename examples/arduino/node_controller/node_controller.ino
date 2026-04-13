/**
 * @file node_controller.ino
 * @brief ESP-NOW Controller Node using EspNowNode high-level API (Arduino)
 *
 * This sketch demonstrates a controller node that:
 *   - Observes properties published by the sensor node (temperature, humidity)
 *   - Emits events to the sensor node (bdk_1, led_control)
 *   - Sends control commands to the sensor node (pump)
 *
 * IMPORTANT: Replace SENSOR_MAC with the MAC printed by the sensor node.
 */
#include <WiFi.h>
#include "EspNowNode.h"

// ╔══════════════════════════════════════════════════════════╗
// ║  REPLACE WITH THE MAC FROM YOUR SENSOR NODE             ║
// ║  (copy from Serial: "Sensor node ready! MAC: ...")      ║
// ╚══════════════════════════════════════════════════════════╝
static const uint8_t SENSOR_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

static int tick = 0;

void setup() {
    Serial.begin(115200);

    auto& node = EspNowNode::instance();
    node.begin(1);

    // ── Observe sensor properties ──
    node.observe(SENSOR_MAC, "temperature", [](EspNowNode::DataMap& data) {
        float temp = data.getFloat("value", 0.0f);
        std::string unit = data.getString("unit", "C");
        Serial.printf("Temperature: %.1f %s\n", temp, unit.c_str());
    });

    node.observe(SENSOR_MAC, "humidity", [](EspNowNode::DataMap& data) {
        float hum = data.getFloat("value", 0.0f);
        std::string unit = data.getString("unit", "%");
        Serial.printf("Humidity: %.1f %s\n", hum, unit.c_str());
    });

    Serial.printf("Controller ready! Observing sensor %s\n",
                  EspNow::macToString(SENSOR_MAC).c_str());
}

void loop() {
    delay(10000);
    tick++;

    auto& node = EspNowNode::instance();

    // ── Emit event with typed data ──
    node.emit(SENSOR_MAC, "bdk_1")
        .set("value", (int32_t)tick)
        .set("state", true)
        .send();
    Serial.printf("Emitted 'bdk_1': value=%d, state=true\n", tick);

    // ── Toggle LED ──
    bool ledOn = (tick % 2 == 0);
    node.emit(SENSOR_MAC, "led_control")
        .set("on", ledOn)
        .send();
    Serial.printf("LED -> %s\n", ledOn ? "ON" : "OFF");

    // ── Control pump ──
    node.control(SENSOR_MAC, "pump")
        .set("on", true)
        .set("speed", (int32_t)(tick * 10))
        .set("pressure", 2.5f)
        .set("mode", std::string("manual"))
        .send();
    Serial.printf("Pump: on=true, speed=%d, mode=manual\n", tick * 10);
}
