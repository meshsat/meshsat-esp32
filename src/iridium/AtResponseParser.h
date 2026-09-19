#pragma once

#include <cstddef>
#include <cstdint>

namespace meshsat::iridium {

enum class FinalResult : uint8_t {
    None,
    Ok,
    Error,
};

// Splits modem output into lines (ended by CR or LF) and reports the final
// result code that ends an AT command: a line that is exactly "OK" or exactly
// "ERROR". Every other line, including the modem's echo of the command, is
// skipped.
//
// Only the verbose result codes are recognised. A modem switched to numeric
// codes with ATV0 answers 0 / 4 instead, which this parser does not treat as
// final (AT+SBDWB and others print a bare "0" status line before their OK),
// so such a modem reads as a timeout until ATV1 is sent.
class AtResponseParser {
public:
    static constexpr size_t kMaxLineLength = 64;

    void reset();

    // Feeds one received byte. Returns Ok or Error on the byte that completes
    // a final result line, None otherwise.
    FinalResult feed(uint8_t byte);

private:
    FinalResult finishLine();

    char line_[kMaxLineLength + 1] = {};
    size_t length_ = 0;
    bool overflow_ = false;
};

}
