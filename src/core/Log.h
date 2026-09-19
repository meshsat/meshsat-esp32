#pragma once

#include <Print.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>

namespace meshsat::core {

// Timestamped diagnostic lines on the USB console. Formatting uses fixed
// stack buffers; nothing is allocated on the heap.
class Log {
public:
    static void begin(Print& out);

    static void info(const char* format, ...) __attribute__((format(printf, 1, 2)));
    static void warn(const char* format, ...) __attribute__((format(printf, 1, 2)));
    static void error(const char* format, ...) __attribute__((format(printf, 1, 2)));

    // One log line: `label` followed by `data` as a quoted string, with
    // \r, \n, \t, \\, \" and \xNN escapes so every byte is visible.
    static void escaped(const char* label, const uint8_t* data, size_t length);

    // Bytes or text exactly as given: no timestamp, no escaping, no newline.
    static void raw(const uint8_t* data, size_t length);
    static void raw(const char* text);

private:
    static void line(char level, const char* format, va_list args);
    static void timestamp(char level);

    static Print* out_;
};

}
