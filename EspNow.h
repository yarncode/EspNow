#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <map>

// ── Platform detection ─────────────────────────────────────
#ifdef ARDUINO
    #include <Arduino.h>
    #include <WiFi.h>
    #include <esp_now.h>
    #include <esp_wifi.h>
    #if __has_include(<esp_mac.h>)
        #include <esp_mac.h>
    #endif
#else
    #include "esp_now.h"
    #include "esp_wifi.h"
    #include "esp_log.h"
    #include "esp_mac.h"
    #include "esp_event.h"
    #include "nvs_flash.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/queue.h"
    #include "freertos/task.h"
#endif

/**
 * @brief C++ wrapper for ESP-NOW peer-to-peer communication.
 *
 * Compatible with both ESP-IDF and Arduino frameworks.
 * Supports chunked transfer for data larger than 250 bytes.
 *
 * Usage:
 *   auto& now = EspNow::instance();
 *   now.init(6);
 *   now.setPassword("secret");
 *   now.onReceive([](const uint8_t* mac, const uint8_t* data, int len) { ... });
 *   now.sendLarge(peer_mac, bigPayload, bigLen);  // auto-chunks
 */
class EspNow {
public:
    /// Callback types
    using RecvCallback = std::function<void(const uint8_t* mac, const uint8_t* data, int len)>;
    using SendCallback = std::function<void(const uint8_t* mac, esp_now_send_status_t status)>;

