#include "diag/ModemDebugApp.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_periph.h>
#include <soc/gpio_struct.h>
#include <soc/io_mux_reg.h>
#include <soc/soc.h>
#include <soc/uart_periph.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_config.h"
#include "core/Log.h"
#include "core/Version.h"

namespace meshsat::diag {

namespace {

using core::Log;
using iridium::CommandOutcome;

constexpr uint8_t kBackspace = 0x08;
constexpr uint8_t kDelete = 0x7f;

// The link test sends exactly "AT" plus the driver's single CR terminator.
constexpr const char* kAtCommand = "AT";

// Wrap-safe "has `deadline` been reached" for millis() values.
bool reached(uint32_t nowMs, uint32_t deadlineMs) { return static_cast<int32_t>(nowMs - deadlineMs) >= 0; }

bool equalsIgnoreCase(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

}

ModemDebugApp::ModemDebugApp(Stream& console, iridium::IridiumModem& modem) : console_(console), modem_(modem) {}

void ModemDebugApp::begin(uint32_t nowMs) {
    printBanner();
    testScheduled_ = true;
    nextTestMs_ = nowMs + kFirstTestDelayMs;
    Log::info("first AT test in %lu s (the RockBLOCK needs ~10 s after power-on); type help for commands",
              static_cast<unsigned long>(kFirstTestDelayMs / 1000));
}

void ModemDebugApp::poll(uint32_t nowMs) {
    readConsole(nowMs);

    if (selfTestActive_ && reached(nowMs, selfTestEndMs_)) {
        finishSelfTest();
    }

    if (testCompleted_) {
        testCompleted_ = false;
        reportAtTest(completedOutcome_);
        scheduleNextTest(nowMs);
    }

    if (mode_ == Mode::Command && testScheduled_ && !testInFlight_ && reached(nowMs, nextTestMs_)) {
        startAtTest(nowMs, testsRun_ == 0 ? "boot" : "auto");
    }
}

void ModemDebugApp::onModemIdleBytes(const uint8_t* data, size_t length) {
    if (selfTestActive_) {
        for (size_t i = 0; i < length && selfTestRxLength_ < kSelfTestCapacity; ++i) {
            selfTestRx_[selfTestRxLength_++] = data[i];
        }
        return;
    }
    if (mode_ == Mode::Passthrough) {
        Log::raw(data, length);
        return;
    }
    Log::escaped("modem, outside a command", data, length);
}

void ModemDebugApp::onModemCommandComplete(CommandOutcome outcome) {
    if (!testInFlight_) {
        return;
    }
    testInFlight_ = false;
    testCompleted_ = true;
    completedOutcome_ = outcome;
}

void ModemDebugApp::readConsole(uint32_t nowMs) {
    for (size_t count = 0; count < kMaxConsoleBytesPerPoll && console_.available() > 0; ++count) {
        const int value = console_.read();
        if (value < 0) {
            break;
        }
        const uint8_t byte = static_cast<uint8_t>(value);
        if (mode_ == Mode::Passthrough) {
            handlePassthroughByte(byte, nowMs);
        } else {
            handleCommandByte(byte, nowMs);
        }
    }
}

void ModemDebugApp::handleCommandByte(uint8_t byte, uint32_t nowMs) {
    if (byte == '\n' && lastByteWasCr_) {
        lastByteWasCr_ = false;
        return;
    }
    lastByteWasCr_ = byte == '\r';

    if (byte == '\r' || byte == '\n') {
        console_.print("\r\n");
        runCommandLine(nowMs);
        lineLength_ = 0;
        lineOverflow_ = false;
        return;
    }
    if (byte == kBackspace || byte == kDelete) {
        if (lineLength_ > 0) {
            --lineLength_;
            console_.print("\b \b");
        }
        return;
    }
    if (byte < 0x20 || byte > 0x7e) {
        return;
    }
    if (lineLength_ < kConsoleLineCapacity) {
        line_[lineLength_++] = static_cast<char>(byte);
        console_.write(byte);
    } else {
        lineOverflow_ = true;
    }
}

void ModemDebugApp::handlePassthroughByte(uint8_t byte, uint32_t nowMs) {
    // Any terminal line ending (CR, LF or CR LF) reaches the modem as exactly
    // one CR: the 9603 command line ends with CR, and a LF does not work.
    if (byte == '\n' && lastByteWasCr_) {
        lastByteWasCr_ = false;
        return;
    }
    lastByteWasCr_ = byte == '\r';

    if (tildePending_) {
        tildePending_ = false;
        if (byte == '.') {
            exitPassthrough(nowMs);
            return;
        }
        forwardToModem('~');
        atLineStart_ = false;
    } else if (atLineStart_ && byte == '~') {
        tildePending_ = true;
        return;
    }

    if (byte == '\r' || byte == '\n') {
        forwardToModem('\r');
        atLineStart_ = true;
        return;
    }
    forwardToModem(byte);
    atLineStart_ = false;
}

void ModemDebugApp::runCommandLine(uint32_t nowMs) {
    if (lineOverflow_) {
        Log::warn("line longer than %u characters, ignored", static_cast<unsigned>(kConsoleLineCapacity));
        return;
    }
    line_[lineLength_] = '\0';

    char* command = line_;
    while (*command == ' ') {
        ++command;
    }
    if (*command == '\0') {
        return;
    }
    char* argument = command;
    while (*argument != '\0' && *argument != ' ') {
        ++argument;
    }
    if (*argument == ' ') {
        *argument++ = '\0';
        while (*argument == ' ') {
            ++argument;
        }
    }

    if (equalsIgnoreCase(command, "help") || equalsIgnoreCase(command, "?")) {
        printHelp();
    } else if (equalsIgnoreCase(command, "test") || equalsIgnoreCase(command, "at")) {
        if (testInFlight_) {
            Log::warn("an AT test is already running");
        } else {
            startAtTest(nowMs, "manual");
        }
    } else if (equalsIgnoreCase(command, "auto")) {
        setAutoInterval(argument, nowMs);
    } else if (equalsIgnoreCase(command, "pass")) {
        enterPassthrough();
    } else if (equalsIgnoreCase(command, "status")) {
        printStatus(nowMs);
    } else if (equalsIgnoreCase(command, "selftest")) {
        startSelfTest(nowMs);
    } else if (equalsIgnoreCase(command, "line")) {
        if (testInFlight_) {
            Log::warn("an AT test is running, try again in a moment");
        } else {
            probeRxLine();
        }
    } else {
        Log::warn("unknown command '%s', type help", command);
    }
}

void ModemDebugApp::setAutoInterval(const char* argument, uint32_t nowMs) {
    if (*argument == '\0') {
        if (autoIntervalMs_ == 0) {
            Log::info("automatic test is off");
        } else {
            Log::info("automatic test every %lu s", static_cast<unsigned long>(autoIntervalMs_ / 1000));
        }
        return;
    }
    if (equalsIgnoreCase(argument, "off") || std::strcmp(argument, "0") == 0) {
        autoIntervalMs_ = 0;
        testScheduled_ = false;
        Log::info("automatic test off");
        return;
    }
    char* end = nullptr;
    const unsigned long seconds = std::strtoul(argument, &end, 10);
    const unsigned long minSeconds = kMinAutoIntervalMs / 1000;
    const unsigned long maxSeconds = kMaxAutoIntervalMs / 1000;
    if (end == argument || *end != '\0' || seconds < minSeconds || seconds > maxSeconds) {
        Log::warn("auto takes a number of seconds from %lu to %lu, or off", minSeconds, maxSeconds);
        return;
    }
    autoIntervalMs_ = static_cast<uint32_t>(seconds * 1000);
    Log::info("automatic test every %lu s", seconds);
    if (!testInFlight_) {
        scheduleNextTest(nowMs);
    }
}

void ModemDebugApp::startAtTest(uint32_t nowMs, const char* trigger) {
    testScheduled_ = false;
    if (!modem_.sendCommand(kAtCommand, kAtTimeoutMs, nowMs)) {
        Log::error("AT test not started: the modem driver is busy");
        scheduleNextTest(nowMs);
        return;
    }
    testInFlight_ = true;
    ++testsRun_;
    Log::info("AT test #%lu (%s): sent \"AT\\r\" on UART%d at %lu 8N1, waiting up to %lu ms",
              static_cast<unsigned long>(testsRun_), trigger, board::kModemUartNumber,
              static_cast<unsigned long>(board::kModemBaudRate), static_cast<unsigned long>(kAtTimeoutMs));
}

void ModemDebugApp::reportAtTest(CommandOutcome outcome) {
    lastTestOutcome_ = outcome;
    const unsigned long number = testsRun_;
    const unsigned long elapsed = modem_.lastDurationMs();
    const size_t length = modem_.lastResponseLength();

    char label[40];
    snprintf(label, sizeof(label), "AT test #%lu raw reply", number);
    Log::escaped(label, modem_.lastResponse(), length);
    if (modem_.lastResponseTruncated()) {
        Log::warn("AT test #%lu: reply longer than %u bytes, the rest was not kept", number,
                  static_cast<unsigned>(iridium::IridiumModem::kResponseCapacity));
    }

    switch (outcome) {
        case CommandOutcome::Ok:
            ++testsPassed_;
            Log::info("AT test #%lu: PASS, OK received after %lu ms", number, elapsed);
            return;
        case CommandOutcome::Error:
            ++testsFailed_;
            Log::warn("AT test #%lu: FAIL, the modem answered ERROR after %lu ms", number, elapsed);
            Log::warn("  the link itself works (bytes came back); try AT again in pass-through");
            return;
        case CommandOutcome::Timeout:
        case CommandOutcome::None:
            break;
    }

    ++testsFailed_;
    if (length == 0) {
        Log::warn("AT test #%lu: FAIL, no reply within %lu ms", number, elapsed);
        Log::warn("  wiring: RockBLOCK pin 1 RXD (modem output) -> XIAO D7/GPIO%d, pin 6 TXD (modem input) <- XIAO "
                  "D6/GPIO%d, pin 10 GND -> XIAO GND",
                  board::kModemRxPin, board::kModemTxPin);
        Log::warn("  RX and TX swapped is the most common fault (Ground Control FAQ)");
        Log::warn("  power: 5 V on RockBLOCK pin 8 from a supply good for >= 500 mA; allow ~10 s after power-on");
        Log::warn("  still silent: power the RockBLOCK off for at least 2 s, then on (Iridium 9603 Developer's "
                  "Guide 3.2.1)");
        Log::warn("  flow control: in pass-through send AT&K0 (Ground Control FAQ: needed in 3-wire mode)");
    } else {
        Log::warn("AT test #%lu: FAIL, %u bytes came back but no OK line within %lu ms", number,
                  static_cast<unsigned>(length), elapsed);
        Log::warn("  garbage points to a wrong baud rate or noise on RX; a bare 0 means numeric result codes "
                  "(send ATV1 in pass-through)");
    }
}

void ModemDebugApp::scheduleNextTest(uint32_t nowMs) {
    if (autoIntervalMs_ == 0 || mode_ != Mode::Command) {
        testScheduled_ = false;
        return;
    }
    testScheduled_ = true;
    nextTestMs_ = nowMs + autoIntervalMs_;
}

void ModemDebugApp::enterPassthrough() {
    if (testInFlight_) {
        Log::warn("an AT test is running, try again in a moment");
        return;
    }
    mode_ = Mode::Passthrough;
    testScheduled_ = false;
    atLineStart_ = true;
    tildePending_ = false;
    lastByteWasCr_ = false;
    dropWarned_ = false;
    Log::info("pass-through ON: typed bytes go to the RockBLOCK, Enter sends one CR; type ~. at the start of a "
              "line to leave. Automatic tests are paused.");
    Log::info("nothing is echoed locally: with the modem's echo on (ATE1) your typing shows as the modem "
              "receives it");
}

void ModemDebugApp::exitPassthrough(uint32_t nowMs) {
    mode_ = Mode::Command;
    lineLength_ = 0;
    lineOverflow_ = false;
    console_.print("\r\n");
    Log::info("pass-through OFF");
    scheduleNextTest(nowMs);
}

void ModemDebugApp::forwardToModem(uint8_t byte) {
    if (modem_.writeRaw(&byte, 1) == 1) {
        return;
    }
    ++droppedPassthroughBytes_;
    if (!dropWarned_) {
        dropWarned_ = true;
        Log::warn("modem transmit queue full, typed bytes are being dropped");
    }
}

void ModemDebugApp::probeRxLine() {
    // The RockBLOCK drives its RXD output (pin 1) high while idle. Swap the
    // RX pin's pull-up for a pull-down for a moment: if the pin still reads
    // high, something powered is driving the wire; if it falls low, nothing
    // is connected there. Nothing is driven, so this is safe with any wiring.
    const gpio_num_t rx = static_cast<gpio_num_t>(board::kModemRxPin);
    const int withPullUp = gpio_get_level(rx);

    gpio_pullup_dis(rx);
    gpio_pulldown_en(rx);
    delayMicroseconds(kLineSettleUs);
    int highCount = 0;
    for (int i = 0; i < kLineSamples; ++i) {
        highCount += gpio_get_level(rx);
        delayMicroseconds(kLineSampleGapUs);
    }
    gpio_pulldown_dis(rx);
    gpio_pullup_en(rx);

    Log::info("RX line GPIO%d (D7): reads %d with pull-up, high in %d of %d samples with pull-down", board::kModemRxPin,
              withPullUp, highCount, kLineSamples);
    if (highCount == kLineSamples) {
        Log::info("  driven high: a powered output is on this wire (expected: RockBLOCK pin 1 RXD)");
    } else if (highCount == 0) {
        Log::warn("  floating or held low: nothing drives this wire; check the D7 contact and RockBLOCK pin 1");
    } else {
        Log::warn("  mixed readings: data or noise on the line, run line again");
    }
}

void ModemDebugApp::printPinRouting() {
    const int uart = board::kModemUartNumber;
    const uint32_t tx = static_cast<uint32_t>(board::kModemTxPin);
    const uint32_t rx = static_cast<uint32_t>(board::kModemRxPin);
    const uint32_t txSignal = UART_PERIPH_SIGNAL(uart, SOC_UART_TX_PIN_IDX);
    const uint32_t rxSignal = UART_PERIPH_SIGNAL(uart, SOC_UART_RX_PIN_IDX);

    const uint32_t txMux = REG_READ(GPIO_PIN_MUX_REG[tx]);
    const uint32_t rxMux = REG_READ(GPIO_PIN_MUX_REG[rx]);
    const uint32_t txOutSignal = GPIO.func_out_sel_cfg[tx].func_sel;
    const uint32_t txOutputEnabled = tx >= 32 ? (GPIO.enable1.data >> (tx - 32)) & 1 : (GPIO.enable >> tx) & 1;
    const uint32_t rxInPad = GPIO.func_in_sel_cfg[rxSignal].func_sel;
    const uint32_t rxViaMatrix = GPIO.func_in_sel_cfg[rxSignal].sig_in_sel;

    const bool txOk = ((txMux >> MCU_SEL_S) & MCU_SEL_V) == PIN_FUNC_GPIO && txOutSignal == txSignal && txOutputEnabled;
    const bool rxOk = rxViaMatrix && rxInPad == rx && ((rxMux >> FUN_IE_S) & 1);

    Log::info("TX GPIO%lu: IO MUX function %lu (expect %d), matrix output signal %lu (expect UART%d TX = %lu), "
              "output enable %lu: %s",
              static_cast<unsigned long>(tx), static_cast<unsigned long>((txMux >> MCU_SEL_S) & MCU_SEL_V),
              PIN_FUNC_GPIO, static_cast<unsigned long>(txOutSignal), uart, static_cast<unsigned long>(txSignal),
              static_cast<unsigned long>(txOutputEnabled), txOk ? "OK" : "WRONG");
    Log::info("RX: UART%d RX signal %lu takes GPIO%lu %s (expect GPIO%lu via matrix); GPIO%lu input %lu, pull-up %lu, "
              "pull-down %lu: %s",
              uart, static_cast<unsigned long>(rxSignal), static_cast<unsigned long>(rxInPad),
              rxViaMatrix ? "via matrix" : "via IO MUX", static_cast<unsigned long>(rx), static_cast<unsigned long>(rx),
              static_cast<unsigned long>((rxMux >> FUN_IE_S) & 1), static_cast<unsigned long>((rxMux >> FUN_PU_S) & 1),
              static_cast<unsigned long>((rxMux >> FUN_PD_S) & 1), rxOk ? "OK" : "WRONG");
}

void ModemDebugApp::startSelfTest(uint32_t nowMs) {
    if (selfTestActive_ || testInFlight_ || modem_.commandInFlight() || modem_.transmitPending()) {
        Log::warn("the modem link is busy, try again in a moment");
        return;
    }
    printPinRouting();

    // Point the UART receiver at our own TX pad for a moment. Whatever the
    // UART really drives onto the TX pad comes straight back, so this checks the
    // firmware's transmit path without touching the wiring. The modem still
    // receives the same bytes on D6 as usual.
    const uint32_t rxSignal = UART_PERIPH_SIGNAL(board::kModemUartNumber, SOC_UART_RX_PIN_IDX);
    gpio_input_enable(static_cast<gpio_num_t>(board::kModemTxPin));
    esp_rom_gpio_connect_in_signal(board::kModemTxPin, rxSignal, false);

    selfTestActive_ = true;
    selfTestRxLength_ = 0;
    selfTestEndMs_ = nowMs + kSelfTestWindowMs;
    static const uint8_t kProbe[] = {'A', 'T', '\r'};
    const size_t queued = modem_.writeRaw(kProbe, sizeof(kProbe));
    Log::info("self-test: UART%d receiver now reads GPIO%d (its own TX); queued %u bytes \"AT\\r\"",
              board::kModemUartNumber, board::kModemTxPin, static_cast<unsigned>(queued));
}

void ModemDebugApp::finishSelfTest() {
    const uint32_t rxSignal = UART_PERIPH_SIGNAL(board::kModemUartNumber, SOC_UART_RX_PIN_IDX);
    esp_rom_gpio_connect_in_signal(board::kModemRxPin, rxSignal, false);
    selfTestActive_ = false;

    Log::escaped("self-test: read back from our TX pin", selfTestRx_, selfTestRxLength_);
    const bool pass = selfTestRxLength_ == 3 && std::memcmp(selfTestRx_, "AT\r", 3) == 0;
    if (pass) {
        Log::info("self-test: PASS, the firmware drives \"AT\\r\" onto GPIO%d at %lu baud; receiver back on GPIO%d",
                  board::kModemTxPin, static_cast<unsigned long>(board::kModemBaudRate), board::kModemRxPin);
    } else {
        Log::warn("self-test: FAIL, the UART did not read back its own \"AT\\r\"; receiver back on GPIO%d",
                  board::kModemRxPin);
    }
    printPinRouting();
}

void ModemDebugApp::printBanner() {
    Log::info("%s %s, board %s, milestone 1: RockBLOCK 9603 AT -> OK", core::kFirmwareName, core::kFirmwareVersion,
              board::kBoardName);
    Log::info("RockBLOCK on UART%d, %lu 8N1, no flow control: TX GPIO%d (D6) -> RockBLOCK pin 6 TXD, RX GPIO%d (D7) "
              "<- RockBLOCK pin 1 RXD",
              board::kModemUartNumber, static_cast<unsigned long>(board::kModemBaudRate), board::kModemTxPin,
              board::kModemRxPin);
    Log::info("SX1262 pins left untouched: SCK %d, MISO %d, MOSI %d, NSS %d, RESET %d, BUSY %d, DIO1 %d, RF switch %d",
              board::kLoraSckPin, board::kLoraMisoPin, board::kLoraMosiPin, board::kLoraNssPin, board::kLoraResetPin,
              board::kLoraBusyPin, board::kLoraDio1Pin, board::kLoraRfSwitchPin);
}

void ModemDebugApp::printHelp() {
    Log::info("commands (end each with Enter):");
    Log::info("  test     send AT to the RockBLOCK now, report OK / ERROR / timeout");
    Log::info("  auto N   repeat the test every N seconds (%lu to %lu); auto off stops it",
              static_cast<unsigned long>(kMinAutoIntervalMs / 1000),
              static_cast<unsigned long>(kMaxAutoIntervalMs / 1000));
    Log::info("  pass     pass-through to the RockBLOCK; Enter sends CR, ~. at line start leaves");
    Log::info("  status   counters, last result and settings");
    Log::info("  line     check whether anything drives the RX wire (GPIO%d, D7)", board::kModemRxPin);
    Log::info("  selftest show the UART pin routing and read back our own TX pin (no modem needed)");
    Log::info("  help     this list");
}

void ModemDebugApp::printStatus(uint32_t nowMs) {
    printBanner();
    Log::info("uptime %lu s, mode %s", static_cast<unsigned long>(nowMs / 1000),
              mode_ == Mode::Command ? "command" : "pass-through");
    Log::info("AT tests run %lu, passed %lu, failed %lu, last result %s", static_cast<unsigned long>(testsRun_),
              static_cast<unsigned long>(testsPassed_), static_cast<unsigned long>(testsFailed_),
              iridium::toString(lastTestOutcome_));
    if (testInFlight_) {
        Log::info("an AT test is running");
    } else if (testScheduled_) {
        const uint32_t remainingMs = reached(nowMs, nextTestMs_) ? 0 : nextTestMs_ - nowMs;
        Log::info("next automatic test in %lu s", static_cast<unsigned long>(remainingMs / 1000));
    } else {
        Log::info("no automatic test scheduled (auto N to start one)");
    }
    if (droppedPassthroughBytes_ > 0) {
        Log::info("pass-through bytes dropped: %lu", static_cast<unsigned long>(droppedPassthroughBytes_));
    }
}

}
