# References

The sources checked before writing hardware-specific code, the facts taken from each, and the GPIO assignments that follow from them. All pages were read on 19 September 2026.

Where two official sources disagree, both are quoted and the choice made is stated.

## Sources

### Ground Control (RockBLOCK 9603)

| Page | Page date | Used for |
|---|---|---|
| [Serial interface](https://docs.groundcontrol.com/iot/rockblock/electrical/serial-interface) | updated 26 Sep 2025 | 19200 8N1, logic levels, flow control, TXD/RXD direction |
| [Connectors and wiring](https://docs.groundcontrol.com/iot/rockblock/specification/connectors-wiring) | updated 9 Sep 2024 | 10-pin PicoBlade pinout |
| [Power supply and On/Off control](https://docs.groundcontrol.com/iot/rockblock/electrical/power-supply) | updated 26 Aug 2025 | supply range, current limit, supercapacitor start delay, OnOff levels |
| [AT commands](https://docs.groundcontrol.com/iot/rockblock/user-manual/at-commands) | read 19 Sep 2026 | CR terminator, 128-character command limit |
| [FAQ](https://docs.groundcontrol.com/iot/rockblock/support/faq) | read 19 Sep 2026 | flow control default, RX/TX swap warning |
| [Further reading](https://docs.groundcontrol.com/iot/rockblock/support/further-reading) | read 19 Sep 2026 | points to the Iridium developer's guide and the IridiumSBD Arduino library |
| [Iridium 9603/9603N SBD Transceiver Developer's Guide, Rev 3.1](https://cdn.rock7.com/docs/9603-Developers-guide.pdf) | 26 Aug 2014 | the module inside the RockBLOCK: power, logic levels, recovery, RF |

The Iridium *ISU AT Command Reference* (MAN0009 Rev 2.0), which Ground Control links from the AT commands page, returned a script page instead of the PDF from both Ground Control URLs. It has **not** been read. The AT behaviour this firmware relies on (CR terminator, verbose `OK`/`ERROR` result codes, command echo) is taken from the Ground Control pages above and from the Bridge's field-tested 9603 driver.

### Seeed Studio

| Document | Used for |
|---|---|
| [XIAO ESP32S3 getting started](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | D-pin to GPIO table, user LED, BOOT button, 3V3 and 5V pins, flash and PSRAM |
| XIAO ESP32S3 schematic v1.4 (`202003751_XIAO ESP32S3_v1.4_SCH_260226.pdf`, linked from the page above) | B2B connector J3 pinout, the D6/TX series resistor, 5V pin = VBUS, GPIO45/46 unconnected |
| [XIAO ESP32S3 & Wio-SX1262 kit](https://wiki.seeedstudio.com/wio_sx1262_with_xiao_esp32s3_kit/) | kit contents; the ESP32-S3 version connects through B2B |
| [Wio-SX1262 for XIAO schematic](https://files.seeedstudio.com/products/SenseCAP/Wio_SX1262/Schematic_Diagram_Wio-SX1262_for_XIAO.pdf) (KiCad sheet "Wio-SX1262 for XIAO V1.0") | the LoRa net on each B2B pin, user button, LED |

### Espressif

| Document | Used for |
|---|---|
| [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) | strapping pins (section 3), ROM message printing (section 3.3), pin reset states, VIH/VOH |
| [ESP-IDF for ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/) | general reference; UART routing through the GPIO matrix |

### Meshtastic firmware

[github.com/meshtastic/firmware](https://github.com/meshtastic/firmware), commit `2a398fb2fdff166a91769fa95e3aa773a007f521` (18 Sep 2026, version 2.8.1):

- `variants/esp32s3/seeed_xiao_s3/variant.h`, `platformio.ini`, `pins_arduino.h`
- `variants/esp32/esp32-common.ini` (platform) and `boards/seeed-xiao-s3.json`
- `src/gps/GPS.cpp` (GPS UART and pins), `src/modules/SerialModule.cpp` (serial module UART)
- `src/concurrency/OSThread.h`, `src/modules/Modules.cpp` (module structure)

### Existing 9603 drivers

- **MeshSat Bridge**: `meshsat` repo, `internal/transport/direct_sat.go` and `serial.go`, read at `acdb6e1` (last change to those files `3f5ea61`, 18 Sep 2026). This is the 9603 driver in field use on the tesseract kit, and the behavioural reference for this project.
- **IridiumSBD** (Mikal Hart), [github.com/mikalhart/IridiumSBD](https://github.com/mikalhart/IridiumSBD) at `609c31c` (29 Jul 2024). This is the Arduino library Ground Control's further-reading page points to. It is a reference only, not a dependency (see below).
- **rock7/RockBLOCK-9704** is Ground Control's only driver on GitHub. It speaks JSPR to the RockBLOCK 9704 and has no AT/SBD support, so it does not apply to the 9603.

## Checked GPIO assignments

### SX1262 on the Wio-SX1262 for XIAO ESP32-S3

The Wio board's 30-pin B2B connector (its J3) mates with the XIAO's J3 (DF40C-30DP-0.4V). Matching the two schematics pin by pin gives this table. Upstream Meshtastic's `seeed_xiao_s3` variant uses the same numbers.

| SX1262 signal | GPIO | Reaches the XIAO through | Meshtastic define |
|---|---|---|---|
| SCK | 7 | B2B, shared with header pin D8 | `LORA_SCK 7` |
| MISO | 8 | B2B, shared with header pin D9 | `LORA_MISO 8` |
| MOSI | 9 | B2B, shared with header pin D10 | `LORA_MOSI 9` |
| NSS | 41 | B2B (MTDI) | `LORA_CS 41` |
| RESET | 42 | B2B (MTMS), 10 k pull-up on the Wio | `LORA_RESET 42` |
| BUSY | 40 | B2B (MTDO) | `SX126X_BUSY 40` |
| DIO1 | 39 | B2B (MTCK) | `LORA_DIO1 39` |
| RF switch (RF_SW1) | 38 | B2B | `SX126X_RXEN 38`, DIO2 as RF switch, DIO3 TCXO 1.8 V |
| Wio user button | 21 | B2B; 10 k pull-up, pressed = low. The XIAO user LED is on the same GPIO. | `BUTTON_PIN 21` |
| Wio green LED | 48 | B2B; anode on GPIO48, lit when high | `LED_POWER 48` |

After reset, GPIO38 to GPIO42 are inputs with no pull (datasheet pin table). This firmware never configures them.

### RockBLOCK 9603 UART

| RockBLOCK pin | Name | Direction | XIAO pin | GPIO |
|---|---|---|---|---|
| 1 | RXD | output from the RockBLOCK | D7 (RX) | 44 |
| 6 | TXD | input to the RockBLOCK | D6 (TX) | 43 |
| 10 | GND | | GND | |
| 8 | 5V In | power | external 5 V, see README | |

Why GPIO43/44 work:

- They are the XIAO header pins labelled TX/RX (Seeed pin table). D6 has a 499 Ω series resistor on the XIAO (R6), which is harmless into the RockBLOCK's input.
- They are not on the XIAO's B2B connector, so the Wio-SX1262 cannot use them (XIAO J3 carries GPIO3, 7-18, 21, 38-42, 47, 48).
- They are not strapping pins (GPIO0, 3, 45, 46) and not the USB pins (19, 20).
- `board_config.h` checks all of this with `static_assert`.

The firmware drives them with **UART1**, routed through the GPIO matrix. See "UART0 boot output" below for why it is not UART0.

## Facts that shaped the code, and open questions

### Serial link

- 19200 baud, 8N1, no autobaud (Ground Control serial page; developer's guide section 3.3).
- A command ends with CR. "Using line feed (\n) will not work" (Ground Control AT page). The driver always appends exactly one `\r`. The console pass-through turns CR, LF or CR LF from the terminal into one `\r`.
- At most 128 characters per command string (Ground Control AT page), so `IridiumModem::kMaxCommandLength = 128`.
- The Bridge waits 3 s for an AT reply (`iridiumReadTimeout`) and drains stale bytes before each command. The firmware does both.

**Flow control: two Ground Control pages disagree.**

- The serial page says "RockBLOCK has flow control disabled during manufacture".
- The FAQ says "The default state for the RockBLOCK and RockBLOCK+ units has flow control turned ON in the modem. When running in 3-wire serial mode, flow control should be turned OFF, which will ensure you get responses to your requests. Use the command AT&K0."
- The Bridge sends `AT&K0` first on every connect, and so does IridiumSBD.

Milestone 1 sends exactly `AT\r`, as specified. If that gets no reply, the failure message suggests `AT&K0` in pass-through. Milestone 2's init sequence starts with `AT&K0`.

**Logic levels.**

The RockBLOCK's figures (Ground Control table, and developer's guide Table 8, which gives the same numbers):

| Direction | Standard figure | Ground Control "tested" figure |
|---|---|---|
| RockBLOCK input high (VIH) | ≥ 2.0 V | 3.0 V |
| RockBLOCK output high (VOH) | ≥ 2.4 V | ≥ 3.0 V |

The ESP32-S3's figures (datasheet): VIH = 0.75 × VDD ≈ 2.48 V, and VOH ≥ 0.8 × VDD = 2.64 V at 40 mA.

What that means:

- ESP32 to modem: into a high-impedance input the ESP32 output sits near 3.3 V, above both the 2.0 V and the 3.0 V figures.
- Modem to ESP32: the modem's 2.4 V minimum output is 80 mV below the ESP32's input threshold on paper. Ground Control's tested ≥ 3.0 V clears it.

This is not a blocker. If the link is flaky, measure pin 1's high level.

**TX/RX naming is easy to get wrong.** The connector page names pin 6 "TXD" and describes it as an input to the RockBLOCK. The serial page's level text talks about "the RX pin (input)". Its "Serial Wiring Essentials" section, though, agrees with the connector page ("TXD is an INPUT to the RockBLOCK"), and so does this firmware. Ground Control's FAQ: "make sure you haven't got your TX and RX lines mixed up ... If it doesn't work one way around, try the other way!"

### UART0 boot output reaches the modem on every reset

- ESP32-S3 datasheet section 3.3: during boot the ROM prints to UART0 and to USB Serial/JTAG by default. UART0 TX is GPIO43.
- Printing can be suppressed with the GPIO46 strap or an eFuse. On the XIAO, GPIO46 is not connected (schematic v1.4), and an eFuse change is permanent, so the boot text cannot be stopped.

So at every reset the RockBLOCK's TXD input receives the ROM boot text at 115200 baud. At 19200 baud it reads as a short burst of garbage.

The firmware limits this to boot time:

- The modem runs on UART1. Once `begin()` routes UART1 to GPIO43, ESP-IDF log output (which goes to UART0) no longer reaches the pin.
- The first test comes 12 s later, and the driver drains stale input before each command.

If the garbage turns out to upset the modem on hardware, moving the modem to D0/D1 (GPIO1/GPIO2) is a two-line change in `board_config.h`. Those pins are free with this kit. GPIO3 (D2) is a strapping pin and should be avoided.

### Power

- The RockBLOCK 9603 input needs 3.0 to 5.4 V on pin 8 (Ground Control table).
- **Current figures differ between Ground Control pages:**
  - connector page: "450mA limit" on pins 8 and 9
  - power page table: 470 mA maximum
  - power page, Rev F note: "fixed current limit of ~500mA ... The minimum supply capability must be 500mA. A lower current source will cause brownouts during transmission."

  Plan for at least 500 mA available to the RockBLOCK alone.
- When power is first applied, the supercapacitor charges and the Iridium module starts "approximately 10 seconds" later (power page). The first automatic AT test runs 12 s after boot.
- The developer's guide Table 9 describes the bare module inside the RockBLOCK:
  - supply 5.0 V ±0.2 V (±0.5 V for the 9603N)
  - transmit peaks of 1.5 A (9603) / 1.3 A (9603N) for 8.3 ms
  - inrush limited to 4 A, and a noise profile

  These apply at the module connector. The RockBLOCK's regulator and supercapacitor sit between our supply and the module, which is why Ground Control asks only for the lighter figures above.
- Average draw from the same table, for sizing the power bank later:

  | | Idle | During an SBD transfer |
  |---|---|---|
  | 9603 | 45 mA | 190 mA |
  | 9603N | 34 mA | 158 mA |
- Developer's guide section 3.2.1: "if a unit does not respond to AT commands, power off the module, wait for 2 seconds and then power it back on". Power must not be reapplied until 2 s after it reached 0 V, or the modem can be left non-operational until the next correct power cycle. Both points are in the failure message and the README.
- **OnOff (pin 7), Rev F:**
  - OFF is at or below supply − 1.5 V. ON is at or above supply − 0.5 V, *or floating*.
  - More than 1 µA of leakage can power the module down.
  - A 3.3 V MCU must be 5 V tolerant or use level shifting (Ground Control power page).

  The pin stays unconnected in milestone 1. The Bridge learned the same rule on tesseract: a 3.3 V GPIO cannot hold OnOff high, and needs an N-MOSFET open-drain stage.
- **The XIAO's 5V pin is VBUS**, wired straight from the USB-C connector with no diode or fuse (schematic v1.4, U9 pin 14). An external 5 V source on that pin back-feeds whatever is plugged into the USB-C port. Seeed publishes no current rating for passing modem current through it.
- The XIAO's 3V3 regulator is rated 700 mA on the wiki and "Imax=600mA" in the schematic note. It is not used for the RockBLOCK in any case.

### Things the 9603 developer's guide sets for later milestones

- Largest message: 340 bytes mobile-originated (sent), 270 bytes mobile-terminated (received) (section 1.1).
- Send `AT*F` (flush memory) before switching the modem off (section 3.2.1).
- Network Available, pin 4 (section 3.4):
  - refreshes about every 4 s while the ring channel is visible
  - when no satellite is visible, the refresh interval grows up to 120 s
  - turns back on 4 to 12 s after an SBD session
  - does not guarantee a send will succeed
- AT support differs between 9603 firmware releases (section 5). Read `AT+CGMR`. The Bridge enables its `AT-MSSTM` workaround for firmware TA16005.
- **Co-location** (section 1.6, FCC/IC RF exposure):
  - keep at least 20 cm between the antenna and people
  - antenna gain ≤ 3 dBi
  - "This transmitter must not be co-located or operating in conjunction with any other antenna or transmitter."

  This node puts a 1.5 W, 1616 to 1626.5 MHz transmitter next to an 868 MHz LoRa radio and 2.4 GHz Wi-Fi/BLE. The scheduler should therefore never let LoRa or Wi-Fi transmit during an `SBDIX` session, and the antenna layout needs planned separation.
- Iridium antenna: RHCP, VSWR ≤ 1.5:1 in band, total implementation loss ≤ 3 dB (section 4).

## Existing 9603 drivers: what we take from each

**Bridge (`direct_sat.go`), the behavioural reference.**

- Connect sequence: drain, `AT&K0`, `ATE0`, `AT&D0`, `AT`, `AT+CGSN`, `AT+CGMM`, `AT+CGMR` (enables the TA16005 `AT-MSSTM` workaround), `AT+SBDMTA=1`, `AT+SBDD0`, `AT+SBDD1`.
- Timing: 3 s AT timeout, 90 s `SBDIX` timeout, at least 10 s between `SBDIX` attempts.
- The Bridge rules also require a 3-minute backoff after `SBDIX` status 32 or 36, and a light `SBDSX` check before `SBDIX`.
- After `SBDWB`, the Bridge drains the port and sends an `AT` probe.
- Port the behaviour, not the code. The Go version blocks on reads with deadlines inside goroutines; this firmware needs a state machine polled from one loop.

**IridiumSBD (Arduino), reference only.**

- Every call blocks until done:
  - `begin()` retries `AT` for up to 240 s
  - the default AT timeout is 30 s
  - a send-receive can wait up to 300 s
- Other work runs only inside the `ISBDCallback()` hook.
- That is the architecture this project rules out, so the library is not used as a dependency.
- It confirms the same init commands (`ATE1`, `AT&D0`, `AT&K0`, `AT+SBDMTA`) and the `READY` / `0\r\n\r\nOK\r\n` reply shape of `AT+SBDWB`.
- The upstream repository has no licence file GitHub recognises. SparkFun's fork is LGPL-2.1.

## Meshtastic: findings

- **An upstream board definition exists**: `variants/esp32s3/seeed_xiao_s3`
  - hardware model 81 `SEEED_XIAO_S3`, marked actively supported
  - partition layout `default_8MB.csv`
  - its SX1262 pins match the Seeed schematics exactly (table above)
- **GPIO43/44 are taken by GPS in that variant.** `variant.h` defines `GPS_L76K` with `GPS_TX_PIN 43`, `GPS_RX_PIN 44`, `HAS_GPS 1` and `PIN_GPS_STANDBY 1` (GPIO1). `GPS.cpp` opens GPS on `Serial1` (UART1) with those pins unless `config.position.rx_gpio/tx_gpio` say otherwise.
  - Consequence: **stock Meshtastic flashed onto this hardware with the RockBLOCK wired would open the modem's UART as a GPS** and write GPS probe strings into it.
  - A fork needs its own variant with the GPS defines removed. The modem then takes over the GPS's pins and UART1, which is the UART this firmware already uses.
- **The serial module uses UART2**: `SerialModule.cpp` calls `Serial2.begin(baud, SERIAL_8N1, rxd, txd)` on ESP32 when pins are configured. UART0 is the console, so on the ESP32-S3 all three UARTs have an owner in Meshtastic; UART1 is free once GPS is removed.
- **No Iridium or RockBLOCK code exists upstream** (searched all of `src/`).
- **Build platform**: pioarduino `55.03.311` (Arduino core 3.3.11, ESP-IDF 5.5), C++17. This project uses the same platform release and compiles its own sources as C++17.
- **Module structure**: a feature is a `MeshModule`/`SinglePortModule` that is also an `OSThread`, whose `runOnce()` returns the delay until its next run. Modules are registered in `src/modules/Modules.cpp`. `IridiumModem::poll()` fits `runOnce()` directly.
- **Licence**: GPL-3.0. A fork, and anything linked into it, is GPL-3.0. This repo is GPL-3.0 (the same as the Bridge and Android) so that path stays open.

### What adding an Iridium module to Meshtastic would take

1. A variant `meshsat_xiao_s3` copied from `seeed_xiao_s3`, with `GPS_L76K`/`HAS_GPS` removed and the modem UART pins defined.
2. `src/modules/IridiumModule.{h,cpp}`, a `SinglePortModule` plus `OSThread` that wraps `IridiumModem`, registered in `Modules.cpp` behind a build flag.
3. A policy for which packets go over SBD (port filter; 340 bytes out / 270 bytes in; compression), and a port number. `PRIVATE_APP` (256) and up are free for private apps.
4. Settings. New `ModuleConfig` fields live in the separate `meshtastic/protobufs` repository, so the fork would carry a protobuf fork too, or use build-time settings.
5. Coordination with the radio: no LoRa transmit during `SBDIX` (co-location, above).

### Recommendation: B, standalone first, then integrate

- Milestones 1 and 2 concern the 9603 driver. Meshtastic contributes nothing to it, and its GPS default on GPIO43/44 would have to be removed before a first `AT` could even be sent safely.
- The driver is built to move later:
  - `src/iridium/` has no Arduino dependency and is covered by host tests
  - it is driven by `poll()`, the shape of `OSThread::runOnce()`
  - it builds on the same core as Meshtastic
- The decision to fork belongs to the milestone that brings up LoRa. Being compatible with Meshtastic means its packet format, encryption, routing and the phone API that the Android app would reuse over BLE. A fork provides all of that; reimplementing it would not be cheaper. That milestone is where to choose between a Meshtastic fork carrying this driver as a module, and a standalone node.
