/**
 * @file basic_broadcast.ino
 * @brief Basic ESP-NOW broadcast example (Arduino)
 */
#include <WiFi.h>
#include "EspNow.h"

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    char buf[65];
    int show = len > 64 ? 64 : len;
    memcpy(buf, data, show);
    buf[show] = '\0';
    Serial.printf("RX [%s] %d bytes: %s\n",
                  EspNow::macToString(mac).c_str(), len, buf);
}

void setup() {
    Serial.begin(115200);

    auto &now = EspNow::instance();
    now.init(6);
    now.setPassword("my_secret");
    now.onReceive(onReceive);

    Serial.printf("MAC: %s\n", now.getMyMac().c_str());
}

void loop() {
    static int counter = 0;
    char msg[64];
    int len = snprintf(msg, sizeof(msg), "hello #%d", counter++);
    EspNow::instance().broadcast((const uint8_t *)msg, len);
    delay(3000);
}
