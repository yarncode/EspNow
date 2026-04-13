#pragma once

#include "EspNow.h"
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <map>
#include <vector>

/**
 * @brief High-level event-driven communication layer on top of EspNow.
 *
 * All data exchanged between devices uses TLV (Type-Length-Value) binary encoding.
 * No manual byte manipulation needed — use DataMap to read and MessageBuilder to write.
 *
 * Usage (Sensor Node):
 *   auto& node = EspNowNode::instance();
 *   node.begin(1)
 *       .on("bdk_1", [](auto mac, auto& data) {
 *           int val = data.getInt("value");
 *           bool state = data.getBool("state");
 *       })
 *       .handle("pump", [](auto mac, auto& ctrl) {
 *           ctrl.getBool("on"); ctrl.getInt("speed");
 *       })
 *       .advertise("temperature");
 *
 *   node.publish("temperature").set("value", 25.5f).set("unit", "C").send();
 *
 * Usage (Controller Node):
 *   node.begin(1);
 *   node.emit(sensorMac, "bdk_1").set("value", 1).set("state", true).send();
 *   node.observe(sensorMac, "temperature", [](auto& data) {
 *       float t = data.getFloat("value");
 *   });
 *   node.control(sensorMac, "pump").set("on", true).set("speed", 100).send();
 */
class EspNowNode {
public:
    // ── Frame Types ────────────────────────────────────────
    static constexpr uint8_t FRAME_EVENT      = 0xE1;
    static constexpr uint8_t FRAME_PROP_SUB   = 0xE2;
    static constexpr uint8_t FRAME_PROP_UNSUB = 0xE3;
    static constexpr uint8_t FRAME_PROP_DATA  = 0xE4;
    static constexpr uint8_t FRAME_CONTROL    = 0xE5;

    // ── TLV Value Types ────────────────────────────────────
    static constexpr uint8_t VAL_BOOL   = 0x01;
    static constexpr uint8_t VAL_INT32  = 0x02;
    static constexpr uint8_t VAL_FLOAT  = 0x03;
    static constexpr uint8_t VAL_STRING = 0x04;
    static constexpr uint8_t VAL_RAW    = 0x05;

    // ════════════════════════════════════════════════════════
    // DataMap — TLV parser for incoming typed data
    // ════════════════════════════════════════════════════════

    class DataMap {
    public:
        DataMap() = default;
        DataMap(const uint8_t* data, int len);

        bool        getBool(const std::string& key, bool defaultVal = false) const;
        int32_t     getInt(const std::string& key, int32_t defaultVal = 0) const;
        float       getFloat(const std::string& key, float defaultVal = 0.0f) const;
        std::string getString(const std::string& key, const std::string& defaultVal = "") const;
        bool        getRaw(const std::string& key, const uint8_t*& outData, int& outLen) const;
        bool        hasKey(const std::string& key) const;

    private:
        struct TLVEntry {
            uint8_t type;
            std::vector<uint8_t> value;
        };
        std::map<std::string, TLVEntry> entries_;
        void parse(const uint8_t* data, int len);
    };

    // ── Callback Types ─────────────────────────────────────

    /// Event & Control callback: (sender_mac, parsed data)
    using EventCallback = std::function<void(const uint8_t* mac, DataMap& data)>;

    /// Property observer callback: (parsed data)
    using ObserveCallback = std::function<void(DataMap& data)>;

    // ════════════════════════════════════════════════════════
    // MessageBuilder — Method chaining for emit/publish/control
    // ════════════════════════════════════════════════════════

    class MessageBuilder {
    public:
        enum class Mode { EMIT, PUBLISH, CONTROL };

        /// For emit/control (single target)
        MessageBuilder(EspNowNode& node, const uint8_t* mac,
                       const std::string& name, uint8_t frameType);

        /// For publish (send to all subscribers of a property)
        MessageBuilder(EspNowNode& node, const std::string& name);

        MessageBuilder& set(const std::string& key, bool value);
        MessageBuilder& set(const std::string& key, int32_t value);
        MessageBuilder& set(const std::string& key, float value);
        MessageBuilder& set(const std::string& key, const std::string& value);
        MessageBuilder& set(const std::string& key, const uint8_t* data, int len);

