#pragma once

#include <cstddef>
#include <cstdint>

namespace meshsat::core {

// Fixed-size byte FIFO for a single task. Not safe for concurrent use.
template <size_t Capacity>
class ByteRing {
public:
    static_assert(Capacity > 0, "ByteRing needs a capacity");

    size_t size() const { return count_; }
    size_t space() const { return Capacity - count_; }
    bool empty() const { return count_ == 0; }

    void clear() {
        head_ = 0;
        count_ = 0;
    }

    // Appends as many bytes as fit and returns how many were taken.
    size_t push(const uint8_t* data, size_t length) {
        size_t taken = 0;
        while (taken < length && count_ < Capacity) {
            buffer_[(head_ + count_) % Capacity] = data[taken++];
            ++count_;
        }
        return taken;
    }

    int peek() const { return count_ == 0 ? -1 : buffer_[head_]; }

    int pop() {
        if (count_ == 0) {
            return -1;
        }
        const uint8_t value = buffer_[head_];
        head_ = (head_ + 1) % Capacity;
        --count_;
        return value;
    }

    // Copies up to `length` bytes from the front without removing them.
    size_t copyFront(uint8_t* out, size_t length) const {
        const size_t count = length < count_ ? length : count_;
        for (size_t i = 0; i < count; ++i) {
            out[i] = buffer_[(head_ + i) % Capacity];
        }
        return count;
    }

    void drop(size_t length) {
        const size_t count = length < count_ ? length : count_;
        head_ = (head_ + count) % Capacity;
        count_ -= count;
    }

private:
    uint8_t buffer_[Capacity] = {};
    size_t head_ = 0;
    size_t count_ = 0;
};

}
