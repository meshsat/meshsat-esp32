# Iridium Bluetooth service

The [meshsat-firmware](https://github.com/meshsat/meshsat-firmware) node exposes this service next to the Meshtastic service, on the same Bluetooth LE connection. It is a binary-safe serial line to the node's RockBLOCK 9603. The client speaks the modem's AT commands through it, exactly as it would over a cable.

Contract version 2, since firmware 2.8.0 of 27 September 2026. A version 1 client still works: the first two bytes of STATUS mean what they always did.

| Characteristic | UUID | Properties | Carries |
|---|---|---|---|
| Service | `b3d305a2-7310-4877-ad12-8e245e71951a` | | |
| RX | `b9e2d4ba-f386-4728-b77a-7df7121db7a9` | write, write without response | raw bytes to the modem's UART |
| TX | `469354dc-4c89-41ed-b939-d707c7a11f49` | notify, read | raw bytes from the modem, in chunks of up to MTU minus 3 (at most 244) |
| STATUS | `69a4064d-78b9-46e5-a30a-1862e553245a` | read, notify | four bytes: contract version (`02`), the modem's owner (`00` none, `01` phone, `02` node), the flags byte and the signal byte described under STATS. Notified when the owner or the flags change |

## Owning the modem

- Subscribing to TX notifications takes the modem when nobody owns it. STATUS then reads `02 01 ...` and is notified.
- Unsubscribing from TX, or disconnecting, gives it back (`02 00 ...`).
- Bytes written to RX while the client does not own the modem are discarded, so nothing stale reaches the modem later. Wait for owner `01` before the first write.
- Modem output while nobody owns it is discarded.
- Owner `02` is the node itself. When no client is subscribed to TX, the node's own routing takes the modem for the mesh channel it carries over Iridium. It hands the modem to a client within about a second of the TX subscription, between its own AT commands, and only after the result when one of its satellite sessions is in flight, which can take up to 90 s. A client that reads owner `02` should wait, not fail.

## The serial line

- The pipe adds and removes nothing. The link to the RockBLOCK is 19200 8N1.
- AT commands end in a single CR. The 9603 does not accept LF as a terminator.
- The stored modem profile has echo on (`E1`). A client that sends `ATE0` keeps echo off until the modem next loses power.
- On node v1 the modem is powered at boot and answers about 10 s later. Retry the first command until it does.
- Every satellite session opened through the pipe (`AT+SBDIX` and friends) is billed by the airtime provider, even one with an empty outbound buffer.

## Version 2 additions

Two more characteristics on the same service. A client that ignores them keeps working.

| Characteristic | UUID | Properties | Carries |
|---|---|---|---|
| STATS | `9c22cf07-2256-4fc2-b6ee-ab0ceb12198d` | read, notify | 52 bytes of node health, little-endian, notified on change and at most every 2 s |
| PASS | `5c1000e8-f411-4f3d-a4c9-5ee0610a8e66` | write | pass windows from the client, up to eight |

STATS, byte by byte:

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | version, `02` |
| 1 | u8 | owner, as in STATUS |
| 2 | u8 | flags: bit 0 a session is in flight, bit 1 a message waits at the gateway, bit 2 the modem answers, bit 3 the incoming buffer is nearly full |
| 3 | u8 | signal 0 to 5 as the modem last reported it, `FF` when never read |
| 4 | u32 | age of that signal reading in seconds, `FFFFFFFF` when never read |
| 8 | u32 | satellite sessions since boot, by any owner |
| 12 | i16 | MO status of the last session, -1 before the first |
| 14 | u16 | MOMSN of the last session |
| 16 | i16 | MT status of the last session, -1 before the first |
| 18 | u16 | messages still queued at the gateway after the last session |
| 20 | u32 | age of the last session in seconds, `FFFFFFFF` before the first |
| 24 | u32 | uptime in seconds |
| 28 | u32 | reboots by the node's Bluetooth watchdog, lifetime |
| 32 | u32 | bytes a client wrote faster than the modem took them, since boot |
| 36 | u32 | sessions opened by the node's own routing since boot |
| 40 | u32 | messages the node's own routing sent |
| 44 | u32 | messages the node's own routing received |
| 48 | u8 | the node's own sessions today |
| 49 | u8 | the node's daily cap |
| 50 | u8[2] | reserved, zero |

The signal byte is information for a screen. It is never a reason to hold a send: this modem has sent and received at 0.

PASS, written with response: `01`, then the number of windows (at most 8), then for each window a u32 start as Unix seconds, a u16 duration in seconds and a u8 peak elevation in degrees, all little-endian. A write replaces the node's list. The node's own routing then opens routine sessions only inside a window; a ring alert or a message queued at the gateway still goes at once, and a node that never received a list is not held back. Writes are accepted from any client on the service, whoever owns the modem.

STATUS carries the flags byte and the signal byte after the owner since version 2. Clients accept a STATUS of 2 or 4 bytes and read only what is there.

## Security

If the node's Bluetooth pairing mode is anything other than "no PIN", all five characteristics need an encrypted, authenticated link: the same bond as the Meshtastic service. With "no PIN" they are open, like the Meshtastic service.

## Changes

Any change to this contract bumps the version byte in STATUS, and the MeshSat Android app is updated first, with the iOS app following it. Additions that a version 1 client can ignore, like STATS and PASS, ship before the bump.
