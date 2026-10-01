#!/usr/bin/env python3
"""Bench client for the MeshSat node's Iridium BLE pipe (MESHSAT-1437).

Takes the modem by subscribing to TX, runs the steps in order, prints everything with a
wall-clock stamp. One run, no retries, nothing left behind.

  pipe_client.py ADDRESS STEP [STEP ...]

  at:<command>   send the command with a CR, print the answer (ends at OK/ERROR/READY or 5 s)
  mo:<text>      AT+SBDWB=<len>, then the text and its checksum
  wait:<s>       hold the modem and do nothing
  stats          read the node's STATS characteristic and print its counters
  release        unsubscribe from TX (the documented way to give the modem back)
  drop           disconnect at once, without unsubscribing

The link must already be bonded (pair_node.py). Never opens a satellite session by itself.
"""
import asyncio
import datetime
import sys

from bleak import BleakClient

RX = "b9e2d4ba-f386-4728-b77a-7df7121db7a9"
TX = "469354dc-4c89-41ed-b939-d707c7a11f49"
STATUS = "69a4064d-78b9-46e5-a30a-1862e553245a"
STATS = "9c22cf07-2256-4fc2-b6ee-ab0ceb12198d"
OWNERS = {0: "none", 1: "client", 2: "node"}


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def say(text):
    print(f"{stamp()} {text}", flush=True)


def show(data):
    return "".join(chr(b) if 32 <= b < 127 else f"<{b:02x}>" for b in data)


async def main():
    address = sys.argv[1]
    steps = sys.argv[2:]
    answer = bytearray()
    owner = {"value": None}
    owned = asyncio.Event()

    def on_tx(_, data):
        answer.extend(data)
        say(f"  modem: {show(data)}")

    def on_status(_, data):
        value = data[1] if len(data) > 1 else None
        owner["value"] = value
        say(f"  STATUS {data.hex()} owner={OWNERS.get(value, value)}")
        if value == 1:
            owned.set()

    async def until(markers, seconds):
        deadline = asyncio.get_event_loop().time() + seconds
        while asyncio.get_event_loop().time() < deadline:
            if any(m in answer for m in markers):
                await asyncio.sleep(0.15)
                return True
            await asyncio.sleep(0.05)
        return False

    say(f"connecting to {address}")
    async with BleakClient(address, timeout=30) as client:
        say("connected")
        # The node may indicate Service Changed on a new link; BlueZ then discovers again and
        # drops a subscription made in that moment.
        await asyncio.sleep(3)
        await client.start_notify(STATUS, on_status)
        await client.start_notify(TX, on_tx)
        say("subscribed to TX, waiting for the modem (the node may hold it for up to 100 s)")
        try:
            await asyncio.wait_for(owned.wait(), 4)
        except asyncio.TimeoutError:
            # A bonded link gets its subscriptions back when it reconnects, and the node's
            # notification then goes out before this client listens: read the value instead.
            try:
                on_status(None, await asyncio.wait_for(client.read_gatt_char(STATUS), 6))
            except asyncio.TimeoutError:
                say("  STATUS read gave no answer in 6 s")
        try:
            await asyncio.wait_for(owned.wait(), 100)
        except asyncio.TimeoutError:
            say("the modem was not handed over, stopping")
            await client.stop_notify(TX)
            return 1
        say("client owns the modem")

        for step in steps:
            kind, _, arg = step.partition(":")
            answer.clear()
            if kind == "at":
                say(f"> {arg}")
                await client.write_gatt_char(RX, arg.encode() + b"\r", response=True)
                if not await until((b"OK\r", b"ERROR\r", b"READY\r"), 5):
                    say("  (no final answer in 5 s)")
            elif kind == "mo":
                payload = arg.encode()
                say(f"> AT+SBDWB={len(payload)}")
                await client.write_gatt_char(RX, f"AT+SBDWB={len(payload)}\r".encode(), response=True)
                if not await until((b"READY\r",), 5):
                    say("  no READY, payload not sent")
                    continue
                answer.clear()
                total = sum(payload) & 0xFFFF
                say(f"> {len(payload)} bytes and checksum {total:04x}")
                await client.write_gatt_char(RX, payload + bytes([total >> 8, total & 0xFF]), response=True)
                if not await until((b"OK\r", b"ERROR\r"), 5):
                    say("  (no final answer in 5 s)")
            elif kind == "stats":
                try:
                    raw = await asyncio.wait_for(client.read_gatt_char(STATS), 6)
                except asyncio.TimeoutError:
                    say("  STATS read gave no answer in 6 s")
                    continue
                if len(raw) < 52:
                    say(f"  STATS of {len(raw)} bytes: {bytes(raw).hex()}")
                    continue
                u32 = lambda at: int.from_bytes(raw[at:at + 4], "little")
                say(f"  STATS owner={OWNERS.get(raw[1], raw[1])} flags={raw[2]:02x} sessions since boot={u32(8)} "
                    f"uptime={u32(24)} s node sessions={u32(36)} node sent={u32(40)} node received={u32(44)} "
                    f"today {raw[48]}/{raw[49]}")
            elif kind == "wait":
                say(f"holding the modem for {arg} s")
                await asyncio.sleep(float(arg))
            elif kind == "release":
                say("unsubscribing from TX")
                await client.stop_notify(TX)
                await asyncio.sleep(4)
                await client.stop_notify(STATUS)
            elif kind == "drop":
                say("dropping the link without unsubscribing")
                break
            else:
                say(f"unknown step {step}")
    say("disconnected")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(asyncio.run(main()))
