#!/usr/bin/env python3
"""Bond the laptop with the MeshSat node through bluetoothctl, once.

  pair_node.py ADDRESS PIN_FILE

Reads the node's fixed PIN from PIN_FILE (mode 600) and never prints it. Does not trust the
device, so BlueZ does not reconnect on its own, and disconnects when done.
"""
import sys

import pexpect


def main():
    address, pin_file = sys.argv[1], sys.argv[2]
    pin = open(pin_file).read().strip()
    bt = pexpect.spawn("bluetoothctl", encoding="utf-8", timeout=30)
    bt.expect("#")
    for line in ("agent off", "agent KeyboardOnly", "default-agent"):
        bt.sendline(line)
        bt.expect("#")
    bt.sendline("scan on")
    found = bt.expect([address, pexpect.TIMEOUT], timeout=25)
    if found != 0:
        print("node not seen in 25 s: is another central connected to it?")
        bt.sendline("scan off")
        bt.sendline("quit")
        return 1
    bt.sendline("scan off")
    bt.sendline(f"pair {address}")
    step = bt.expect(["[Pp]asskey", "Pairing successful", "AlreadyExists", "Failed to pair", pexpect.TIMEOUT], timeout=30)
    if step == 0:
        bt.sendline(pin)
        step = bt.expect(["Pairing successful", "Failed to pair", pexpect.TIMEOUT], timeout=30)
        ok = step == 0
    else:
        ok = step in (1, 2)
    print("paired" if ok else "pairing failed")
    bt.sendline(f"disconnect {address}")
    bt.expect(["Successful disconnected", "not available", "#", pexpect.TIMEOUT], timeout=10)
    bt.sendline("quit")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main())
