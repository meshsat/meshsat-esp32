// meshsat-esp32, milestone 1: ESP32-S3 UART -> RockBLOCK 9603 -> AT -> OK.
//
// The main loop only polls. Every module advances its own state machine
// from poll() and returns at once, so BLE, Wi-Fi and the LoRa radio can be
// added later as further pollers (or tasks) without reworking this one.

#include <Arduino.h>
#include <driver/gpio.h>

#include "bluetooth/BleConsole.h"
#include "board_config.h"
#include "core/DualStream.h"
#include "core/Log.h"
#include "core/UartStream.h"
#include "core/Version.h"
#include "diag/ModemDebugApp.h"
#include "iridium/IridiumModem.h"

// BLE console PIN, passed in at build time from the environment variable of
// the same name (platformio.ini) so it never lives in the repository. Empty
// or out of range: the BLE console stays off.
#ifndef MESHSAT_BLE_CONSOLE_PIN
#define MESHSAT_BLE_CONSOLE_PIN ""
#endif

namespace {

using namespace meshsat;

// USB console transmit buffer. The console never blocks: when no host is
// reading, output beyond this buffer is dropped instead of stalling the loop.
constexpr size_t kConsoleTxBufferBytes = 2048;

HardwareSerial modemUart(board::kModemUartNumber);
core::UartStream modemLink(modemUart);
iridium::IridiumModem modem(modemLink);

// One console over USB and BLE: output goes to both, input comes from either.
bluetooth::BleConsole bleConsole;
core::DualStream console(Serial, bleConsole);
diag::ModemDebugApp app(console, modem);

void beginConsole() {
    Serial.setTxBufferSize(kConsoleTxBufferBytes);
    Serial.setTxTimeoutMs(0);
    // Baud rate is meaningless for USB CDC; the value only satisfies the API.
    Serial.begin(115200);
    core::Log::begin(console);
}

void beginModemUart() {
    // Buffer sizes only take effect when set before begin().
    modemUart.setRxBufferSize(board::kModemRxBufferBytes);
    modemUart.setTxBufferSize(board::kModemTxBufferBytes);
    modemUart.begin(board::kModemBaudRate, SERIAL_8N1, board::kModemRxPin, board::kModemTxPin);
    modemUart.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
    // Keep RX idle-high when no modem is connected, so a floating wire does
    // not read as a stream of noise bytes.
    gpio_pullup_en(static_cast<gpio_num_t>(board::kModemRxPin));
}

void beginBleConsole() {
    if (bleConsole.begin(core::kFirmwareName, MESHSAT_BLE_CONSOLE_PIN)) {
        core::Log::info("BLE console: advertising as %s (Nordic UART Service); a client sends unlock <PIN> first",
                        bleConsole.deviceName());
        return;
    }
    core::Log::warn("BLE console off: no usable PIN built in (set MESHSAT_BLE_CONSOLE_PIN, %u to %u characters, "
                    "when building)",
                    static_cast<unsigned>(bluetooth::BleConsole::kMinPinLength),
                    static_cast<unsigned>(bluetooth::BleConsole::kMaxPinLength));
}

}

void setup() {
    beginConsole();
    beginModemUart();

    modem.setListener(&app);
    modem.begin();
    app.begin(millis());
    beginBleConsole();
}

void loop() {
    const uint32_t now = millis();
    modem.poll(now);
    bleConsole.poll(now);
    app.poll(now);
    // Give up the CPU for one tick so other tasks run. Nothing above waits
    // on I/O; the UART driver buffers bytes in the meantime.
    delay(1);
}
