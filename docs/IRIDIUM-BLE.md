# Iridium Bluetooth service

The [meshsat-firmware](https://github.com/meshsat/meshsat-firmware) node exposes this service next to the Meshtastic service, on the same Bluetooth LE connection. It is a binary-safe serial line to the node's RockBLOCK 9603. The client speaks the modem's AT commands through it, exactly as it would over a cable.

Contract version 1.

| Characteristic | UUID | Properties | Carries |
|---|---|---|---|
| Service | `b3d305a2-7310-4877-ad12-8e245e71951a` | | |
| RX | `b9e2d4ba-f386-4728-b77a-7df7121db7a9` | write, write without response | raw bytes to the modem's UART |
| TX | `469354dc-4c89-41ed-b939-d707c7a11f49` | notify, read | raw bytes from the modem, in chunks of up to MTU minus 3 (at most 244) |
| STATUS | `69a4064d-78b9-46e5-a30a-1862e553245a` | read, notify | two bytes: contract version (`01`), then the modem's owner: `00` none, `01` phone, `02` node |

## Owning the modem

- Subscribing to TX notifications takes the modem when nobody owns it. STATUS then reads `01 01` and is notified on every change.
- Unsubscribing from TX, or disconnecting, gives it back (`01 00`).
- Bytes written to RX while the client does not own the modem are discarded, so nothing stale reaches the modem later. Wait for `01 01` before the first write.
- Modem output while nobody owns it is discarded.
- Owner `02` is reserved for routing on the node itself, which is not built yet.

## The serial line

- The pipe adds and removes nothing. The link to the RockBLOCK is 19200 8N1.
- AT commands end in a single CR. The 9603 does not accept LF as a terminator.
- The stored modem profile has echo on (`E1`). A client that sends `ATE0` keeps echo off until the modem next loses power.
- On node v1 the modem is powered at boot and answers about 10 s later. Retry the first command until it does.
- Every satellite session opened through the pipe (`AT+SBDIX` and friends) is billed by the airtime provider, even one with an empty outbound buffer.

## Security

If the node's Bluetooth pairing mode is anything other than "no PIN", all three characteristics need an encrypted, authenticated link: the same bond as the Meshtastic service. With "no PIN" they are open, like the Meshtastic service.

## Changes

Any change to this contract bumps the version byte in STATUS, and the MeshSat Android app is updated first.
