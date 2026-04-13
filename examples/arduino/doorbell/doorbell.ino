/**
 * @file doorbell.ino
 * @brief Wireless Doorbell example using EspNowNode (Arduino)
 *
 * Two-board setup:
 *   - DOORBELL (this sketch): sends a "ring" event when button is pressed,
 *     publishes battery status, handles "config" commands.
 *   - RECEIVER (doorbell_receiver.ino): plays sound/lights up on "ring" event,
 *     observes battery, can configure the doorbell remotely.
 *
 * Flash this to the outdoor doorbell button board.
 * Connect a button between GPIO 0 and GND (built-in BOOT button on most boards).
 */
#include <WiFi.h>
#include "EspNowNode.h"

static const int BUTTON_PIN = 0;   // BOOT button on most ESP32 boards
static bool lastButtonState = HIGH;
static int32_t ringCount = 0;

// ╔══════════════════════════════════════════════════════════╗
// ║  REPLACE WITH THE MAC FROM YOUR RECEIVER BOARD          ║
// ╚══════════════════════════════════════════════════════════╝
static const uint8_t RECEIVER_MAC[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// Settings (can be changed remotely)
static std::string ringTone = "default";
static int32_t     volume   = 80;

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    auto& node = EspNowNode::instance();

    node.begin(1)
        // ── Handle remote config ──
        .handle("config", [](const uint8_t* mac, EspNowNode::DataMap& ctrl) {
            if (ctrl.hasKey("tone"))   ringTone = ctrl.getString("tone", "default");
            if (ctrl.hasKey("volume")) volume   = ctrl.getInt("volume", 80);
            Serial.printf("Config updated: tone=%s volume=%d\n",
                          ringTone.c_str(), (int)volume);
        })

        // ── Advertise battery ──
        .advertise("battery");

    Serial.printf("Doorbell ready! MAC: %s\n",
                  EspNow::instance().getMyMac().c_str());
}

void loop() {
    bool buttonState = digitalRead(BUTTON_PIN);

    // Detect falling edge (button pressed)
    if (lastButtonState == HIGH && buttonState == LOW) {
        ringCount++;
        Serial.printf("RING! #%d (tone=%s, vol=%d)\n",
                      (int)ringCount, ringTone.c_str(), (int)volume);

        // Send ring event to receiver
        EspNowNode::instance().emit(RECEIVER_MAC, "ring")
            .set("count", ringCount)
            .set("tone", ringTone)
            .set("volume", volume)
            .send();

        delay(200);  // Debounce
    }
    lastButtonState = buttonState;

    // Publish battery every 30 seconds
    static unsigned long lastBat = 0;
    if (millis() - lastBat > 30000) {
        lastBat = millis();
        float voltage = analogRead(34) * 3.3f / 4095.0f * 2.0f;  // Voltage divider
        EspNowNode::instance().publish("battery")
            .set("voltage", voltage)
            .set("low", voltage < 3.0f)
            .send();
        Serial.printf("Battery: %.2fV\n", voltage);
    }

    delay(50);
}
