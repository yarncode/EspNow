/**
 * @file auto_pair.ino
 * @brief Auto-pairing example (Arduino)
 *
 * Two ESP32 boards running this sketch will automatically discover
 * and pair with each other. After pairing, they exchange messages.
 *
 * Both boards must use the same channel and password.
 * Press RESET on both boards at roughly the same time.
 */
#include <WiFi.h>
#include "EspNow.h"

static uint8_t peerMac[6] = {};
static bool paired = false;

void setup() {
    Serial.begin(115200);

    auto& now = EspNow::instance();
    now.init(6);
    now.setPassword("pair_demo");

    now.onReceive([](const uint8_t* mac, const uint8_t* data, int len) {
        char buf[65];
        int show = len > 64 ? 64 : len;
        memcpy(buf, data, show);
        buf[show] = '\0';
        Serial.printf("RX from %s: %s\n",
                      EspNow::macToString(mac).c_str(), buf);
    });

    Serial.printf("My MAC: %s\n", now.getMyMac().c_str());
    Serial.println("Waiting for auto-pair...");

    // Block until another board responds (5s timeout)
    paired = now.autoPair(5000);

    if (paired) {
        now.getMyMacRaw(peerMac);  // Peer stored internally by autoPair
        Serial.println("Paired successfully!");
    } else {
        Serial.println("Pairing timeout — no peer found.");
    }
}

void loop() {
    if (!paired) {
        delay(1000);
        return;
    }

    static int counter = 0;
    char msg[64];
    int len = snprintf(msg, sizeof(msg), "paired msg #%d", counter++);

    // After autoPair, broadcast is the simplest way to reach the peer
    EspNow::instance().broadcast((const uint8_t*)msg, len);
    delay(3000);
}
