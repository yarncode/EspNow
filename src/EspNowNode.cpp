#include "EspNowNode.h"

#ifndef ARDUINO
static const char* TAG_NODE = "EspNowNode";
#define NODE_LOG_I(fmt, ...) ESP_LOGI(TAG_NODE, fmt, ##__VA_ARGS__)
#define NODE_LOG_W(fmt, ...) ESP_LOGW(TAG_NODE, fmt, ##__VA_ARGS__)
#define NODE_LOG_E(fmt, ...) ESP_LOGE(TAG_NODE, fmt, ##__VA_ARGS__)
#else
#define NODE_LOG_I(fmt, ...) Serial.printf("[EspNowNode] " fmt "\n", ##__VA_ARGS__)
#define NODE_LOG_W(fmt, ...) Serial.printf("[EspNowNode WARN] " fmt "\n", ##__VA_ARGS__)
#define NODE_LOG_E(fmt, ...) Serial.printf("[EspNowNode ERR] " fmt "\n", ##__VA_ARGS__)
#endif

// ══════════════════════════════════════════════════════════════
// Singleton
// ══════════════════════════════════════════════════════════════

EspNowNode& EspNowNode::instance() {
    static EspNowNode inst;
    return inst;
}

// ══════════════════════════════════════════════════════════════
// Init
// ══════════════════════════════════════════════════════════════

EspNowNode& EspNowNode::begin(uint8_t channel) {
    if (initialized_) {
        NODE_LOG_W("Already initialized");
        return *this;
    }

    auto& espnow = EspNow::instance();
    esp_err_t ret = espnow.init(channel);
    if (ret != ESP_OK) {
        NODE_LOG_E("EspNow init failed: 0x%x", ret);
        return *this;
    }

    espnow.onReceive([this](const uint8_t* mac, const uint8_t* data, int len) {
        this->handleFrame(mac, data, len);
    });

    initialized_ = true;
    NODE_LOG_I("EspNowNode initialized on channel %d", channel);
    return *this;
}

// ══════════════════════════════════════════════════════════════
// Event & Control Registration
// ══════════════════════════════════════════════════════════════

EspNowNode& EspNowNode::on(const std::string& name, EventCallback cb) {
    eventHandlers_[name] = std::move(cb);
    NODE_LOG_I("Registered event: \"%s\"", name.c_str());
    return *this;
}

EspNowNode& EspNowNode::handle(const std::string& name, EventCallback cb) {
    controlHandlers_[name] = std::move(cb);
    NODE_LOG_I("Registered control: \"%s\"", name.c_str());
    return *this;
}

// ══════════════════════════════════════════════════════════════
// Property Advertisement & Publishing
// ══════════════════════════════════════════════════════════════

EspNowNode& EspNowNode::advertise(const std::string& name) {
    if (properties_.find(name) == properties_.end()) {
        properties_[name] = PropertyInfo{};
        NODE_LOG_I("Advertised property: \"%s\"", name.c_str());
    }
    return *this;
}

EspNowNode::MessageBuilder EspNowNode::publish(const std::string& name) {
    return MessageBuilder(*this, name);
}

