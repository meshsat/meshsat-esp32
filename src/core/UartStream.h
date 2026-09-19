#pragma once

#include <HardwareSerial.h>

#include "core/ByteStream.h"

namespace meshsat::core {

// ByteStream over an Arduino HardwareSerial port. write() is capped at the
// space the driver reports, so it does not wait for the UART to drain.
//
// Arduino core 3.3 reports the hardware FIFO space instead of 0 when the
// transmit ring buffer is completely full, so in that one state a write can
// wait a few byte times (about 0.5 ms each at 19200 baud). An AT command
// cannot get there, since the largest (128 characters plus CR) fits in the
// empty ring buffer (board::kModemTxBufferBytes); a long paste in the debug
// pass-through can.
class UartStream : public ByteStream {
public:
    explicit UartStream(HardwareSerial& serial) : serial_(serial) {}

    size_t available() override {
        const int count = serial_.available();
        return count > 0 ? static_cast<size_t>(count) : 0;
    }

    int read() override { return serial_.read(); }

    size_t write(const uint8_t* data, size_t length) override {
        const int space = serial_.availableForWrite();
        if (space <= 0 || length == 0) {
            return 0;
        }
        const size_t chunk = length < static_cast<size_t>(space) ? length : static_cast<size_t>(space);
        return serial_.write(data, chunk);
    }

private:
    HardwareSerial& serial_;
};

}
