#!/usr/bin/env python3
"""Bench client for a RockBLOCK 9704 behind the MeshSat node's Iridium BLE pipe (MESHSAT-1507).

Takes the modem by subscribing to TX, brings the 9704 up (API version, SIM, active), then
originates one message on a topic and follows it to its final status, only watches events, or
cancels messages the modem still holds, by message_id.

  jspr_mo.py ADDRESS send "text" [--topic 244] [--timeout 600] [--ref 1]
  jspr_mo.py ADDRESS watch [--timeout 120]
  jspr_mo.py ADDRESS cancel ID [ID ...] [--topic 244] [--timeout 30]

A cancel is Ground Control's PUT messageOriginateStatus {"action": "cancel"} (rbCancelMessage in
their RockBLOCK-9704 library), as the Bridge sends it (cmd/jspr-helper, MESHSAT-1282). The modem
then reports one final status for that id: cancelled, or mo_ack_received when it already went.

JSPR lines are "METHOD target {json}" + CR out, "CODE target {json}" + CR back; 299 lines are
events. The 9704's parser wants a space after every colon and comma, or it answers 407 BAD_JSON. The payload carries a CRC-16/CCITT (init 0, big-endian) at its end, as the Bridge and
Android send it. The link must already be bonded (pair_node.py). Prints everything with a stamp.
"""
import asyncio
import base64
import datetime
import json
import sys

from bleak import BleakClient

RX = "b9e2d4ba-f386-4728-b77a-7df7121db7a9"
TX = "469354dc-4c89-41ed-b939-d707c7a11f49"
STATUS = "69a4064d-78b9-46e5-a30a-1862e553245a"
RAW_TOPIC = 244


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def say(text):
    print(f"{stamp()} {text}", flush=True)