esp_err_t EspNowNode::publishFrame(const std::string& name, const std::vector<uint8_t>& frame,
                                    const uint8_t* tlvPayload, size_t tlvLen) {
    auto it = properties_.find(name);
    if (it == properties_.end()) {
        NODE_LOG_W("Property \"%s\" not advertised", name.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    auto& prop = it->second;
    // Cache TLV payload for new subscribers
    prop.lastTLV.assign(tlvPayload, tlvPayload + tlvLen);

    if (prop.subscribers.empty()) {
        return ESP_OK;
    }

    esp_err_t lastErr = ESP_OK;
    for (auto& sub : prop.subscribers) {
        esp_err_t ret = sendFrame(sub.mac, frame);
        if (ret != ESP_OK) {
            NODE_LOG_E("Publish \"%s\" -> %s failed", name.c_str(),
                       EspNow::macToString(sub.mac).c_str());
            lastErr = ret;
        }
    }
    return lastErr;
}

// ══════════════════════════════════════════════════════════════
// Remote Operations
// ══════════════════════════════════════════════════════════════

EspNowNode::MessageBuilder EspNowNode::emit(const uint8_t* mac, const std::string& name) {
    return MessageBuilder(*this, mac, name, FRAME_EVENT);
}

EspNowNode& EspNowNode::observe(const uint8_t* mac, const std::string& name, ObserveCallback cb) {
    ObserverKey key;
    memcpy(key.mac, mac, ESP_NOW_ETH_ALEN);
    key.name = name;
    observers_[key] = std::move(cb);

    auto frame = buildFrame(FRAME_PROP_SUB, name);
    EspNow::instance().addPeer(mac);
    sendFrame(mac, frame);

    NODE_LOG_I("Observing \"%s\" on %s", name.c_str(), EspNow::macToString(mac).c_str());
    return *this;
}

EspNowNode& EspNowNode::unobserve(const uint8_t* mac, const std::string& name) {
    ObserverKey key;
    memcpy(key.mac, mac, ESP_NOW_ETH_ALEN);
    key.name = name;
    observers_.erase(key);

    auto frame = buildFrame(FRAME_PROP_UNSUB, name);
    sendFrame(mac, frame);
    NODE_LOG_I("Unobserved \"%s\" on %s", name.c_str(), EspNow::macToString(mac).c_str());
    return *this;
}

EspNowNode::MessageBuilder EspNowNode::control(const uint8_t* mac, const std::string& name) {
    return MessageBuilder(*this, mac, name, FRAME_CONTROL);
}

// ══════════════════════════════════════════════════════════════
// Frame Building & Sending
// ══════════════════════════════════════════════════════════════

std::vector<uint8_t> EspNowNode::buildFrame(uint8_t type, const std::string& name,
                                              const uint8_t* payload, size_t payloadLen) {
    std::vector<uint8_t> frame;
    uint8_t nameLen = (uint8_t)std::min(name.size(), (size_t)255);
    frame.reserve(2 + nameLen + payloadLen);

    frame.push_back(type);
    frame.push_back(nameLen);
    frame.insert(frame.end(), name.begin(), name.begin() + nameLen);

    if (payload && payloadLen > 0) {
        frame.insert(frame.end(), payload, payload + payloadLen);
    }
    return frame;
}

esp_err_t EspNowNode::sendFrame(const uint8_t* mac, const std::vector<uint8_t>& frame) {
    auto& espnow = EspNow::instance();
    if (frame.size() > ESP_NOW_MAX_DATA_LEN) {
        return espnow.sendLarge(mac, frame.data(), frame.size());
    }
    return espnow.send(mac, frame.data(), frame.size());
}

// ══════════════════════════════════════════════════════════════
// Internal Frame Handler
// ══════════════════════════════════════════════════════════════

void EspNowNode::handleFrame(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 2) return;

    uint8_t frameType = data[0];
    uint8_t nameLen = data[1];
    if (len < 2 + nameLen) return;

    std::string name((const char*)(data + 2), nameLen);
    const uint8_t* payload = data + 2 + nameLen;
    int payloadLen = len - 2 - nameLen;

    switch (frameType) {
        case FRAME_EVENT: {
            auto it = eventHandlers_.find(name);
            if (it != eventHandlers_.end()) {
                DataMap dm(payload, payloadLen);
                it->second(mac, dm);
            } else {
                NODE_LOG_W("No handler for event \"%s\"", name.c_str());
            }
            break;
        }

        case FRAME_PROP_SUB: {
            auto it = properties_.find(name);
            if (it != properties_.end()) {
                auto& prop = it->second;
                bool exists = false;
                for (auto& sub : prop.subscribers) {
                    if (memcmp(sub.mac, mac, ESP_NOW_ETH_ALEN) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    PropertyInfo::Subscriber sub;
                    memcpy(sub.mac, mac, ESP_NOW_ETH_ALEN);
                    prop.subscribers.push_back(sub);
                    NODE_LOG_I("Subscriber added for \"%s\": %s", name.c_str(),
                               EspNow::macToString(mac).c_str());
                }
                // Send current value immediately if available
                if (!prop.lastTLV.empty()) {
                    auto frame = buildFrame(FRAME_PROP_DATA, name,
                                            prop.lastTLV.data(), prop.lastTLV.size());
                    sendFrame(mac, frame);
                }
            } else {
                NODE_LOG_W("Property \"%s\" not advertised", name.c_str());
            }
            break;
        }

        case FRAME_PROP_UNSUB: {
            auto it = properties_.find(name);
            if (it != properties_.end()) {
                auto& subs = it->second.subscribers;
                for (auto sit = subs.begin(); sit != subs.end(); ++sit) {
                    if (memcmp(sit->mac, mac, ESP_NOW_ETH_ALEN) == 0) {
                        subs.erase(sit);
                        NODE_LOG_I("Subscriber removed for \"%s\": %s", name.c_str(),
                                   EspNow::macToString(mac).c_str());
                        break;
                    }
                }
            }
            break;
        }

        case FRAME_PROP_DATA: {
            ObserverKey key;
            memcpy(key.mac, mac, ESP_NOW_ETH_ALEN);
            key.name = name;

            auto it = observers_.find(key);
            if (it != observers_.end()) {
                DataMap dm(payload, payloadLen);
                it->second(dm);
            }
            break;
        }

        case FRAME_CONTROL: {
            auto it = controlHandlers_.find(name);
            if (it != controlHandlers_.end()) {
                DataMap dm(payload, payloadLen);
                it->second(mac, dm);
            } else {
                NODE_LOG_W("No handler for control \"%s\"", name.c_str());
            }
            break;
        }

        default:
            NODE_LOG_W("Unknown frame type: 0x%02X", frameType);
            break;
    }
}

// ══════════════════════════════════════════════════════════════
// DataMap — TLV Parser
// ══════════════════════════════════════════════════════════════

EspNowNode::DataMap::DataMap(const uint8_t* data, int len) {
    parse(data, len);
}

void EspNowNode::DataMap::parse(const uint8_t* data, int len) {
    int pos = 0;
    while (pos < len) {
        // [KeyLen:1][Key:N][ValType:1][ValLen:2 LE][Value:M]
        if (pos + 1 > len) break;
        uint8_t keyLen = data[pos++];
        if (pos + keyLen > len) break;
        std::string key((const char*)(data + pos), keyLen);
        pos += keyLen;

        if (pos + 3 > len) break;
        uint8_t valType = data[pos++];
        uint16_t valLen = data[pos] | (data[pos + 1] << 8);
        pos += 2;

        if (pos + valLen > len) break;
        TLVEntry entry;
        entry.type = valType;
        entry.value.assign(data + pos, data + pos + valLen);
        pos += valLen;

        entries_[key] = std::move(entry);
    }
}

bool EspNowNode::DataMap::hasKey(const std::string& key) const {
    return entries_.find(key) != entries_.end();
}

bool EspNowNode::DataMap::getBool(const std::string& key, bool defaultVal) const {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.value.empty()) return defaultVal;
    return it->second.value[0] != 0;
}

int32_t EspNowNode::DataMap::getInt(const std::string& key, int32_t defaultVal) const {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.value.size() < 4) return defaultVal;
    int32_t val;
    memcpy(&val, it->second.value.data(), 4);
    return val;
}

