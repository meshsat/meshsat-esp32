#!/usr/bin/env python3
"""Timestamped log of the MeshSat node over USB, with texts sent on a channel at set moments.

  node_log.py PORT SECONDS LOGFILE [AT_SECOND:CHANNEL_NAME:TEXT ...]

One process holds the serial port: it follows the node's log (the Meshtastic client API carries
it) and sends each text through the same API when its moment comes. The channel is given by
name and looked up on the node, so a text never lands on another channel. Runs for SECONDS,
then exits. Run it with the Meshtastic venv's python.
"""
import datetime
import sys
import time

import meshtastic.serial_interface
from pubsub import pub


def stamp():
    return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]


def channel_index(iface, name):
    for channel in iface.localNode.channels or []:
        if channel.role != 0 and channel.settings.name.lower() == name.lower():
            return channel.index
    return None


def main():
    port, seconds, path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    sends = []
    for item in sys.argv[4:]:
        at, channel, text = item.split(":", 2)
        sends.append((float(at), channel, text))
    sends.sort()

    out = open(path, "a", buffering=1)

    def write(line):
        out.write(f"{stamp()} {line}\n")

    def on_log(line, interface=None):
        write(line.rstrip())

    pub.subscribe(on_log, "meshtastic.log.line")
    start = time.monotonic()
    write(f"--- node_log: opening {port} for {seconds:.0f} s")
    iface = meshtastic.serial_interface.SerialInterface(port)
    version = getattr(iface.metadata, "firmware_version", "") if iface.metadata else ""
    names = [c.settings.name for c in (iface.localNode.channels or []) if c.role != 0]
    write(f"--- node_log: connected, firmware {version or 'unknown'}, {len(names)} channel(s)")
    try:
        while time.monotonic() - start < seconds:
            now = time.monotonic() - start
            while sends and sends[0][0] <= now:
                _, channel, text = sends.pop(0)
                index = channel_index(iface, channel)
                if index is None:
                    write(f"--- node_log: no channel named {channel} on the node, nothing sent")
                    continue
                write(f"--- node_log: sending on channel {index} ({channel}): {text}")
                iface.sendText(text, channelIndex=index)
            time.sleep(0.2)
    finally:
        write("--- node_log: closing")
        iface.close()
        out.close()


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(2)
    main()
