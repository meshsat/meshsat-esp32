#pragma once

#include <cstddef>
#include <cstdint>

namespace meshsat::core {

// A byte link to a peripheral. Implementations never block: read() returns -1
// when nothing is buffered, and write() takes only what fits right now and
// returns how many bytes it accepted.
//
// Protocol code (iridium/, later radio/ and bluetooth/) talks to hardware only
// through this interface, so it builds and runs in host unit tests.
class ByteStream {
public:
    virtual ~ByteStream() = default;

    virtual size_t available() = 0;
    virtual int read() = 0;
    virtual size_t write(const uint8_t* data, size_t length) = 0;
};

}
