#pragma once

#include <Stream.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/ByteRing.h"

class BLEServer;
class BLECharacteristic;

namespace meshsat::bluetooth {

// Bench debug console over BLE, using the Nordic UART Service (NUS) so any
// NUS client works: nRF Connect, a Serial Bluetooth Terminal app, or
// tools/ble_console.py.
//
// It is a Stream, so it can sit next to the USB console. A client starts
// locked: until it sends the line "unlock <PIN>", nothing it writes reaches
// the console and no console output is sent to it. The PIN is set at build
// time. The link itself is not encrypted, so the PIN travels in the clear;
// this is a bench tool, not the node's product BLE interface.
//
// BLE callbacks run in the NimBLE host task and only hand bytes over through
// a FreeRTOS stream buffer. Everything else happens in poll(), in the main
// loop.
class BleConsole : public Stream {
public:
    static constexpr const char* kServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
    static constexpr const char* kRxCharacteristicUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
    static constexpr const char* kTxCharacteristicUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

    static constexpr size_t kMinPinLength = 6;
    static constexpr size_t kMaxPinLength = 16;
    static constexpr int kMaxUnlockFailures = 3;

    static constexpr size_t kIncomingBytes = 256;
    static constexpr size_t kConsoleRxBytes = 256;
    static constexpr size_t kTxBytes = 4096;
    static constexpr size_t kLockLineBytes = 40;

    // ATT header takes 3 bytes of the MTU. Larger chunks are capped so one
    // notification never holds the host's buffers for long.
    static constexpr size_t kAttHeaderBytes = 3;
    static constexpr size_t kDefaultNotifyBytes = 20;
    static constexpr size_t kMaxNotifyBytes = 180;
    static constexpr uint32_t kNotifyGapMs = 10;

    // Starts the BLE stack and advertising as "<namePrefix>-XXXX" (last two
    // bytes of the Bluetooth MAC). Returns false, and stays off, when the PIN
    // is shorter than kMinPinLength or longer than kMaxPinLength.
    bool begin(const char* namePrefix, const char* pin);

    void poll(uint32_t nowMs);

    bool enabled() const { return enabled_; }
    bool connected() const { return connected_.load(); }
    bool unlocked() const { return unlocked_; }
    const char* deviceName() const { return name_; }

    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t byte) override;
    size_t write(const uint8_t* data, size_t length) override;
    void flush() override {}

    // Called from the NimBLE host task.
    void onConnect();
    void onDisconnect();
    void onReceive(const uint8_t* data, size_t length);

private:
    void handleLockedByte(uint8_t byte);
    void checkUnlockLine();
    void queueText(const char* text);
    void sendPending(uint32_t nowMs);
    void resetSession();

    bool enabled_ = false;
    char name_[32] = {};
    char pin_[kMaxPinLength + 1] = {};

    BLEServer* server_ = nullptr;
    BLECharacteristic* tx_ = nullptr;

    std::atomic<bool> connected_{false};
    std::atomic<bool> sessionEnded_{false};
    std::atomic<uint32_t> incomingDropped_{0};

    StaticStreamBuffer_t incomingControl_ = {};
    uint8_t incomingStorage_[kIncomingBytes + 1] = {};
    StreamBufferHandle_t incoming_ = nullptr;

    bool unlocked_ = false;
    int unlockFailures_ = 0;
    char lockLine_[kLockLineBytes + 1] = {};
    size_t lockLineLength_ = 0;
    bool lockLineOverflow_ = false;

    core::ByteRing<kConsoleRxBytes> consoleRx_;
    core::ByteRing<kTxBytes> txRing_;
    uint32_t txDropped_ = 0;
    uint32_t lastNotifyMs_ = 0;
};

}
