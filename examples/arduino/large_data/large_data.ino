/**
 * @file large_data.ino
 * @brief Large data transfer example (Arduino)
 *
 * Demonstrates sending data larger than 250 bytes using auto-chunking.
 * The library automatically splits large payloads into chunks and
 * reassembles them on the receiver side.
 *
 * Flash to two boards — both will broadcast and receive large payloads.
 */
#include <WiFi.h>
#include "EspNow.h"

void setup() {
    Serial.begin(115200);

    auto& now = EspNow::instance();
    now.init(6);
    now.setPassword("my_secret");

    now.onReceive([](const uint8_t* mac, const uint8_t* data, int len) {
        Serial.printf("RX [%s] %d bytes (reassembled)\n",
                      EspNow::macToString(mac).c_str(), len);
    });

    Serial.printf("MAC: %s\n", now.getMyMac().c_str());
}

void loop() {
    // Build a 1KB payload
    char payload[1024];
    memset(payload, 'A', sizeof(payload));
    snprintf(payload, 64, "Large payload (%d bytes)", (int)sizeof(payload));

    // Auto-chunks into ~5 packets, receiver reassembles
    EspNow::instance().broadcastLarge((const uint8_t*)payload, sizeof(payload));
    delay(5000);
}
