/**
 * @file multi_sensor.ino
 * @brief Multi-Sensor Hub example using EspNowNode (Arduino)
 *
 * A single ESP32 that publishes multiple sensor readings:
 *   - Temperature & humidity (simulated DHT)
 *   - Soil moisture
 *   - Light level
 *   - Battery voltage
 *
 * Any number of controllers can observe any subset of these properties.
 * Also handles a "config" control to change the report interval remotely.
 */
#include <WiFi.h>
#include "EspNowNode.h"

static int      tick       = 0;
static int32_t  intervalMs = 5000;   // Default report interval

void setup() {
    Serial.begin(115200);

    auto& node = EspNowNode::instance();

    node.begin(1)
        // ── Remote config: change report interval ──
        .handle("config", [](const uint8_t* mac, EspNowNode::DataMap& ctrl) {
            if (ctrl.hasKey("interval")) {
                intervalMs = ctrl.getInt("interval", 5000);
                Serial.printf("Config from %s: interval=%d ms\n",
                              EspNow::macToString(mac).c_str(), (int)intervalMs);
            }
        })

        // ── Advertise all sensor properties ──
        .advertise("temperature")
        .advertise("humidity")
        .advertise("soil")
        .advertise("light")
        .advertise("battery");

    Serial.printf("Multi-Sensor Hub ready! MAC: %s\n",
                  EspNow::instance().getMyMac().c_str());
}

void loop() {
    auto& node = EspNowNode::instance();

    // Simulated sensor readings
    float temp     = 22.0f + 3.0f * sin(tick * 0.1f);
    float humidity  = 55.0f + 15.0f * cos(tick * 0.07f);
    int32_t soil   = 400 + (int32_t)(200.0f * sin(tick * 0.05f));
    int32_t light  = 500 + (int32_t)(300.0f * cos(tick * 0.03f));
    float battery  = 3.3f + 0.5f * cos(tick * 0.01f);

    node.publish("temperature")
        .set("value", temp)
        .set("unit", std::string("C"))
        .send();

    node.publish("humidity")
        .set("value", humidity)
        .set("unit", std::string("%"))
        .send();

    node.publish("soil")
        .set("raw", soil)
        .set("percent", (int32_t)map(soil, 200, 800, 0, 100))
        .send();

    node.publish("light")
        .set("lux", light)
        .send();

    node.publish("battery")
        .set("voltage", battery)
        .set("low", battery < 3.3f)
        .send();

    Serial.printf("Published: T=%.1f H=%.1f Soil=%d Light=%d Bat=%.2fV\n",
                  temp, humidity, (int)soil, (int)light, battery);

    tick++;
    delay(intervalMs);
}
