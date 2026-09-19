#include "iridium/AtResponseParser.h"

#include <cstring>

namespace meshsat::iridium {

void AtResponseParser::reset() {
    length_ = 0;
    overflow_ = false;
    line_[0] = '\0';
}

FinalResult AtResponseParser::feed(uint8_t byte) {
    if (byte == '\r' || byte == '\n') {
        return finishLine();
    }
    if (length_ < kMaxLineLength) {
        line_[length_++] = static_cast<char>(byte);
    } else {
        // Too long to be a result code. Drop the rest of the line.
        overflow_ = true;
    }
    return FinalResult::None;
}

FinalResult AtResponseParser::finishLine() {
    FinalResult result = FinalResult::None;
    if (!overflow_ && length_ > 0) {
        line_[length_] = '\0';
        if (std::strcmp(line_, "OK") == 0) {
            result = FinalResult::Ok;
        } else if (std::strcmp(line_, "ERROR") == 0) {
            result = FinalResult::Error;
        }
    }
    reset();
    return result;
}

}
