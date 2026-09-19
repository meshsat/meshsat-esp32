#pragma once

#include <cstddef>
#include <cstdint>

// Seeed Studio XIAO ESP32-S3 + Wio-SX1262 for XIAO ESP32-S3 (B2B connector)
// + RockBLOCK 9603 on the XIAO header pins D6/D7.
//
// Every GPIO number the firmware uses is defined here and nowhere else.
// The source behind each number is in docs/REFERENCES.md.

namespace meshsat::board {

constexpr const char* kBoardName = "xiao-esp32s3-wio-sx1262";

// ---------------------------------------------------------------------------
// RockBLOCK 9603 (Iridium SBD), 3-wire UART: TX, RX, GND.
// ---------------------------------------------------------------------------

// XIAO D6 = GPIO43, drives RockBLOCK pin 6 (TXD, input to the RockBLOCK).
constexpr int kModemTxPin = 43;

// XIAO D7 = GPIO44, reads RockBLOCK pin 1 (RXD, output from the RockBLOCK).
constexpr int kModemRxPin = 44;

// UART1, routed to GPIO43/44 through the GPIO matrix. GPIO43/44 are UART0's
// default pins, and UART0 stays the ESP-IDF console: after the UART1 pin
// assignment, IDF log output no longer reaches the modem line.
constexpr int kModemUartNumber = 1;

// Ground Control: 19200 baud, 8 data bits, no parity, 1 stop bit, flow control off.
constexpr uint32_t kModemBaudRate = 19200;

// Driver ring buffers, set before the UART is started. The transmit buffer lets
// writes return at once instead of waiting on the 128-byte hardware FIFO.
constexpr size_t kModemRxBufferBytes = 512;
constexpr size_t kModemTxBufferBytes = 256;

// ---------------------------------------------------------------------------
// Wio-SX1262 LoRa radio. Reserved: nothing in this firmware configures or
// drives these pins yet. Values match the Wio-SX1262 schematic, the XIAO
// ESP32-S3 v1.4 B2B connector (J3) and upstream Meshtastic's seeed_xiao_s3 variant.
// ---------------------------------------------------------------------------

// SPI, shared with the XIAO header pins D8 (SCK), D9 (MISO), D10 (MOSI).
constexpr int kLoraSckPin = 7;
constexpr int kLoraMisoPin = 8;
constexpr int kLoraMosiPin = 9;

// Radio control lines on the B2B connector.
constexpr int kLoraNssPin = 41;
constexpr int kLoraResetPin = 42;
constexpr int kLoraBusyPin = 40;
constexpr int kLoraDio1Pin = 39;
constexpr int kLoraRfSwitchPin = 38;

// Wio-SX1262 user button (10 k pull-up, pressed = low). The XIAO's own user
// LED is on the same GPIO.
constexpr int kUserButtonPin = 21;

// Wio-SX1262 green LED, anode on GPIO48, lit when the pin is high.
constexpr int kWioLedPin = 48;

// ---------------------------------------------------------------------------
// Compile-time checks: the modem must not share a pin with the radio, the
// board's button/LED, the USB lines or an ESP32-S3 strapping pin.
// ---------------------------------------------------------------------------

constexpr int kReservedPins[] = {
    kLoraSckPin,  kLoraMisoPin, kLoraMosiPin,     kLoraNssPin,    kLoraResetPin,
    kLoraBusyPin, kLoraDio1Pin, kLoraRfSwitchPin, kUserButtonPin, kWioLedPin,
};

// ESP32-S3 USB D- / D+.
constexpr int kUsbPins[] = {19, 20};

// ESP32-S3 datasheet, "Boot Configurations": GPIO0, GPIO3, GPIO45, GPIO46.
constexpr int kStrappingPins[] = {0, 3, 45, 46};

template <size_t N>
constexpr bool pinInList(int pin, const int (&list)[N]) {
    for (size_t i = 0; i < N; ++i) {
        if (list[i] == pin) {
            return true;
        }
    }
    return false;
}

constexpr bool pinIsFreeForModem(int pin) {
    return !pinInList(pin, kReservedPins) && !pinInList(pin, kUsbPins) && !pinInList(pin, kStrappingPins);
}

static_assert(pinIsFreeForModem(kModemTxPin), "modem TX pin collides with a reserved pin");
static_assert(pinIsFreeForModem(kModemRxPin), "modem RX pin collides with a reserved pin");
static_assert(kModemTxPin != kModemRxPin, "modem TX and RX must be different pins");

}
