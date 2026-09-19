#pragma once

#include <Stream.h>

namespace meshsat::core {

// One console over two links (USB and BLE): output goes to both, input is
// read from the first link that has a byte waiting. Typing on both at the
// same time interleaves, which is acceptable for a bench console.
class DualStream : public Stream {
public:
    DualStream(Stream& first, Stream& second) : first_(first), second_(second) {}

    int available() override { return first_.available() + second_.available(); }

    int read() override {
        if (first_.available() > 0) {
            return first_.read();
        }
        return second_.read();
    }

    int peek() override {
        if (first_.available() > 0) {
            return first_.peek();
        }
        return second_.peek();
    }

    size_t write(uint8_t byte) override {
        first_.write(byte);
        second_.write(byte);
        return 1;
    }

    size_t write(const uint8_t* data, size_t length) override {
        first_.write(data, length);
        second_.write(data, length);
        return length;
    }

    void flush() override {}

private:
    Stream& first_;
    Stream& second_;
};

}
