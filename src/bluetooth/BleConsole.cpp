#include "bluetooth/BleConsole.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <esp_mac.h>

#include <cstdio>
#include <cstring>

#include "core/Log.h"

namespace meshsat::bluetooth {

namespace {

using core::Log;

// HCI reason "remote user terminated connection".
constexpr uint8_t kDisconnectReason = 0x13;

constexpr const char* kUnlockPrefix = "unlock ";

class ServerCallbacks : public BLEServerCallbacks {
public:
    BleConsole* owner = nullptr;

    void onConnect(BLEServer* server) override {
        (void)server;
        owner->onConnect();
    }

    void onDisconnect(BLEServer* server) override {
        (void)server;
        owner->onDisconnect();
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
public:
    BleConsole* owner = nullptr;

    void onWrite(BLECharacteristic* characteristic) override {
        owner->onReceive(characteristic->getData(), characteristic->getLength());
    }
};

ServerCallbacks serverCallbacks;
RxCallbacks rxCallbacks;

}

bool BleConsole::begin(const char* namePrefix, const char* pin) {
    const size_t pinLength = std::strlen(pin);
    if (pinLength < kMinPinLength || pinLength > kMaxPinLength) {
        return false;
    }
    std::memcpy(pin_, pin, pinLength + 1);

    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(name_, sizeof(name_), "%s-%02X%02X", namePrefix, mac[4], mac[5]);

    incoming_ = xStreamBufferCreateStatic(kIncomingBytes, 1, incomingStorage_, &incomingControl_);
    if (incoming_ == nullptr) {
        return false;
    }

    serverCallbacks.owner = this;
    rxCallbacks.owner = this;

    BLEDevice::init(name_);
    server_ = BLEDevice::createServer();
    server_->setCallbacks(&serverCallbacks);
    server_->advertiseOnDisconnect(true);

    BLEService* service = server_->createService(kServiceUuid);
    tx_ = service->createCharacteristic(kTxCharacteristicUuid, BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic* rx = service->createCharacteristic(
        kRxCharacteristicUuid, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(&rxCallbacks);
    service->start();

    BLEDevice::startAdvertising();
    enabled_ = true;
    return true;
}

void BleConsole::poll(uint32_t nowMs) {
    if (!enabled_) {
        return;
    }
    if (sessionEnded_.exchange(false)) {
        resetSession();
        Log::info("BLE client disconnected; console locked, advertising again as %s", name_);
    }

    uint8_t chunk[64];
    size_t received = 0;
    while ((received = xStreamBufferReceive(incoming_, chunk, sizeof(chunk), 0)) > 0) {
        for (size_t i = 0; i < received; ++i) {
            if (unlocked_) {
                consoleRx_.push(&chunk[i], 1);
            } else {
                handleLockedByte(chunk[i]);
            }
        }
    }

    const uint32_t dropped = incomingDropped_.exchange(0);
    if (dropped > 0) {
        Log::warn("BLE console: %lu received bytes dropped (typing faster than the loop reads)",
                  static_cast<unsigned long>(dropped));
    }

    sendPending(nowMs);
}

int BleConsole::available() { return static_cast<int>(consoleRx_.size()); }

int BleConsole::read() { return consoleRx_.pop(); }

int BleConsole::peek() { return consoleRx_.peek(); }

size_t BleConsole::write(uint8_t byte) { return write(&byte, 1); }

size_t BleConsole::write(const uint8_t* data, size_t length) {
    if (!enabled_ || !connected_.load() || !unlocked_) {
        return length;
    }
    const size_t taken = txRing_.push(data, length);
    txDropped_ += static_cast<uint32_t>(length - taken);
    return length;
}

void BleConsole::onConnect() { connected_.store(true); }

void BleConsole::onDisconnect() {
    connected_.store(false);
    sessionEnded_.store(true);
}

void BleConsole::onReceive(const uint8_t* data, size_t length) {
    if (incoming_ == nullptr || length == 0) {
        return;
    }
    const size_t sent = xStreamBufferSend(incoming_, data, length, 0);
    if (sent < length) {
        incomingDropped_.fetch_add(static_cast<uint32_t>(length - sent));
    }
}

void BleConsole::handleLockedByte(uint8_t byte) {
    if (byte == '\r' || byte == '\n') {
        checkUnlockLine();
        return;
    }
    if (lockLineLength_ < kLockLineBytes) {
        lockLine_[lockLineLength_++] = static_cast<char>(byte);
    } else {
        lockLineOverflow_ = true;
    }
}

void BleConsole::checkUnlockLine() {
    if (lockLineLength_ == 0 && !lockLineOverflow_) {
        return;
    }
    lockLine_[lockLineLength_] = '\0';
    const size_t prefixLength = std::strlen(kUnlockPrefix);
    const bool match = !lockLineOverflow_ && std::strncmp(lockLine_, kUnlockPrefix, prefixLength) == 0 &&
                       std::strcmp(lockLine_ + prefixLength, pin_) == 0;
    lockLineLength_ = 0;
    lockLineOverflow_ = false;

    if (match) {
        unlocked_ = true;
        unlockFailures_ = 0;
        Log::info("BLE console unlocked (%s); type help", name_);
        return;
    }

    ++unlockFailures_;
    if (unlockFailures_ >= kMaxUnlockFailures) {
        Log::warn("BLE client sent a wrong unlock line %d times, disconnecting it", unlockFailures_);
        server_->disconnect(server_->getConnId(), kDisconnectReason);
        return;
    }
    queueText("locked: send the line unlock <PIN>\r\n");
}

void BleConsole::queueText(const char* text) {
    if (!connected_.load()) {
        return;
    }
    txRing_.push(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
}

void BleConsole::sendPending(uint32_t nowMs) {
    if (!connected_.load() || txRing_.empty()) {
        return;
    }
    if (static_cast<uint32_t>(nowMs - lastNotifyMs_) < kNotifyGapMs) {
        return;
    }
    const uint16_t mtu = server_->getPeerMTU(server_->getConnId());
    size_t chunkLimit = mtu > kAttHeaderBytes + kDefaultNotifyBytes ? mtu - kAttHeaderBytes : kDefaultNotifyBytes;
    if (chunkLimit > kMaxNotifyBytes) {
        chunkLimit = kMaxNotifyBytes;
    }
    uint8_t chunk[kMaxNotifyBytes];
    const size_t count = txRing_.copyFront(chunk, chunkLimit);
    tx_->setValue(chunk, count);
    tx_->notify();
    txRing_.drop(count);
    lastNotifyMs_ = nowMs;
}

void BleConsole::resetSession() {
    unlocked_ = false;
    unlockFailures_ = 0;
    lockLineLength_ = 0;
    lockLineOverflow_ = false;
    consoleRx_.clear();
    txRing_.clear();
    xStreamBufferReset(incoming_);
}

}
