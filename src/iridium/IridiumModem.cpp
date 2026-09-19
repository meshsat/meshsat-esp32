#include "iridium/IridiumModem.h"

#include <cstring>

namespace meshsat::iridium {

namespace {

// The 9603 command line ends with a carriage return only.
constexpr uint8_t kCommandTerminator = '\r';

}

const char* toString(CommandOutcome outcome) {
    switch (outcome) {
        case CommandOutcome::None:
            return "none";
        case CommandOutcome::Ok:
            return "OK";
        case CommandOutcome::Error:
            return "ERROR";
        case CommandOutcome::Timeout:
            return "timeout";
    }
    return "unknown";
}

IridiumModem::IridiumModem(core::ByteStream& link) : link_(link) {}

void IridiumModem::setListener(ModemListener* listener) { listener_ = listener; }

void IridiumModem::begin() {
    parser_.reset();
    inFlight_ = false;
    finalPending_ = false;
    pendingOutcome_ = CommandOutcome::None;
    txHead_ = 0;
    txTail_ = 0;
    command_[0] = '\0';
    responseLength_ = 0;
    responseTruncated_ = false;
    lastOutcome_ = CommandOutcome::None;
    lastDurationMs_ = 0;
}

void IridiumModem::poll(uint32_t nowMs) {
    flushTx();
    receive(nowMs);
    if (!inFlight_) {
        return;
    }
    if (finalPending_) {
        if (static_cast<uint32_t>(nowMs - finalSeenMs_) >= kFinalLineGraceMs) {
            complete(pendingOutcome_, finalSeenMs_);
        }
        return;
    }
    if (static_cast<uint32_t>(nowMs - commandStartMs_) >= commandTimeoutMs_) {
        complete(CommandOutcome::Timeout, nowMs);
    }
}

bool IridiumModem::sendCommand(const char* command, uint32_t timeoutMs, uint32_t nowMs) {
    if (inFlight_ || transmitPending() || command == nullptr) {
        return false;
    }
    size_t length = 0;
    while (length <= kMaxCommandLength && command[length] != '\0') {
        ++length;
    }
    if (length == 0 || length > kMaxCommandLength) {
        return false;
    }
    if (std::memchr(command, '\r', length) != nullptr || std::memchr(command, '\n', length) != nullptr) {
        return false;
    }

    drainStaleInput();

    std::memcpy(command_, command, length);
    command_[length] = '\0';
    parser_.reset();
    responseLength_ = 0;
    responseTruncated_ = false;
    lastOutcome_ = CommandOutcome::None;
    lastDurationMs_ = 0;
    finalPending_ = false;

    queue(reinterpret_cast<const uint8_t*>(command_), length);
    queue(&kCommandTerminator, 1);

    inFlight_ = true;
    commandStartMs_ = nowMs;
    commandTimeoutMs_ = timeoutMs;
    flushTx();
    return true;
}

size_t IridiumModem::writeRaw(const uint8_t* data, size_t length) {
    if (inFlight_ || data == nullptr || length == 0) {
        return 0;
    }
    const size_t accepted = queue(data, length);
    flushTx();
    return accepted;
}

bool IridiumModem::commandInFlight() const { return inFlight_; }

bool IridiumModem::transmitPending() const { return txHead_ != txTail_; }

CommandOutcome IridiumModem::lastOutcome() const { return lastOutcome_; }

const char* IridiumModem::lastCommand() const { return command_; }

const uint8_t* IridiumModem::lastResponse() const { return response_; }

size_t IridiumModem::lastResponseLength() const { return responseLength_; }

bool IridiumModem::lastResponseTruncated() const { return responseTruncated_; }

uint32_t IridiumModem::lastDurationMs() const { return lastDurationMs_; }

size_t IridiumModem::queue(const uint8_t* data, size_t length) {
    if (txHead_ > 0 && txTail_ + length > kTxCapacity) {
        const size_t pending = txTail_ - txHead_;
        std::memmove(tx_, tx_ + txHead_, pending);
        txHead_ = 0;
        txTail_ = pending;
    }
    const size_t space = kTxCapacity - txTail_;
    const size_t count = length < space ? length : space;
    std::memcpy(tx_ + txTail_, data, count);
    txTail_ += count;
    return count;
}

void IridiumModem::flushTx() {
    while (txHead_ < txTail_) {
        const size_t pending = txTail_ - txHead_;
        const size_t written = link_.write(tx_ + txHead_, pending);
        if (written == 0) {
            break;
        }
        txHead_ += written < pending ? written : pending;
    }
    if (txHead_ == txTail_) {
        txHead_ = 0;
        txTail_ = 0;
    }
}

size_t IridiumModem::readChunk(uint8_t* out, size_t capacity) {
    size_t count = 0;
    while (count < capacity && link_.available() > 0) {
        const int value = link_.read();
        if (value < 0) {
            break;
        }
        out[count++] = static_cast<uint8_t>(value);
    }
    return count;
}

void IridiumModem::drainStaleInput() {
    uint8_t chunk[kMaxBytesPerPoll];
    size_t drained = 0;
    while (drained < kMaxStaleDrainBytes) {
        const size_t count = readChunk(chunk, sizeof(chunk));
        if (count == 0) {
            break;
        }
        drained += count;
        deliverIdle(chunk, count);
    }
}

void IridiumModem::receive(uint32_t nowMs) {
    uint8_t chunk[kMaxBytesPerPoll];
    const size_t count = readChunk(chunk, sizeof(chunk));
    size_t index = 0;

    while (index < count && inFlight_) {
        const uint8_t byte = chunk[index];

        if (finalPending_) {
            if (byte == '\n') {
                append(byte);
                ++index;
            }
            complete(pendingOutcome_, finalSeenMs_);
            break;
        }

        append(byte);
        ++index;
        const FinalResult result = parser_.feed(byte);
        if (result == FinalResult::None) {
            continue;
        }
        pendingOutcome_ = result == FinalResult::Ok ? CommandOutcome::Ok : CommandOutcome::Error;
        finalSeenMs_ = nowMs;
        if (byte == '\r') {
            finalPending_ = true;
            continue;
        }
        complete(pendingOutcome_, nowMs);
        break;
    }

    // Whatever follows the end of a reply is not part of it, even if the
    // listener started a new command from its completion callback.
    if (index < count) {
        deliverIdle(chunk + index, count - index);
    }
}

void IridiumModem::append(uint8_t byte) {
    if (responseLength_ < kResponseCapacity) {
        response_[responseLength_++] = byte;
    } else {
        responseTruncated_ = true;
    }
}

void IridiumModem::complete(CommandOutcome outcome, uint32_t atMs) {
    inFlight_ = false;
    finalPending_ = false;
    lastOutcome_ = outcome;
    lastDurationMs_ = atMs - commandStartMs_;
    if (outcome == CommandOutcome::Timeout) {
        // Drop any part of the command the UART never took, so it cannot
        // reach the modem after the caller has given up on it.
        txHead_ = 0;
        txTail_ = 0;
    }
    if (listener_ != nullptr) {
        listener_->onModemCommandComplete(outcome);
    }
}

void IridiumModem::deliverIdle(const uint8_t* data, size_t length) {
    if (listener_ != nullptr && length > 0) {
        listener_->onModemIdleBytes(data, length);
    }
}

}
