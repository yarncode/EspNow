#include "EspNow.h"

#ifndef ARDUINO
static const char* TAG = "EspNow";
#define LOG_I(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...) Serial.printf("[EspNow] " fmt "\n", ##__VA_ARGS__)
#define LOG_W(fmt, ...) Serial.printf("[EspNow WARN] " fmt "\n", ##__VA_ARGS__)
#define LOG_E(fmt, ...) Serial.printf("[EspNow ERR] " fmt "\n", ##__VA_ARGS__)
#endif

// ── Singleton ──────────────────────────────────────────────

EspNow& EspNow::instance() {
    static EspNow inst;
    return inst;
}

// ── Init / Deinit ──────────────────────────────────────────

esp_err_t EspNow::init(uint8_t channel) {
    if (initialized_) {
        LOG_W("Already initialized");
        return ESP_OK;
    }

#ifdef ARDUINO
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        LOG_I("WiFi STA mode, channel %d", channel);
    } else {
        LOG_I("WiFi already running (mode=%d), layering ESP-NOW", WiFi.getMode());
    }
#else
    wifi_mode_t currentMode;
    bool wifiAlreadyRunning = (esp_wifi_get_mode(&currentMode) == ESP_OK);

    if (!wifiAlreadyRunning) {
        LOG_I("WiFi not running, initializing...");

        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());

        ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA,
            WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
        ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));

        wifiManaged_ = true;
    } else {
        LOG_I("WiFi already running (mode=%d), layering ESP-NOW", currentMode);
        wifiManaged_ = false;
    }
#endif

    ESP_ERROR_CHECK(esp_now_init());

    static const uint8_t pmk[16] = {
        0x50, 0x4D, 0x4B, 0x5F, 0x45, 0x53, 0x50, 0x4E,
        0x4F, 0x57, 0x5F, 0x4B, 0x45, 0x59, 0x21, 0x00
    };
    ESP_ERROR_CHECK(esp_now_set_pmk(pmk));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onRecvStatic));
    ESP_ERROR_CHECK(esp_now_register_send_cb(onSendStatic));

    channel_ = channel;

#ifndef ARDUINO
    recvQueue_ = xQueueCreate(16, sizeof(RecvEvent));
    if (!recvQueue_) {
        LOG_E("Failed to create receive queue");
        return ESP_FAIL;
    }

    BaseType_t ok = xTaskCreate(recvTaskFunc, "espnow_recv", 4096, this, 5, &recvTask_);
    if (ok != pdPASS) {
        LOG_E("Failed to create receive task");
        return ESP_FAIL;
    }
#endif

    addPeer(BROADCAST_MAC, channel);

    initialized_ = true;
    LOG_I("Initialized — MAC: %s  CH: %d", getMyMac().c_str(), channel);
    return ESP_OK;
}

void EspNow::deinit() {
    if (!initialized_) return;

#ifndef ARDUINO
    if (recvTask_) { vTaskDelete(recvTask_); recvTask_ = nullptr; }
    if (recvQueue_) { vQueueDelete(recvQueue_); recvQueue_ = nullptr; }
#endif

    esp_now_deinit();
    reassemblyMap_.clear();

#ifndef ARDUINO
    if (wifiManaged_) {
        esp_wifi_stop();
        esp_wifi_deinit();
        wifiManaged_ = false;
    }
#endif

    initialized_ = false;
    LOG_I("De-initialized");
}

// ── Peer Management ────────────────────────────────────────

esp_err_t EspNow::addPeer(const uint8_t* mac, uint8_t channel, bool encrypt) {
    if (esp_now_is_peer_exist(mac)) return ESP_OK;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    peer.channel = channel;
    peer.encrypt = encrypt;
    peer.ifidx = WIFI_IF_STA;

    esp_err_t ret = esp_now_add_peer(&peer);
    if (ret == ESP_OK) {
        LOG_I("Added peer: %s", macToString(mac).c_str());
    } else {
        LOG_E("Add peer failed: %s (0x%x)", macToString(mac).c_str(), ret);
    }
    return ret;
}

esp_err_t EspNow::removePeer(const uint8_t* mac) {
    return esp_now_del_peer(mac);
}

// ── Send (single packet) ──────────────────────────────────

esp_err_t EspNow::send(const uint8_t* mac, const uint8_t* data, size_t len) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    if (hasPassword_) {
        size_t totalLen = TOKEN_SIZE + len;
        if (totalLen > ESP_NOW_MAX_DATA_LEN) return ESP_ERR_INVALID_SIZE;

        uint8_t buf[ESP_NOW_MAX_DATA_LEN];
        memcpy(buf, passwordToken_, TOKEN_SIZE);
        memcpy(buf + TOKEN_SIZE, data, len);
        return esp_now_send(mac, buf, totalLen);
    }

    return esp_now_send(mac, data, len);
}

