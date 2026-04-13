/**
 * @file smart_light.ino
 * @brief Smart Light example using EspNowNode (Arduino)
 *
 * Demonstrates a smart light that:
 *   - Handles "light" control commands (on/off, brightness, color)
 *   - Publishes its current state as a property for observers
 *   - Responds to events ("scene") to switch lighting modes
 *
 * Pair this with a controller that sends control commands & observes state.
 */
#include <WiFi.h>
#include "EspNowNode.h"

// ── Light state ──
static bool    lightOn     = false;
static int32_t brightness  = 100;
static std::string color   = "white";

void publishState() {
    EspNowNode::instance().publish("light_state")
        .set("on", lightOn)
        .set("brightness", brightness)
        .set("color", color)
        .send();
}

void setup() {
    Serial.begin(115200);

    auto& node = EspNowNode::instance();

    node.begin(1)
        // ── Control: turn on/off, set brightness, set color ──
        .handle("light", [](const uint8_t* mac, EspNowNode::DataMap& ctrl) {
            if (ctrl.hasKey("on"))         lightOn    = ctrl.getBool("on");
            if (ctrl.hasKey("brightness")) brightness = ctrl.getInt("brightness");
            if (ctrl.hasKey("color"))      color      = ctrl.getString("color");

            Serial.printf("Light from %s: on=%d bright=%d color=%s\n",
                          EspNow::macToString(mac).c_str(),
                          lightOn, (int)brightness, color.c_str());

            // Apply to hardware:
            // analogWrite(LED_PIN, lightOn ? brightness : 0);

            // Publish updated state to all observers
            publishState();
        })

        // ── Event: scene presets ──
        .on("scene", [](const uint8_t* mac, EspNowNode::DataMap& data) {
            std::string sceneName = data.getString("name", "default");
            Serial.printf("Scene '%s' from %s\n", sceneName.c_str(),
                          EspNow::macToString(mac).c_str());

            if (sceneName == "movie") {
                lightOn = true; brightness = 30; color = "warm";
            } else if (sceneName == "reading") {
                lightOn = true; brightness = 80; color = "white";
            } else if (sceneName == "party") {
                lightOn = true; brightness = 100; color = "rainbow";
            } else if (sceneName == "off") {
                lightOn = false;
            }

            publishState();
        })

        // ── Advertise current state ──
        .advertise("light_state");

    Serial.printf("Smart Light ready! MAC: %s\n",
                  EspNow::instance().getMyMac().c_str());

    // Publish initial state
    publishState();
}

void loop() {
    // Could add button input, animation, etc. here
    delay(100);
}
