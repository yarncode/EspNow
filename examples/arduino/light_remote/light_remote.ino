/**
 * @file light_remote.ino
 * @brief Light Remote Controller example (Arduino)
 *
 * A controller that pairs with the smart_light example.
 * It demonstrates:
 *   - Observing the light's state property
 *   - Sending control commands (on/off, brightness, color)
 *   - Sending scene events
 *
 * IMPORTANT: Replace LIGHT_MAC with the MAC printed by the smart_light sketch.
 */
#include <WiFi.h>
#include "EspNowNode.h"

// ╔══════════════════════════════════════════════════════════╗
// ║  REPLACE WITH THE MAC FROM YOUR SMART LIGHT NODE        ║
// ╚══════════════════════════════════════════════════════════╝
static const uint8_t LIGHT_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

static int demo = 0;

void setup() {
    Serial.begin(115200);

    auto& node = EspNowNode::instance();
    node.begin(1);

    // ── Observe light state ──
    node.observe(LIGHT_MAC, "light_state", [](EspNowNode::DataMap& data) {
        bool on          = data.getBool("on", false);
        int32_t bright   = data.getInt("brightness", 0);
        std::string col  = data.getString("color", "?");
        Serial.printf("Light state: on=%d brightness=%d color=%s\n",
                      on, (int)bright, col.c_str());
    });

    Serial.printf("Remote ready! Controlling light %s\n",
                  EspNow::macToString(LIGHT_MAC).c_str());
}

void loop() {
    delay(8000);
    demo++;

    auto& node = EspNowNode::instance();

    switch (demo % 5) {
        case 0:
            // Turn on with brightness 80
            node.control(LIGHT_MAC, "light")
                .set("on", true)
                .set("brightness", (int32_t)80)
                .send();
            Serial.println("=> Light ON, brightness 80");
            break;

        case 1:
            // Change color to warm
            node.control(LIGHT_MAC, "light")
                .set("color", std::string("warm"))
                .set("brightness", (int32_t)50)
                .send();
            Serial.println("=> Color warm, brightness 50");
            break;

        case 2:
            // Scene: movie mode
            node.emit(LIGHT_MAC, "scene")
                .set("name", std::string("movie"))
                .send();
            Serial.println("=> Scene: movie");
            break;

        case 3:
            // Scene: party mode
            node.emit(LIGHT_MAC, "scene")
                .set("name", std::string("party"))
                .send();
            Serial.println("=> Scene: party");
            break;

        case 4:
            // Turn off
            node.control(LIGHT_MAC, "light")
                .set("on", false)
                .send();
            Serial.println("=> Light OFF");
            break;
    }
}
