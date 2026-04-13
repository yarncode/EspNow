/**
 * @file doorbell_receiver.ino
 * @brief Doorbell Receiver example using EspNowNode (Arduino)
 *
 * Pairs with the doorbell.ino sketch. This board:
 *   - Listens for "ring" events (plays buzzer / lights LED)
 *   - Observes the doorbell's battery level
 *   - Can send "config" commands to change ring tone/volume
 *
 * Flash this to the indoor receiver board.
 * Connect a buzzer to GPIO 25 (optional) and an LED to GPIO 2.
 */
#include <WiFi.h>
#include "EspNowNode.h"

static const int BUZZER_PIN = 25;
static const int LED_PIN    = 2;

// ╔══════════════════════════════════════════════════════════╗
// ║  REPLACE WITH THE MAC FROM YOUR DOORBELL BOARD          ║
// ╚══════════════════════════════════════════════════════════╝
static const uint8_t DOORBELL_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

void playRing(const std::string& tone, int32_t volume) {
    Serial.printf("Playing ring: tone=%s volume=%d\n", tone.c_str(), (int)volume);
    // Simple LED flash as indicator
    for (int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    auto& node = EspNowNode::instance();
    node.begin(1);

    // ── Listen for ring events ──
    node.on("ring", [](const uint8_t* mac, EspNowNode::DataMap& data) {
        int32_t count    = data.getInt("count", 0);
        std::string tone = data.getString("tone", "default");
        int32_t vol      = data.getInt("volume", 80);

        Serial.printf("DOORBELL RING #%d from %s!\n",
                      (int)count, EspNow::macToString(mac).c_str());
        playRing(tone, vol);
    });

    // ── Observe doorbell battery ──
    node.observe(DOORBELL_MAC, "battery", [](EspNowNode::DataMap& data) {
        float voltage = data.getFloat("voltage", 0.0f);
        bool low      = data.getBool("low", false);
        Serial.printf("Doorbell battery: %.2fV %s\n", voltage,
                      low ? "⚠ LOW!" : "OK");
    });

    Serial.printf("Receiver ready! Listening for doorbell %s\n",
                  EspNow::macToString(DOORBELL_MAC).c_str());

    // ── Optional: configure the doorbell remotely ──
    node.control(DOORBELL_MAC, "config")
        .set("tone", std::string("melody"))
        .set("volume", (int32_t)90)
        .send();
    Serial.println("Sent doorbell config: tone=melody, volume=90");
}

void loop() {
    // The receiver just waits for events
    delay(100);
}
