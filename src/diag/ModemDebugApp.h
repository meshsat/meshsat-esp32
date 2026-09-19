#pragma once

#include <Stream.h>

#include <cstddef>
#include <cstdint>

#include "iridium/IridiumModem.h"

namespace meshsat::diag {

// Milestone 1 bench tool, driven from the USB serial console:
//
//   - the link test: send exactly "AT\r" to the RockBLOCK, log the raw reply,
//     report PASS when an OK line comes back and FAIL otherwise;
//   - a pass-through mode that forwards typed bytes to the modem and prints
//     whatever the modem sends back.
//
// Everything runs from poll(); nothing waits on the console or the modem.
class ModemDebugApp : public iridium::ModemListener {
public:
    // First automatic test after boot. Ground Control: when power is first
    // applied the RockBLOCK's supercapacitor charges and the Iridium module
    // starts about 10 s later, so a modem powered together with the XIAO
    // cannot answer sooner.
    static constexpr uint32_t kFirstTestDelayMs = 12000;

    // AT reply timeout. Matches the Bridge's 9603 transport
    // (meshsat internal/transport/direct_sat.go, iridiumReadTimeout = 3 s).
    static constexpr uint32_t kAtTimeoutMs = 3000;

    // Slow repeat interval of the automatic test; 0 turns it off.
    static constexpr uint32_t kDefaultAutoIntervalMs = 30000;
    static constexpr uint32_t kMinAutoIntervalMs = 5000;
    static constexpr uint32_t kMaxAutoIntervalMs = 3600000;

    static constexpr size_t kConsoleLineCapacity = 64;

    ModemDebugApp(Stream& console, iridium::IridiumModem& modem);

    void begin(uint32_t nowMs);
    void poll(uint32_t nowMs);

    void onModemIdleBytes(const uint8_t* data, size_t length) override;
    void onModemCommandComplete(iridium::CommandOutcome outcome) override;

private:
    enum class Mode : uint8_t {
        Command,
        Passthrough,
    };

    // Bytes read from the console per poll(), so one call stays short.
    static constexpr size_t kMaxConsoleBytesPerPoll = 64;

    // RX line probe: settle time after switching pulls, then samples spread
    // over about one byte time at 19200 baud (about 1 ms in total).
    static constexpr uint32_t kLineSettleUs = 200;
    static constexpr int kLineSamples = 8;
    static constexpr uint32_t kLineSampleGapUs = 100;

    // UART self-test: how long the receiver listens to its own TX pin.
    // "AT\r" takes about 1.6 ms at 19200 baud.
    static constexpr uint32_t kSelfTestWindowMs = 50;
    static constexpr size_t kSelfTestCapacity = 32;

    void readConsole(uint32_t nowMs);
    void handleCommandByte(uint8_t byte, uint32_t nowMs);
    void handlePassthroughByte(uint8_t byte, uint32_t nowMs);
    void runCommandLine(uint32_t nowMs);
    void setAutoInterval(const char* argument, uint32_t nowMs);

    void startAtTest(uint32_t nowMs, const char* trigger);
    void reportAtTest(iridium::CommandOutcome outcome);
    void scheduleNextTest(uint32_t nowMs);

    void enterPassthrough();
    void exitPassthrough(uint32_t nowMs);
    void forwardToModem(uint8_t byte);

    void probeRxLine();
    void printPinRouting();
    void startSelfTest(uint32_t nowMs);
    void finishSelfTest();

    void printBanner();
    void printHelp();
    void printStatus(uint32_t nowMs);

    Stream& console_;
    iridium::IridiumModem& modem_;
    Mode mode_ = Mode::Command;

    // Set by the completion callback, handled in poll() so the callback
    // itself does no logging.
    bool testCompleted_ = false;
    iridium::CommandOutcome completedOutcome_ = iridium::CommandOutcome::None;

    char line_[kConsoleLineCapacity + 1] = {};
    size_t lineLength_ = 0;
    bool lineOverflow_ = false;

    // Swallows the LF of a CR LF pair from the terminal.
    bool lastByteWasCr_ = false;

    // Pass-through escape "~." at the start of a line.
    bool atLineStart_ = true;
    bool tildePending_ = false;

    bool testInFlight_ = false;
    bool testScheduled_ = false;
    uint32_t nextTestMs_ = 0;
    uint32_t autoIntervalMs_ = kDefaultAutoIntervalMs;
    uint32_t testsRun_ = 0;
    uint32_t testsPassed_ = 0;
    uint32_t testsFailed_ = 0;
    iridium::CommandOutcome lastTestOutcome_ = iridium::CommandOutcome::None;
    uint32_t droppedPassthroughBytes_ = 0;
    bool dropWarned_ = false;

    bool selfTestActive_ = false;
    uint32_t selfTestEndMs_ = 0;
    uint8_t selfTestRx_[kSelfTestCapacity] = {};
    size_t selfTestRxLength_ = 0;
};

}
