#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "core/ByteStream.h"
#include "iridium/IridiumModem.h"

namespace meshsat::test {

// In-memory ByteStream: tests queue modem output with feed() and inspect
// what the driver transmitted in `written`.
class FakeByteStream : public core::ByteStream {
public:
    size_t available() override { return rx_.size() - rxPos_; }

    int read() override {
        if (rxPos_ >= rx_.size()) {
            return -1;
        }
        return static_cast<uint8_t>(rx_[rxPos_++]);
    }

    size_t write(const uint8_t* data, size_t length) override {
        size_t count = length;
        if (maxWritePerCall > 0 && count > maxWritePerCall) {
            count = maxWritePerCall;
        }
        if (refuseWrites) {
            count = 0;
        }
        written.append(reinterpret_cast<const char*>(data), count);
        return count;
    }

    void feed(const std::string& bytes) { rx_.append(bytes); }

    std::string written;
    size_t maxWritePerCall = 0;
    bool refuseWrites = false;

private:
    std::string rx_;
    size_t rxPos_ = 0;
};

class RecordingListener : public iridium::ModemListener {
public:
    void onModemIdleBytes(const uint8_t* data, size_t length) override {
        idle.append(reinterpret_cast<const char*>(data), length);
    }

    void onModemCommandComplete(iridium::CommandOutcome outcome) override {
        ++completions;
        lastOutcome = outcome;
    }

    std::string idle;
    int completions = 0;
    iridium::CommandOutcome lastOutcome = iridium::CommandOutcome::None;
};

inline std::string responseOf(const iridium::IridiumModem& modem) {
    return std::string(reinterpret_cast<const char*>(modem.lastResponse()), modem.lastResponseLength());
}

}