def crc16_ccitt(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class Pipe:
    def __init__(self, client):
        self.client = client
        self.buf = bytearray()
        self.lines = asyncio.Queue()
        self.owner = None
        self.owned = asyncio.Event()

    def on_tx(self, _, data):
        self.buf.extend(data)
        while b"\r" in self.buf:
            line, _, rest = self.buf.partition(b"\r")
            self.buf = bytearray(rest)
            text = line.decode("ascii", "replace").strip("\n")
            if text:
                say(f"  < {text[:200]}")
                self.lines.put_nowait(text)

    def on_status(self, _, data):
        self.owner = data[1] if len(data) > 1 else None
        say(f"  STATUS {data.hex()}")
        if self.owner == 1:
            self.owned.set()

    async def write(self, line, response=True):
        say(f"  > {line[:200]}")
        await self.client.write_gatt_char(RX, line.encode() + b"\r", response=response)

    async def request(self, method, target, payload, timeout=10, response=True):
        await self.write(f"{method} {target} {json.dumps(payload, separators=(', ', ': '))}", response)
        end = asyncio.get_event_loop().time() + timeout
        while True:
            left = end - asyncio.get_event_loop().time()
            if left <= 0:
                say(f"  no answer to {method} {target} in {timeout} s")
                return None, None
            try:
                text = await asyncio.wait_for(self.lines.get(), left)
            except asyncio.TimeoutError:
                continue
            code, tgt, body = parse(text)
            if tgt == target and code != 299:
                return code, body

    async def event(self, timeout):
        try:
            text = await asyncio.wait_for(self.lines.get(), timeout)
        except asyncio.TimeoutError:
            return None, None, None
        return parse(text)


def parse(text):
    parts = text.split(" ", 2)
    try:
        code = int(parts[0])
    except (ValueError, IndexError):
        return None, None, None
    tgt = parts[1] if len(parts) > 1 else ""
    body = {}
    if len(parts) > 2:
        try:
            body = json.loads(parts[2])
        except ValueError:
            body = {}
    return code, tgt, body


async def bring_up(p):
    await p.write("")
    await asyncio.sleep(0.5)
    while not p.lines.empty():
        p.lines.get_nowait()
    code, body = await p.request("GET", "apiVersion", {})
    if code != 200:
        say("modem did not answer GET apiVersion")
        return False
    if not body.get("active_version"):
        first = body["supported_versions"][0]
        await p.request("PUT", "apiVersion", {"active_version": first})
    code, body = await p.request("GET", "simConfig", {})
    if body.get("interface") != "internal":
        await p.request("PUT", "simConfig", {"interface": "internal"})
    code, body = await p.request("GET", "operationalState", {})
    if body.get("state") != "active":
        await p.request("PUT", "operationalState", {"state": "active"})
    code, body = await p.request("GET", "constellationState", {})
    say(f"constellation visible={body.get('constellation_visible')} bars={body.get('signal_bars')}")
    return True


REF = 1


async def send(p, text, topic, timeout):
    data = text.encode()
    crc = crc16_ccitt(data)
    payload = data + bytes([crc >> 8, crc & 0xFF])
    # The modem answered 407 to references of 206 and larger on the bench, and 200 to 1.
    ref = REF
    code, body = await p.request("PUT", "messageOriginate",
                                 {"topic_id": topic, "message_length": len(payload), "request_reference": ref})
    if code != 200 or not body.get("message_id"):
        say(f"originate refused: code={code} body={body}")
        return
    msg_id = body["message_id"]
    say(f"originate accepted: message_id={msg_id} response={body.get('message_response')}")
    end = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < end:
        code, tgt, ev = await p.event(min(30, end - asyncio.get_event_loop().time()))
        if code is None:
            continue
        if tgt == "messageOriginateSegment" and ev.get("message_id") == msg_id:
            start = ev["segment_start"]
            length = ev["segment_length"]
            chunk = payload[start:start + length]
            await p.request("PUT", "messageOriginateSegment",
                            {"topic_id": topic, "message_id": msg_id, "segment_length": len(chunk),
                             "segment_start": start, "data": base64.b64encode(chunk).decode()},
                            response=False)
        elif tgt == "messageOriginateStatus" and ev.get("message_id") == msg_id:
            say(f"FINAL: message {msg_id} {ev.get('final_mo_status')}")
            return
        elif tgt == "constellationState":
            say(f"constellation visible={ev.get('constellation_visible')} bars={ev.get('signal_bars')}")
    say(f"no final status for message {msg_id} within {timeout} s (it stays queued in the modem)")


async def cancel(p, ids, topic, timeout):
    for msg_id in ids:
        code, body = await p.request("PUT", "messageOriginateStatus",
                                     {"topic_id": topic, "message_id": msg_id, "action": "cancel"})
        say(f"cancel of message {msg_id}: code={code} body={body}")
    left = set(ids)
    end = asyncio.get_event_loop().time() + timeout
    while left and asyncio.get_event_loop().time() < end:
        code, tgt, ev = await p.event(min(30, end - asyncio.get_event_loop().time()))
        if tgt == "messageOriginateStatus":
            say(f"FINAL: message {ev.get('message_id')} {ev.get('final_mo_status')}")
            left.discard(ev.get("message_id"))
    if left:
        say(f"no final status within {timeout} s for: {sorted(left)}")


async def watch(p, timeout):
    end = asyncio.get_event_loop().time() + timeout
    while asyncio.get_event_loop().time() < end:
        await p.event(min(30, end - asyncio.get_event_loop().time()))


async def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    address, mode = sys.argv[1], sys.argv[2]
    args = sys.argv[3:]
    text = args.pop(0) if mode == "send" and args and not args[0].startswith("--") else ""
    ids = []
    while mode == "cancel" and args and not args[0].startswith("--"):
        ids.append(int(args.pop(0)))
    topic, timeout = RAW_TOPIC, {"send": 600, "cancel": 30}.get(mode, 120)
    while args:
        flag = args.pop(0)
        if flag == "--topic":
            topic = int(args.pop(0))
        elif flag == "--timeout":
            timeout = int(args.pop(0))
        elif flag == "--ref":
            global REF
            REF = int(args.pop(0))
    say(f"connecting to {address}")
    async with BleakClient(address, timeout=30) as client:
        say("connected")
        await asyncio.sleep(3)
        p = Pipe(client)
        await client.start_notify(STATUS, p.on_status)
        await client.start_notify(TX, p.on_tx)
        try:
            await asyncio.wait_for(p.owned.wait(), 100)
        except asyncio.TimeoutError:
            say("the node did not hand over the modem in 100 s")
            return 1
        say("client owns the modem")
        try:
            if await bring_up(p):
                if mode == "send":
                    await send(p, text, topic, timeout)
                elif mode == "cancel":
                    await cancel(p, ids, topic, timeout)
                else:
                    await watch(p, timeout)
        finally:
            say("releasing the modem")
            await client.stop_notify(TX)
            await asyncio.sleep(2)
            await client.stop_notify(STATUS)
    say("disconnected")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
