#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ByteStream.h"
#include "iridium/AtResponseParser.h"

namespace meshsat::iridium {

enum class CommandOutcome : uint8_t {
    None,
    Ok,
    Error,
    Timeout,
};

const char* toString(CommandOutcome outcome);

// Receives what the modem driver sees. Callbacks run inside poll() or
// sendCommand() and must return quickly.
class ModemListener {
public:
    virtual ~ModemListener() = default;

    // Bytes that arrived while no AT command was in flight: unsolicited modem
    // output, line noise, or replies to bytes sent with writeRaw().
    virtual void onModemIdleBytes(const uint8_t* data, size_t length) = 0;

    // The command started with sendCommand() has finished. The raw reply
    // stays readable through lastResponse() until the next command starts.
    virtual void onModemCommandComplete(CommandOutcome outcome) = 0;
};

// Iridium 9603 on a RockBLOCK, over a 3-wire UART.
//
// Milestone 1 scope: one AT command at a time, ended by OK, ERROR or a
// timeout, plus raw writes for a debug pass-through. The SBD commands
// (AT&K0, AT+CSQ, AT+SBDWB, AT+SBDIX, AT+SBDRB, AT+SBDD) come later on top
// of sendCommand().
//
// Nothing here blocks or sleeps. Transmission and reception advance in
// poll(), which the caller runs from its main loop with the current time in
// milliseconds. Interval arithmetic is unsigned, so millis() wrap-around is
// harmless.
class IridiumModem {
public:
    // Ground Control, RockBLOCK AT commands: at most 128 characters per
    // command string, ended by CR (a line feed does not work).
    static constexpr size_t kMaxCommandLength = 128;
    static constexpr size_t kTxCapacity = 256;
    static constexpr size_t kResponseCapacity = 256;

    // Upper bound on bytes read per poll(), so one call stays short.
    static constexpr size_t kMaxBytesPerPoll = 64;

    // Upper bound on stale bytes discarded before a command. Larger than any
    // receive buffer the board configures.
    static constexpr size_t kMaxStaleDrainBytes = 1024;

    // A final result line normally ends in CR LF. After the CR, wait this long
    // for the LF so it lands in the reply instead of arriving as an idle byte.
    // At 19200 baud one byte takes about 0.5 ms.
    static constexpr uint32_t kFinalLineGraceMs = 10;

    explicit IridiumModem(core::ByteStream& link);

    void setListener(ModemListener* listener);

    // Clears all driver state. Call once before the first poll().
    void begin();

    void poll(uint32_t nowMs);

    // Sends `command` followed by exactly one carriage return, then waits
    // (through poll()) for OK, ERROR or `timeoutMs`. The command text carries
    // no terminator and must not contain CR or LF. Bytes already waiting in
    // the receive buffer go to the listener as idle bytes first, so a stale
    // byte is never taken for part of the reply.
    //
    // Returns false, and sends nothing, when a command is in flight, raw bytes
    // are still queued, or the command is empty, too long or contains CR/LF.
    bool sendCommand(const char* command, uint32_t timeoutMs, uint32_t nowMs);

    // Queues bytes for the modem exactly as given; no terminator is added.
    // Refused (returns 0) while an AT command is in flight. Returns how many
    // bytes were accepted, which is less than `length` if the queue is full.
    size_t writeRaw(const uint8_t* data, size_t length);

    bool commandInFlight() const;
    bool transmitPending() const;

    CommandOutcome lastOutcome() const;
    const char* lastCommand() const;
    const uint8_t* lastResponse() const;
    size_t lastResponseLength() const;
    bool lastResponseTruncated() const;
    uint32_t lastDurationMs() const;

private:
    size_t queue(const uint8_t* data, size_t length);
    void flushTx();
    size_t readChunk(uint8_t* out, size_t capacity);
    void drainStaleInput();
    void receive(uint32_t nowMs);
    void append(uint8_t byte);
    void complete(CommandOutcome outcome, uint32_t atMs);
    void deliverIdle(const uint8_t* data, size_t length);

    core::ByteStream& link_;
    ModemListener* listener_ = nullptr;
    AtResponseParser parser_;

    bool inFlight_ = false;
    uint32_t commandStartMs_ = 0;
    uint32_t commandTimeoutMs_ = 0;

    // Set when a final result line has ended with CR and its LF is awaited.
    bool finalPending_ = false;
    CommandOutcome pendingOutcome_ = CommandOutcome::None;
    uint32_t finalSeenMs_ = 0;

    uint8_t tx_[kTxCapacity] = {};
    size_t txHead_ = 0;
    size_t txTail_ = 0;

    char command_[kMaxCommandLength + 1] = {};
    uint8_t response_[kResponseCapacity] = {};
    size_t responseLength_ = 0;
    bool responseTruncated_ = false;
    CommandOutcome lastOutcome_ = CommandOutcome::None;
    uint32_t lastDurationMs_ = 0;
};

}