    /// Broadcast address constant
    static constexpr uint8_t BROADCAST_MAC[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    /// Token size prepended to messages when password is set
    static constexpr size_t TOKEN_SIZE = 4;

    /// Chunk header: [magic][msgId][chunkIdx][totalChunks]
    static constexpr size_t CHUNK_HEADER_SIZE = 4;
    static constexpr uint8_t CHUNK_MAGIC = 0xC7;

    /// Max payload per single ESP-NOW packet (without password/chunk overhead)
    static constexpr size_t MAX_SINGLE_PAYLOAD = ESP_NOW_MAX_DATA_LEN - TOKEN_SIZE;

    /// Max payload per chunk (with both password + chunk header)
    static constexpr size_t MAX_CHUNK_PAYLOAD = ESP_NOW_MAX_DATA_LEN - TOKEN_SIZE - CHUNK_HEADER_SIZE;

    /// Max total data size for sendLarge (255 chunks)
    static constexpr size_t MAX_LARGE_PAYLOAD = MAX_CHUNK_PAYLOAD * 255;

    /// Get singleton instance
    static EspNow& instance();

    // Non-copyable
    EspNow(const EspNow&) = delete;
    EspNow& operator=(const EspNow&) = delete;

    /**
     * @brief Initialize NVS, WiFi (STA), and ESP-NOW.
     * @param channel WiFi channel (1-13). Both devices must match.
     * @return ESP_OK on success
     */
    esp_err_t init(uint8_t channel = 1);

    /** @brief De-initialize ESP-NOW. */
    void deinit();

    /**
     * @brief Add a peer device by MAC address.
     * @param mac 6-byte MAC address
     * @param channel WiFi channel (0 = current)
     * @param encrypt Enable encryption
     * @return ESP_OK on success
     */
    esp_err_t addPeer(const uint8_t* mac, uint8_t channel = 0, bool encrypt = false);

    /** @brief Remove a peer. */
    esp_err_t removePeer(const uint8_t* mac);

    /**
     * @brief Set password for message authentication.
     *        Both sides must match. Empty string to disable.
     */
    void setPassword(const std::string& password);

    /**
     * @brief Send data (single packet, max 246 bytes with password).
     */
    esp_err_t send(const uint8_t* mac, const uint8_t* data, size_t len);

    /** @brief Broadcast (single packet). */
    esp_err_t broadcast(const uint8_t* data, size_t len);

    /**
     * @brief Send data of any size — auto-chunks if > single packet.
     *        Small data goes as single packet (no chunk overhead).
     *        Large data is split with 4-byte chunk headers.
     * @param mac Destination MAC
     * @param data Data buffer
     * @param len Data length (max ~61 KB)
     * @param delayMs Delay between chunks in ms (default 15)
     * @return ESP_OK on success
     */
    esp_err_t sendLarge(const uint8_t* mac, const uint8_t* data, size_t len, uint16_t delayMs = 15);

    /** @brief Broadcast large data (auto-chunks). */
    esp_err_t broadcastLarge(const uint8_t* data, size_t len, uint16_t delayMs = 15);

    /** @brief Register receive callback (delivers complete reassembled messages). */
    void onReceive(RecvCallback cb);

    /** @brief Register send completion callback. */
    void onSendComplete(SendCallback cb);

    /** @brief Get MAC as string "AA:BB:CC:DD:EE:FF". */
    std::string getMyMac() const;

    /** @brief Get raw 6-byte MAC. */
    void getMyMacRaw(uint8_t* mac) const;

    /** @brief Format MAC to string. */
    static std::string macToString(const uint8_t* mac);

private:
    EspNow() = default;

    bool initialized_ = false;
    bool wifiManaged_ = false;
    bool hasPassword_ = false;
    uint8_t channel_ = 1;
    uint8_t nextMsgId_ = 0;
    uint8_t passwordToken_[TOKEN_SIZE] = {};
    RecvCallback recvCb_ = nullptr;
    SendCallback sendCb_ = nullptr;

    static void computeToken(const std::string& password, uint8_t token[TOKEN_SIZE]);

    // ── Chunk reassembly ───────────────────────────────────
    struct ReassemblyKey {
        uint8_t mac[ESP_NOW_ETH_ALEN];
        uint8_t msgId;
        bool operator<(const ReassemblyKey& o) const {
            int r = memcmp(mac, o.mac, ESP_NOW_ETH_ALEN);
            if (r != 0) return r < 0;
            return msgId < o.msgId;
        }
    };

    struct ReassemblyBuffer {
        uint8_t totalChunks;
        uint8_t receivedMask;   // bitmask for up to 8 chunks (simple case)
        size_t totalLen;
        uint8_t* data;          // dynamically allocated
        uint32_t startTick;     // for timeout cleanup

        ReassemblyBuffer() : totalChunks(0), receivedMask(0), totalLen(0), data(nullptr), startTick(0) {}
        ~ReassemblyBuffer() { free(data); }

        // Non-copyable, movable
        ReassemblyBuffer(const ReassemblyBuffer&) = delete;
        ReassemblyBuffer& operator=(const ReassemblyBuffer&) = delete;
        ReassemblyBuffer(ReassemblyBuffer&& o) noexcept
            : totalChunks(o.totalChunks), receivedMask(o.receivedMask),
              totalLen(o.totalLen), data(o.data), startTick(o.startTick) {
            o.data = nullptr;
        }
        ReassemblyBuffer& operator=(ReassemblyBuffer&& o) noexcept {
            if (this != &o) {
                free(data);
                totalChunks = o.totalChunks;
                receivedMask = o.receivedMask;
                totalLen = o.totalLen;
                data = o.data;
                startTick = o.startTick;
                o.data = nullptr;
            }
            return *this;
        }
    };

    std::map<ReassemblyKey, ReassemblyBuffer> reassemblyMap_;
    static constexpr uint32_t REASSEMBLY_TIMEOUT_MS = 5000;

    /// Process incoming data: detect chunked vs plain, reassemble if needed
    void handleIncoming(const uint8_t* mac, const uint8_t* data, int len);
    void cleanupStaleReassembly();

#ifndef ARDUINO
    struct RecvEvent {
        uint8_t mac[ESP_NOW_ETH_ALEN];
        uint8_t data[ESP_NOW_MAX_DATA_LEN];
        int len;
    };

    QueueHandle_t recvQueue_ = nullptr;
    TaskHandle_t recvTask_ = nullptr;
    static void recvTaskFunc(void* arg);
#endif

    // ESP-IDF v5.1+ uses esp_now_recv_info_t; older IDF / Arduino uses (const uint8_t* mac)
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
    static void onRecvStatic(const esp_now_recv_info_t* info, const uint8_t* data, int len);
#else
    static void onRecvStatic(const uint8_t* mac, const uint8_t* data, int len);
#endif
    static void onSendStatic(const uint8_t* mac, esp_now_send_status_t status);
};