float EspNowNode::DataMap::getFloat(const std::string& key, float defaultVal) const {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.value.size() < 4) return defaultVal;
    float val;
    memcpy(&val, it->second.value.data(), 4);
    return val;
}

std::string EspNowNode::DataMap::getString(const std::string& key, const std::string& defaultVal) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return defaultVal;
    return std::string(it->second.value.begin(), it->second.value.end());
}

bool EspNowNode::DataMap::getRaw(const std::string& key, const uint8_t*& outData, int& outLen) const {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    outData = it->second.value.data();
    outLen = (int)it->second.value.size();
    return true;
}

// ══════════════════════════════════════════════════════════════
// MessageBuilder — Unified TLV Builder
// ══════════════════════════════════════════════════════════════

// Constructor for emit/control (single target)
EspNowNode::MessageBuilder::MessageBuilder(EspNowNode& node, const uint8_t* mac,
                                            const std::string& name, uint8_t frameType)
    : node_(node), name_(name), frameType_(frameType), isPublish_(false) {
    memcpy(mac_, mac, ESP_NOW_ETH_ALEN);
}

// Constructor for publish (broadcast to all subscribers)
EspNowNode::MessageBuilder::MessageBuilder(EspNowNode& node, const std::string& name)
    : node_(node), name_(name), frameType_(FRAME_PROP_DATA), isPublish_(true) {
    memset(mac_, 0, ESP_NOW_ETH_ALEN);
}

void EspNowNode::MessageBuilder::appendTLV(const std::string& key, uint8_t valType,
                                            const uint8_t* valData, uint16_t valLen) {
    uint8_t keyLen = (uint8_t)std::min(key.size(), (size_t)255);
    payload_.push_back(keyLen);
    payload_.insert(payload_.end(), key.begin(), key.begin() + keyLen);
    payload_.push_back(valType);
    payload_.push_back(valLen & 0xFF);
    payload_.push_back((valLen >> 8) & 0xFF);
    payload_.insert(payload_.end(), valData, valData + valLen);
}

EspNowNode::MessageBuilder& EspNowNode::MessageBuilder::set(const std::string& key, bool value) {
    uint8_t v = value ? 1 : 0;
    appendTLV(key, VAL_BOOL, &v, 1);
    return *this;
}

EspNowNode::MessageBuilder& EspNowNode::MessageBuilder::set(const std::string& key, int32_t value) {
    appendTLV(key, VAL_INT32, (const uint8_t*)&value, 4);
    return *this;
}

EspNowNode::MessageBuilder& EspNowNode::MessageBuilder::set(const std::string& key, float value) {
    appendTLV(key, VAL_FLOAT, (const uint8_t*)&value, 4);
    return *this;
}

EspNowNode::MessageBuilder& EspNowNode::MessageBuilder::set(const std::string& key, const std::string& value) {
    appendTLV(key, VAL_STRING, (const uint8_t*)value.data(), (uint16_t)value.size());
    return *this;
}

EspNowNode::MessageBuilder& EspNowNode::MessageBuilder::set(const std::string& key, const uint8_t* data, int len) {
    appendTLV(key, VAL_RAW, data, (uint16_t)len);
    return *this;
}

esp_err_t EspNowNode::MessageBuilder::send() {
    auto frame = EspNowNode::buildFrame(frameType_, name_, payload_.data(), payload_.size());

    if (isPublish_) {
        // Publish: send to all subscribers + cache
        return node_.publishFrame(name_, frame, payload_.data(), payload_.size());
    } else {
        // Emit/Control: send to single target
        EspNow::instance().addPeer(mac_);
        return node_.sendFrame(mac_, frame);
    }
}