esp_err_t EspNow::broadcast(const uint8_t* data, size_t len) {
    return send(BROADCAST_MAC, data, len);
}

// ── Send Large (chunked) ──────────────────────────────────

esp_err_t EspNow::sendLarge(const uint8_t* mac, const uint8_t* data, size_t len, uint16_t delayMs) {
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    // If fits in single packet, send directly (no chunk overhead)
    size_t maxSingle = hasPassword_ ? MAX_SINGLE_PAYLOAD : ESP_NOW_MAX_DATA_LEN;
    if (len <= maxSingle) {
        return send(mac, data, len);
    }

    // Calculate chunks
    size_t chunkPayload = hasPassword_ ? MAX_CHUNK_PAYLOAD : (ESP_NOW_MAX_DATA_LEN - CHUNK_HEADER_SIZE);
    size_t totalChunks = (len + chunkPayload - 1) / chunkPayload;

    if (totalChunks > 255) {
        LOG_E("Data too large: %d bytes (%d chunks > 255)", (int)len, (int)totalChunks);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t msgId = nextMsgId_++;
    LOG_I("Sending %d bytes in %d chunks (msgId=%d)", (int)len, (int)totalChunks, msgId);

    for (size_t i = 0; i < totalChunks; i++) {
        size_t offset = i * chunkPayload;
        size_t thisLen = (i == totalChunks - 1) ? (len - offset) : chunkPayload;

        // Build packet: [password_token] [chunk_header] [payload]
        uint8_t pkt[ESP_NOW_MAX_DATA_LEN];
        size_t pos = 0;

        if (hasPassword_) {
            memcpy(pkt + pos, passwordToken_, TOKEN_SIZE);
            pos += TOKEN_SIZE;
        }

        /// Chunk header: [magic=0xC7][msgId][chunkIdx][totalChunks]
        pkt[pos++] = CHUNK_MAGIC;
        pkt[pos++] = msgId;
        pkt[pos++] = (uint8_t)i;
        pkt[pos++] = (uint8_t)totalChunks;

        memcpy(pkt + pos, data + offset, thisLen);
        pos += thisLen;

        esp_err_t ret = esp_now_send(mac, pkt, pos);
        if (ret != ESP_OK) {
            LOG_E("Chunk %d/%d send failed: 0x%x", (int)i + 1, (int)totalChunks, ret);
            return ret;
        }

        // Delay between chunks to avoid flooding
        if (i < totalChunks - 1 && delayMs > 0) {
            vTaskDelay(pdMS_TO_TICKS(delayMs));
        }
    }

    return ESP_OK;
}

esp_err_t EspNow::broadcastLarge(const uint8_t* data, size_t len, uint16_t delayMs) {
    return sendLarge(BROADCAST_MAC, data, len, delayMs);
}

// ── Password ───────────────────────────────────────────────

void EspNow::setPassword(const std::string& password) {
    if (password.empty()) {
        hasPassword_ = false;
        memset(passwordToken_, 0, TOKEN_SIZE);
        LOG_I("Password disabled");
    } else {
        hasPassword_ = true;
        computeToken(password, passwordToken_);
        LOG_I("Password set (token: %02X%02X%02X%02X)",
              passwordToken_[0], passwordToken_[1], passwordToken_[2], passwordToken_[3]);
    }
}

void EspNow::computeToken(const std::string& password, uint8_t token[TOKEN_SIZE]) {
    uint32_t hash = 5381;
    for (char c : password) {
        hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }
    token[0] = (hash >> 24) & 0xFF;
    token[1] = (hash >> 16) & 0xFF;
    token[2] = (hash >> 8)  & 0xFF;
    token[3] =  hash        & 0xFF;
}

// ── Callbacks ──────────────────────────────────────────────

void EspNow::onReceive(RecvCallback cb) { recvCb_ = std::move(cb); }
void EspNow::onSendComplete(SendCallback cb) { sendCb_ = std::move(cb); }

// ── Incoming data handler (reassembly) ─────────────────────

void EspNow::handleIncoming(const uint8_t* mac, const uint8_t* data, int len) {
    // Chunk detection: first byte must be CHUNK_MAGIC
    bool isChunked = (len >= (int)CHUNK_HEADER_SIZE && data[0] == CHUNK_MAGIC && data[3] >= 2 && data[2] < data[3]);

    if (!isChunked) {
        // Plain single-packet message
        if (recvCb_) recvCb_(mac, data, len);
        return;
    }

    // Parse chunk header: [magic][msgId][chunkIdx][totalChunks]
    uint8_t msgId = data[1];
    uint8_t chunkIdx = data[2];
    uint8_t totalChunks = data[3];

    const uint8_t* payload = data + CHUNK_HEADER_SIZE;
    int payloadLen = len - CHUNK_HEADER_SIZE;

    // Build reassembly key
    ReassemblyKey key;
    memcpy(key.mac, mac, ESP_NOW_ETH_ALEN);
    key.msgId = msgId;

    // Find or create buffer
    auto it = reassemblyMap_.find(key);
    if (it == reassemblyMap_.end()) {
        // Cleanup stale entries first
        cleanupStaleReassembly();

        ReassemblyBuffer buf;
        buf.totalChunks = totalChunks;
        buf.receivedMask = 0;
        buf.totalLen = 0;

        // Allocate max possible size
        size_t chunkPayload = hasPassword_ ? MAX_CHUNK_PAYLOAD : (ESP_NOW_MAX_DATA_LEN - CHUNK_HEADER_SIZE);
        buf.data = (uint8_t*)malloc(totalChunks * chunkPayload);
        if (!buf.data) {
            LOG_E("Reassembly malloc failed (%d chunks)", totalChunks);
            return;
        }
        buf.startTick = xTaskGetTickCount();

        auto result = reassemblyMap_.emplace(key, std::move(buf));
        it = result.first;
    }

    auto& buf = it->second;

    // Store chunk data at correct position
    size_t chunkPayloadMax = hasPassword_ ? MAX_CHUNK_PAYLOAD : (ESP_NOW_MAX_DATA_LEN - CHUNK_HEADER_SIZE);
    size_t offset = chunkIdx * chunkPayloadMax;
    memcpy(buf.data + offset, payload, payloadLen);

    // Track received chunks
    if (chunkIdx < 8) {
        buf.receivedMask |= (1 << chunkIdx);
    }

    // Check if all chunks received
    uint8_t expectedMask = 0;
    bool allReceived = true;
    if (totalChunks <= 8) {
        expectedMask = (uint8_t)((1 << totalChunks) - 1);
        allReceived = (buf.receivedMask == expectedMask);
    } else {
        // For > 8 chunks, just check last chunk flag
        allReceived = (chunkIdx == totalChunks - 1);
    }

    if (allReceived) {
        // Calculate total length: all full chunks + last chunk
        size_t total = offset + payloadLen;

        LOG_I("Reassembled msgId=%d: %d bytes from %d chunks", msgId, (int)total, totalChunks);

        if (recvCb_) {
            recvCb_(mac, buf.data, (int)total);
        }

        reassemblyMap_.erase(it);
    }
}

void EspNow::cleanupStaleReassembly() {
    uint32_t now = xTaskGetTickCount();
    auto it = reassemblyMap_.begin();
    while (it != reassemblyMap_.end()) {
        uint32_t elapsed = (now - it->second.startTick) * portTICK_PERIOD_MS;
        if (elapsed > REASSEMBLY_TIMEOUT_MS) {
            LOG_I("Reassembly timeout: msgId=%d (chunk lost)", it->first.msgId);
            it = reassemblyMap_.erase(it);
        } else {
            ++it;
        }
    }
}

// ── Static ESP-NOW callbacks ──────────────────────────────

void EspNow::onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    auto& self = instance();

    // Password validation
    const uint8_t* payload = data;
    int payloadLen = len;

    if (self.hasPassword_) {
        if (len < (int)TOKEN_SIZE || memcmp(data, self.passwordToken_, TOKEN_SIZE) != 0) {
            return; // Wrong password — drop silently
        }
        payload = data + TOKEN_SIZE;
        payloadLen = len - TOKEN_SIZE;
    }

#ifndef ARDUINO
    if (!self.recvQueue_) return;

    RecvEvent evt = {};
    memcpy(evt.mac, info->src_addr, ESP_NOW_ETH_ALEN);
    int copyLen = (payloadLen > ESP_NOW_MAX_DATA_LEN) ? ESP_NOW_MAX_DATA_LEN : payloadLen;
    memcpy(evt.data, payload, copyLen);
    evt.len = copyLen;

    if (xQueueSend(self.recvQueue_, &evt, 0) != pdTRUE) {
        LOG_W("Receive queue full, dropping packet");
    }
#else
    self.handleIncoming(info->src_addr, payload, payloadLen);
#endif
}

void EspNow::onSendStatic(const uint8_t* mac, esp_now_send_status_t status) {
    auto& self = instance();
    if (self.sendCb_) self.sendCb_(mac, status);
}

// ── Receive task (ESP-IDF only) ────────────────────────────

#ifndef ARDUINO
void EspNow::recvTaskFunc(void* arg) {
    auto* self = static_cast<EspNow*>(arg);
    RecvEvent evt;

    while (true) {
        if (xQueueReceive(self->recvQueue_, &evt, portMAX_DELAY) == pdTRUE) {
            self->handleIncoming(evt.mac, evt.data, evt.len);
        }
    }
}
#endif

// ── MAC Utilities ──────────────────────────────────────────

std::string EspNow::getMyMac() const {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    return macToString(mac);
}

void EspNow::getMyMacRaw(uint8_t* mac) const {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

std::string EspNow::macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(buf);
}
