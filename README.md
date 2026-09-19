# meshsat-esp32

Firmware for a small MeshSat node: a Seeed Studio XIAO ESP32-S3 with a Wio-SX1262 LoRa board and a RockBLOCK 9603 Iridium SBD modem. Later milestones add BLE (to the Android app), Wi-Fi and Meshtastic-compatible LoRa. This one does not.

```
                    XIAO ESP32-S3
                 ┌─────────────────┐
Android ← BLE ←──┤                 │   later
Internet ← WiFi ─┤                 │   later
SX1262 ← SPI ────┤                 │   later (pins reserved, never touched)
9603 ← UART ─────┤                 │   milestone 1: AT -> OK
                 └─────────────────┘
```

## Status: milestone 1

ESP32-S3 UART → RockBLOCK 9603 → `AT` → `OK`, plus a USB console with a manual test trigger and a pass-through to the modem. It builds, and the host unit tests pass. **It has not yet been run against a RockBLOCK.** The hardware checks are listed [below](#hardware-validation-still-to-do).

Sources, checked pin numbers, and the places where official documents disagree: [docs/REFERENCES.md](docs/REFERENCES.md).

## Hardware

| Part | Notes |
|---|---|
| Seeed Studio XIAO ESP32-S3 | ESP32-S3R8, 8 MB flash, 8 MB PSRAM |
| Wio-SX1262 for XIAO ESP32-S3 | connected through the XIAO's B2B connector (the ESP32-S3 version, not the header-pin nRF52840 version); EU 868 MHz |
| RockBLOCK 9603 | 10-pin Molex PicoBlade 1.25 mm, mating housing Molex 51021-1000 |
| 5 V supply for the RockBLOCK | must deliver at least 500 mA, see [Power](#power) |

## Wiring

RockBLOCK 9603 connector to XIAO ESP32-S3:

| RockBLOCK pin | Name | Direction | Connect to | GPIO |
|:---:|---|---|---|:---:|
| 1 | RXD | **output from** the RockBLOCK | XIAO **D7** (RX) | 44 |
| 6 | TXD | **input to** the RockBLOCK | XIAO **D6** (TX) | 43 |
| 10 | GND | ground | XIAO **GND** *and* the 5 V supply's ground | |
| 8 | 5V In | power in | 5 V supply, ≥ 500 mA (see [Power](#power)) | |
| 2 | CTS | output | not connected (flow control is off) | |
| 3 | RTS | input | not connected | |
| 4 | NetAv | output | not connected yet | |
| 5 | RI | output, active low | not connected yet | |
| 7 | OnOff | input | **not connected**. Floating means ON. See below. | |
| 9 | Li-Ion | power in | **not connected**. Never use it together with pin 8. | |

RockBLOCK pin 1 goes to the XIAO's **RX**, and pin 6 to the XIAO's **TX**. The RockBLOCK names its pins from the modem's side, so "RXD" is the modem's output. Ground Control's FAQ calls a swapped pair the most common wiring mistake.

Logic levels need no level shifter. The RockBLOCK's UART pins are 3.3 V and 5 V tolerant, and the XIAO drives 3.3 V.

**Do not connect OnOff (pin 7) to a XIAO GPIO.** The RockBLOCK 9603 Rev F reads OnOff against its supply voltage: ON needs at least supply − 0.5 V (about 4.5 V on 5 V) or a floating pin, and more than 1 µA of leakage can switch the modem off. A 3.3 V pin can do neither. Switching the modem from firmware later needs an open-drain (N-MOSFET) stage, the same one the Bridge kits need.

The SX1262 uses GPIO7, 8, 9, 38, 39, 40, 41 and 42. The Wio board's button and LED use GPIO21 and 48. None of these are D6/D7. The firmware does not configure or drive any of the radio pins.

## Power

Never power the RockBLOCK from the XIAO's 3V3 pin.

The RockBLOCK takes 3.0 to 5.4 V on pin 8 and needs a source that can deliver **at least 500 mA** (Ground Control; a weaker source browns out during transmit). Its supercapacitor charges when power is first applied, and the modem answers about **10 s** later.

**Bench setup for milestone 1 (XIAO on a computer for the console):**

- XIAO USB-C → computer (console and flashing).
- RockBLOCK pin 8 → a separate 5 V source rated ≥ 500 mA, for example the power bank through a USB breakout.
- RockBLOCK pin 10 → that source's ground **and** a XIAO GND pin (common ground).

Do not feed the power bank's 5 V into the XIAO's **5V pin** while the XIAO is plugged into the computer. That pin is the USB VBUS line with no diode (XIAO schematic v1.4), so it would back-feed the computer's USB port. A computer port (500 mA on USB 2.0) is also too weak to power the RockBLOCK through the XIAO.

**Target setup (power bank only):**

- Power bank → XIAO USB-C.
- Power bank → RockBLOCK pin 8 through a second port or a splitter, with common ground.

Taking the RockBLOCK's 5 V from the XIAO's 5V pin (VBUS pass-through) is possible, but it runs up to about 500 mA through the XIAO's USB-C connector and traces, and Seeed publishes no current rating for that path. A splitter avoids the question.

## Building

PlatformIO Core 6.x is required (`pip install platformio`). The first build downloads the pioarduino platform `55.03.311` (Arduino core 3.3.11 on ESP-IDF 5.5), the same release upstream Meshtastic uses.

```sh
# firmware
pio run

# host unit tests for the AT parser and the modem state machine (needs a host g++)
pio test -e native

# flash over USB-C, then open the console
pio run -t upload
pio device monitor
```

If the XIAO does not show up for flashing, hold **BOOT** while plugging in USB-C to start the ROM bootloader, then upload again.

## Using the console

Output starts immediately. Whatever is printed before the monitor opens is dropped; type `status` to see it again. About 12 s after boot the firmware sends the first test, then repeats it every 30 s until you type `auto off`.

| Command | Does |
|---|---|
| `test` | send exactly `AT\r` now and report the raw reply and PASS / FAIL |
| `auto N` / `auto off` | repeat the test every N seconds (5 to 3600), or stop repeating |
| `pass` | pass-through: typed bytes go to the RockBLOCK, Enter sends one CR, `~.` at the start of a line returns to the console |
| `status` | counters, last result, pins |
| `help` | command list |

A passing test looks like this (timings will differ; with the modem's echo on, the reply starts with your `AT`):

```
[     12.001] I AT test #1 (boot): sent "AT\r" on UART1 at 19200 8N1, waiting up to 3000 ms
[     12.020] I AT test #1 raw reply (9 bytes): "AT\r\r\nOK\r\n"
[     12.020] I AT test #1: PASS, OK received after 19 ms
```

On failure the log gives the likely causes in order: wiring, power, the 2-second power cycle, flow control. Any line ending you type (CR, LF or CR LF) reaches the modem as a single CR, because the 9603 accepts only CR. Commands such as `AT&K0` or `AT+CSQ` can be tried in pass-through.

### If the modem does not answer

1. Check RX/TX: RockBLOCK pin 1 → D7, pin 6 → D6. Ground Control's FAQ advises simply trying the other way round if in doubt.
2. Check that ground is shared between the RockBLOCK, the XIAO and the 5 V source.
3. Wait 10 s after powering the RockBLOCK.
4. Power-cycle the RockBLOCK with **at least 2 s off** (Iridium 9603 Developer's Guide 3.2.1: a unit reapplied too fast can hang until the next proper power cycle).
5. In pass-through, send `AT&K0`. Ground Control's pages disagree on whether flow control ships enabled, and in 3-wire mode it must be off.

## Hardware validation still to do

- [ ] `AT` → `OK` over GPIO43/44 with the wiring above (milestone 1 exit).
- [ ] Pass-through: `AT+CGMR` and `AT+CSQ` answered.
- [ ] RockBLOCK stays up during boot. The ESP32-S3 ROM prints its boot text on GPIO43 at every reset (it cannot be switched off on the XIAO); check that the first test after a XIAO reset still passes.
- [ ] Voltage on RockBLOCK pin 1 when idle is ≥ 3.0 V (ESP32-S3 input threshold 2.48 V).
- [ ] The chosen power bank keeps its output on at the node's idle current.
- [ ] Nothing changes on the SX1262 side (the firmware never touches its pins).

## Layout

```
boards/xiao-esp32s3-wio-sx1262/board_config.h   every GPIO number, with compile-time collision checks
src/core/         ByteStream interface, Arduino UART adapter, logging
src/iridium/      AT reply parser and the IridiumModem driver (no Arduino dependency)
src/diag/         milestone 1 bench tool: AT link test and pass-through
src/main.cpp      setup and the polling loop
tests/            host unit tests (pio test -e native)
docs/REFERENCES.md
```

Planned layers, added when their milestone comes: `src/radio/` (SX1262), `src/bluetooth/` (BLE GATT to Android), `src/wifi/`, `src/routing/`, `src/storage/`.

Design rules:

- Nothing blocks. Each module advances a state machine in `poll()` and returns.
- Protocol code talks to hardware only through `core::ByteStream`, so it runs in host tests.
- The Iridium driver knows nothing about BLE, Wi-Fi or routing.

## Direction

The node is built as standalone PlatformIO firmware first, with Meshtastic integration later. Upstream Meshtastic already supports this board (`seeed_xiao_s3`), but it claims GPIO43/44 for a GPS. The Iridium driver is written to drop into a Meshtastic module (`poll()` maps onto `OSThread::runOnce()`, same core version). Evidence and the steps a fork would take: [docs/REFERENCES.md](docs/REFERENCES.md#meshtastic-findings).

## Licence

GPL-3.0, the same as the MeshSat Bridge and Android app, and compatible with a future Meshtastic (GPL-3.0) integration. See [LICENSE](LICENSE).
