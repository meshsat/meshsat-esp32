#!/usr/bin/env python3
"""Drive the meshsat-esp32 BLE console (Nordic UART Service) from a computer.

Finds the node by its advertised name (meshsat-esp32-XXXX), subscribes to its
output, unlocks the console with the PIN the firmware was built with, then
sends each command followed by a carriage return and prints everything the
node sends back.

The PIN comes from the MESHSAT_BLE_CONSOLE_PIN environment variable or, if
that is unset, from ~/.config/meshsat-esp32/ble-pin.

Examples:
    tools/ble_console.py status
    tools/ble_console.py --listen 5 selftest line test
    tools/ble_console.py pass AT "AT+CGMR" "~."

Needs bleak (pip install bleak).
"""

import argparse
import asyncio
import os
import sys
from pathlib import Path

from bleak import BleakClient, BleakScanner

NAME_PREFIX = "meshsat-esp32"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
PIN_FILE = Path.home() / ".config" / "meshsat-esp32" / "ble-pin"

# Writes are split to fit the default ATT MTU (23 bytes minus a 3-byte header).
WRITE_CHUNK = 20


def load_pin():
    pin = os.environ.get("MESHSAT_BLE_CONSOLE_PIN", "").strip()
    if not pin and PIN_FILE.exists():
        pin = PIN_FILE.read_text().strip()
    if not pin:
        sys.exit(f"no PIN: set MESHSAT_BLE_CONSOLE_PIN or write it to {PIN_FILE}")
    return pin


def command_wait(command, default):
    if command.split(" ", 1)[0].lower() in ("test", "at"):
        return max(default, 4.0)
    return default


async def send_line(client, text):
    data = text.encode() + b"\r"
    for start in range(0, len(data), WRITE_CHUNK):
        await client.write_gatt_char(RX_UUID, data[start:start + WRITE_CHUNK], response=True)


async def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("commands", nargs="*", help="console lines to send, in order")
    parser.add_argument("--name", default=NAME_PREFIX, help="advertised name or prefix to look for")
    parser.add_argument("--scan", type=float, default=15.0, help="seconds to look for the node")
    parser.add_argument("--wait", type=float, default=2.0, help="seconds to listen after each command")
    parser.add_argument("--listen", type=float, default=2.0, help="seconds to listen after unlocking")
    args = parser.parse_args()
    pin = load_pin()

    device = await BleakScanner.find_device_by_filter(
        lambda d, _ad: bool(d.name) and d.name.startswith(args.name), timeout=args.scan)
    if device is None:
        sys.exit(f"no BLE device named {args.name}* found in {args.scan:.0f} s")
    print(f"--- connecting to {device.name} ({device.address})", flush=True)

    def on_output(_sender, data):
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()

    async with BleakClient(device) as client:
        await client.start_notify(TX_UUID, on_output)
        await send_line(client, f"unlock {pin}")
        await asyncio.sleep(args.listen)
        for command in args.commands:
            print(f"\n>>> {command!r}", flush=True)
            await send_line(client, command)
            await asyncio.sleep(command_wait(command, args.wait))
        await client.stop_notify(TX_UUID)
    print("\n--- disconnected", flush=True)


if __name__ == "__main__":
    asyncio.run(main())
