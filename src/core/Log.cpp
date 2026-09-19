#include "core/Log.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace meshsat::core {

namespace {

constexpr size_t kLineBufferBytes = 192;
constexpr size_t kEscapeChunkBytes = 96;

// Worst case for one input byte is "\xNN", four characters.
constexpr size_t kMaxEscapedBytes = 4;

size_t escapeByte(uint8_t byte, char* out) {
    switch (byte) {
        case '\r':
            out[0] = '\\';
            out[1] = 'r';
            return 2;
        case '\n':
            out[0] = '\\';
            out[1] = 'n';
            return 2;
        case '\t':
            out[0] = '\\';
            out[1] = 't';
            return 2;
        case '\\':
        case '"':
            out[0] = '\\';
            out[1] = static_cast<char>(byte);
            return 2;
        default:
            break;
    }
    if (byte >= 0x20 && byte < 0x7f) {
        out[0] = static_cast<char>(byte);
        return 1;
    }
    static const char kHex[] = "0123456789ABCDEF";
    out[0] = '\\';
    out[1] = 'x';
    out[2] = kHex[byte >> 4];
    out[3] = kHex[byte & 0x0f];
    return 4;
}

}

Print* Log::out_ = nullptr;

void Log::begin(Print& out) { out_ = &out; }

void Log::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    line('I', format, args);
    va_end(args);
}

void Log::warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    line('W', format, args);
    va_end(args);
}

void Log::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    line('E', format, args);
    va_end(args);
}

void Log::escaped(const char* label, const uint8_t* data, size_t length) {
    if (out_ == nullptr) {
        return;
    }
    timestamp('I');
    out_->print(label);
    out_->print(" (");
    out_->print(static_cast<unsigned>(length));
    out_->print(length == 1 ? " byte): \"" : " bytes): \"");

    char chunk[kEscapeChunkBytes];
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        if (used + kMaxEscapedBytes > sizeof(chunk)) {
            out_->write(reinterpret_cast<const uint8_t*>(chunk), used);
            used = 0;
        }
        used += escapeByte(data[i], chunk + used);
    }
    if (used > 0) {
        out_->write(reinterpret_cast<const uint8_t*>(chunk), used);
    }
    out_->print("\"\r\n");
}

void Log::raw(const uint8_t* data, size_t length) {
    if (out_ == nullptr || length == 0) {
        return;
    }
    out_->write(data, length);
}

void Log::raw(const char* text) {
    if (out_ == nullptr) {
        return;
    }
    out_->print(text);
}

void Log::line(char level, const char* format, va_list args) {
    if (out_ == nullptr) {
        return;
    }
    char buffer[kLineBufferBytes];
    const int written = vsnprintf(buffer, sizeof(buffer), format, args);
    timestamp(level);
    out_->print(buffer);
    if (written >= static_cast<int>(sizeof(buffer))) {
        out_->print(" [...]");
    }
    out_->print("\r\n");
}

void Log::timestamp(char level) {
    const uint32_t now = millis();
    char prefix[24];
    snprintf(prefix, sizeof(prefix), "[%7lu.%03lu] %c ", static_cast<unsigned long>(now / 1000),
             static_cast<unsigned long>(now % 1000), level);
    out_->print(prefix);
}

}
