# EspNow

Portable C++ wrapper for ESP-NOW peer-to-peer communication.  
Works with both **ESP-IDF** and **Arduino** frameworks.

## Features

- 🔌 **Plug & Play** — auto WiFi init, coexists with existing WiFi connections
- 🔒 **Password Auth** — DJB2 token-based, both sides must match
- 📦 **Chunked Transfer** — send data up to ~61 KB (auto split/reassemble)
- 📡 **Broadcast & Unicast** — send to all or specific peers
- 🔄 **Dual Framework** — same API for ESP-IDF and Arduino

## Quick Start

### ESP-IDF

Copy `EspNow/` to your project's `components/` directory:

```
your_project/
├── components/
│   └── EspNow/
│       ├── CMakeLists.txt
│       ├── EspNow.h
│       └── EspNow.cpp
└── main/
    └── main.cpp
```

```cpp
#include "EspNow.h"

void onReceive(const uint8_t *mac, const uint8_t *data, int len) {
    ESP_LOGI("app", "RX %d bytes from %s", len, EspNow::macToString(mac).c_str());
}

extern "C" void app_main(void) {
    auto &now = EspNow::instance();
    now.init(6);                          // channel 6
    now.setPassword("my_secret");         // optional
    now.onReceive(onReceive);

    char msg[] = "hello";
    now.broadcast((const uint8_t *)msg, sizeof(msg));
}
```

### Arduino

Install via Library Manager or copy to `~/Arduino/libraries/EspNow/`:

```cpp
#include "EspNow.h"

void setup() {
    auto &now = EspNow::instance();
    now.init(6);
    now.setPassword("my_secret");
    now.onReceive([](const uint8_t *mac, const uint8_t *data, int len) {
        Serial.printf("RX %d bytes\n", len);
    });
}

void loop() {
    EspNow::instance().broadcast((const uint8_t *)"hello", 5);
    delay(3000);
}
```

## API Reference

### Initialization

| Method | Description |
|--------|-------------|
| `EspNow::instance()` | Get singleton |
| `init(channel)` | Init WiFi + ESP-NOW on channel (1-13) |
| `deinit()` | Cleanup |

### Peer Management

| Method | Description |
|--------|-------------|
| `addPeer(mac, channel, encrypt)` | Add peer (broadcast auto-added) |
| `removePeer(mac)` | Remove peer |

### Sending

| Method | Description |
|--------|-------------|
| `send(mac, data, len)` | Unicast (≤ 246 bytes) |
| `broadcast(data, len)` | Broadcast (≤ 246 bytes) |
| `sendLarge(mac, data, len, delayMs)` | Unicast with auto-chunking (≤ 61 KB) |
| `broadcastLarge(data, len, delayMs)` | Broadcast with auto-chunking (≤ 61 KB) |

### Receiving

| Method | Description |
|--------|-------------|
| `onReceive(callback)` | Register receive callback |
| `onSendComplete(callback)` | Register send status callback |

### Security

| Method | Description |
|--------|-------------|
| `setPassword(password)` | Set password (empty to disable) |

### Utilities

| Method | Description |
|--------|-------------|
| `getMyMac()` | Get MAC as string |
| `getMyMacRaw(buf)` | Get raw 6-byte MAC |
| `macToString(mac)` | Format MAC to string |

## Chunked Transfer Protocol

For data > 250 bytes, `sendLarge()` splits into chunks:

```
Packet: [password_token(4B)] [magic(1B)] [msgId(1B)] [chunkIdx(1B)] [totalChunks(1B)] [payload]
```

- Max payload per chunk: **242 bytes** (with password)
- Max total: **255 × 242 = ~61 KB**
- Receiver auto-reassembles, 5s timeout for incomplete messages
- Small data (≤ 246B) sent as single packet — no chunk overhead

## License

MIT