        /// Build TLV payload, construct frame, and send
        esp_err_t send();

    private:
        EspNowNode& node_;
        uint8_t mac_[ESP_NOW_ETH_ALEN];
        std::string name_;
        uint8_t frameType_;
        bool isPublish_ = false;
        std::vector<uint8_t> payload_;

        void appendTLV(const std::string& key, uint8_t valType,
                       const uint8_t* valData, uint16_t valLen);
    };

    // ── Singleton ──────────────────────────────────────────

    static EspNowNode& instance();

    EspNowNode(const EspNowNode&) = delete;
    EspNowNode& operator=(const EspNowNode&) = delete;

    // ── Init ───────────────────────────────────────────────

    EspNowNode& begin(uint8_t channel = 1);

    // ── Event Registration (method chaining) ───────────────

    /**
     * @brief Register a named event handler.
     *        Other devices trigger this via emit(mac, name).set(...).send()
     */
    EspNowNode& on(const std::string& name, EventCallback cb);

    /**
     * @brief Register a control command handler.
     *        Other devices trigger this via control(mac, name).set(...).send()
     */
    EspNowNode& handle(const std::string& name, EventCallback cb);

    // ── Property Advertisement & Publishing ────────────────

    /**
     * @brief Declare a property this node can publish.
     */
    EspNowNode& advertise(const std::string& name);

    /**
     * @brief Start building a publish message (method chaining).
     *        Data will be sent to all subscribers when .send() is called.
     * @return MessageBuilder for chaining .set("key", value).send()
     */
    MessageBuilder publish(const std::string& name);

    // ── Remote Operations ──────────────────────────────────

    /**
     * @brief Start building an event message to a specific device.
     * @return MessageBuilder for chaining .set("key", value).send()
     */
    MessageBuilder emit(const uint8_t* mac, const std::string& name);

    /**
     * @brief Subscribe to observe a property on a remote device.
     *        Callback receives parsed DataMap with typed values.
     */
    EspNowNode& observe(const uint8_t* mac, const std::string& name, ObserveCallback cb);

    /**
     * @brief Unsubscribe from observing a property.
     */
    EspNowNode& unobserve(const uint8_t* mac, const std::string& name);

    /**
     * @brief Start building a control command (method chaining).
     * @return MessageBuilder for chaining .set("key", value).send()
     */
    MessageBuilder control(const uint8_t* mac, const std::string& name);

    // ── Internal: used by MessageBuilder for publish ───────
    esp_err_t publishFrame(const std::string& name, const std::vector<uint8_t>& frame,
                           const uint8_t* tlvPayload, size_t tlvLen);

private:
    EspNowNode() = default;

    bool initialized_ = false;

    // ── Event handlers (name -> callback) ──────────────────
    std::map<std::string, EventCallback> eventHandlers_;

    // ── Control handlers (name -> callback) ────────────────
    std::map<std::string, EventCallback> controlHandlers_;

    // ── Advertised properties ──────────────────────────────
    struct PropertyInfo {
        std::vector<uint8_t> lastTLV;  // Last TLV payload (for new subscribers)
        struct Subscriber {
            uint8_t mac[ESP_NOW_ETH_ALEN];
        };
        std::vector<Subscriber> subscribers;
    };
    std::map<std::string, PropertyInfo> properties_;

    // ── Observer subscriptions (for remote properties) ─────
    struct ObserverKey {
        uint8_t mac[ESP_NOW_ETH_ALEN];
        std::string name;
        bool operator<(const ObserverKey& o) const {
            int r = memcmp(mac, o.mac, ESP_NOW_ETH_ALEN);
            if (r != 0) return r < 0;
            return name < o.name;
        }
    };
    std::map<ObserverKey, ObserveCallback> observers_;

    // ── Frame building & parsing ───────────────────────────
    static std::vector<uint8_t> buildFrame(uint8_t type, const std::string& name,
                                            const uint8_t* payload = nullptr, size_t payloadLen = 0);
    esp_err_t sendFrame(const uint8_t* mac, const std::vector<uint8_t>& frame);
    void handleFrame(const uint8_t* mac, const uint8_t* data, int len);
};
