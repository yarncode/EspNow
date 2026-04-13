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
 * Provides:
 *   - Named event registration & emission
 *   - Property advertising, publishing & observation
 *   - Smart control commands with TLV encoding
 *   - Method chaining for fluent API
 *
 * Usage (Sensor Node):
 *   auto& node = EspNowNode::instance();
 *   node.begin(1)
 *       .on("bdk_1", [](auto mac, auto data, int len) { })
 *       .handle("pump", [](auto mac, auto& ctrl) { ctrl.getInt("speed", 0); })
 *       .advertise("temperature");
 *   node.publish("temperature", &temp, sizeof(temp));
 *
 * Usage (Controller Node):
 *   node.begin(1);
 *   node.emit(sensorMac, "bdk_1", &val, 1);
 *   node.observe(sensorMac, "temperature", [](auto data, int len) { });
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

    // ── Callback Types ─────────────────────────────────────

    /// Event callback: (sender_mac, payload_data, payload_len)
    using EventCallback = std::function<void(const uint8_t* mac, const uint8_t* data, int len)>;

    /// Property observer callback: (payload_data, payload_len)
    using ObserveCallback = std::function<void(const uint8_t* data, int len)>;

    // ── ControlData — TLV parser for incoming control commands ──

    class ControlData {
    public:
        ControlData(const uint8_t* data, int len);

        bool     getBool(const std::string& key, bool defaultVal = false) const;
        int32_t  getInt(const std::string& key, int32_t defaultVal = 0) const;
        float    getFloat(const std::string& key, float defaultVal = 0.0f) const;
        std::string getString(const std::string& key, const std::string& defaultVal = "") const;
        bool     getRaw(const std::string& key, const uint8_t*& outData, int& outLen) const;
        bool     hasKey(const std::string& key) const;

    private:
        struct TLVEntry {
            uint8_t type;
            std::vector<uint8_t> value;
        };
        std::map<std::string, TLVEntry> entries_;
        void parse(const uint8_t* data, int len);
    };

    /// Control handler callback: (sender_mac, parsed_control_data)
    using ControlCallback = std::function<void(const uint8_t* mac, ControlData& ctrl)>;

    // ── ControlBuilder — Method chaining builder for outgoing control ──

    class ControlBuilder {
    public:
        ControlBuilder(EspNowNode& node, const uint8_t* mac, const std::string& name);

        ControlBuilder& set(const std::string& key, bool value);
        ControlBuilder& set(const std::string& key, int32_t value);
        ControlBuilder& set(const std::string& key, float value);
        ControlBuilder& set(const std::string& key, const std::string& value);
        ControlBuilder& set(const std::string& key, const uint8_t* data, int len);

        /// Build TLV payload and send CONTROL frame
        esp_err_t send();

    private:
        EspNowNode& node_;
        uint8_t mac_[ESP_NOW_ETH_ALEN];
        std::string name_;
        std::vector<uint8_t> payload_;

        void appendTLV(const std::string& key, uint8_t valType, const uint8_t* valData, uint16_t valLen);
    };

    // ── Singleton ──────────────────────────────────────────

    static EspNowNode& instance();

    EspNowNode(const EspNowNode&) = delete;
    EspNowNode& operator=(const EspNowNode&) = delete;

    // ── Init ───────────────────────────────────────────────

    /**
     * @brief Initialize EspNow and register internal frame handler.
     * @param channel WiFi channel (1-13)
     * @return Reference to self for method chaining
     */
    EspNowNode& begin(uint8_t channel = 1);

    // ── Event Registration (method chaining) ───────────────

    /**
     * @brief Register a named event handler. Other devices can emit() to this event.
     * @param name Event name (e.g. "bdk_1")
     * @param cb   Callback invoked when event is received
     */
    EspNowNode& on(const std::string& name, EventCallback cb);

    /**
     * @brief Register a control command handler with TLV parsing.
     * @param name  Control name (e.g. "pump")
     * @param cb    Callback with parsed ControlData
     */
    EspNowNode& handle(const std::string& name, ControlCallback cb);

    // ── Property Advertisement & Publishing ────────────────

    /**
     * @brief Declare a property that this node can publish.
     *        Remote devices can observe() this property.
     */
    EspNowNode& advertise(const std::string& name);

    /**
     * @brief Publish property data to all subscribers.
     * @param name Property name (must be advertised first)
     * @param data Raw data bytes
     * @param len  Data length
     */
    esp_err_t publish(const std::string& name, const void* data, size_t len);

    // ── Remote Operations ──────────────────────────────────

    /**
     * @brief Send an event to a specific device (triggers its .on() handler).
     */
    esp_err_t emit(const uint8_t* mac, const std::string& name,
                   const uint8_t* data = nullptr, size_t len = 0);

    /**
     * @brief Subscribe to observe a property on a remote device.
     * @param mac  Remote device MAC
     * @param name Property name
     * @param cb   Callback invoked when property data is received
     */
    EspNowNode& observe(const uint8_t* mac, const std::string& name, ObserveCallback cb);

    /**
     * @brief Unsubscribe from observing a property.
     */
    EspNowNode& unobserve(const uint8_t* mac, const std::string& name);

    /**
     * @brief Start building a control command (method chaining).
     * @return ControlBuilder for chaining .set("key", value).send()
     */
    ControlBuilder control(const uint8_t* mac, const std::string& name);

private:
    EspNowNode() = default;

    bool initialized_ = false;

    // ── Event handlers (name -> callback) ──────────────────
    std::map<std::string, EventCallback> eventHandlers_;

    // ── Control handlers (name -> callback) ────────────────
    std::map<std::string, ControlCallback> controlHandlers_;

    // ── Advertised properties ──────────────────────────────
    struct PropertyInfo {
        std::vector<uint8_t> lastValue;
        // List of subscriber MACs
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

    /**
     * @brief Build a frame: [type][nameLen][name][payload]
     */
    static std::vector<uint8_t> buildFrame(uint8_t type, const std::string& name,
                                            const uint8_t* payload = nullptr, size_t payloadLen = 0);

    /**
     * @brief Send a pre-built frame via EspNow.
     */
    esp_err_t sendFrame(const uint8_t* mac, const std::vector<uint8_t>& frame);

    /**
     * @brief Internal receive handler registered with EspNow.
     */
    void handleFrame(const uint8_t* mac, const uint8_t* data, int len);
};
