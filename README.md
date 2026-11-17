# SoloDolo

## What it is

A portable traffic display. **Dolo** runs on a Raspberry Pi with an RTL-SDR and a GNSS module. It decodes ADS-B
aircraft broadcasts on 1090 MHz, reads the receiver's own position, and streams both to **Solo**, an iPad app.
Solo shows a live dashboard: a traffic map, GPS health, and the five closest aircraft.

```
1090 MHz antenna → RTL-SDR → dump1090 ─┐
                                       ├─▶ Dolo (Pi) ══ TCP :5555, protobuf ══▶ Solo (iPad)
GNSS antenna → GNSS module (UBX/NMEA) ─┘        └─ :5556 JSON copy for debugging
```

For the design, wire protocol and state machines, see [`docs/architecture.md`](docs/architecture.md).
For install, run and configuration instructions, see [`dolo/README.md`](dolo/README.md) (Pi; internals in
[`dolo/ARCHITECTURE.md`](dolo/ARCHITECTURE.md)) and [`solo/README.md`](solo/README.md) (iPad).

## Repository layout

| Path | Contents |
|---|---|
| `dolo/` | Pi bridge (C++20). Build files, sources, tests, `deploy/` scripts |
| `solo/` | iPad app sources (Xcode target `SoloDolo`) |
| `proto/` | `solodolo.proto`, the wire contract shared by both halves |
| `scripts/` | `generate-proto.sh` regenerates Solo's Swift protobuf code |
| `docs/` | Architecture document |
| `SoloDolo.xcodeproj` | Xcode project for Solo |

## External interfaces

### Wire protocol (Dolo ↔ Solo)

The only interface between the two halves of the system, defined by [`proto/solodolo/v1/solodolo.proto`](proto/solodolo/v1/solodolo.proto):

| Port | Protocol | Purpose |
|---|---|---|
| **5555** | Protobuf, length-prefixed `Envelope` frames | Handshake (`ClientHello`/`ServerHello`), `GnssFix`, `TrafficSnapshot`, `Ping`/`Pong` keepalive |
| **5556** | NDJSON (same `Envelope`s, JSON-mapped), read-only | Debugging mirror, e.g. `nc <pi-address> 5556`; disable with `server.debug_port: null` |

Dolo also advertises itself over mDNS as `_solodolo._tcp` (via avahi); Solo does not yet use this for discovery and connects to a typed host/port instead.
Full message schema, framing and state machines are in [`docs/architecture.md`](docs/architecture.md#5-wire-protocol).

### Sensor / hardware interfaces (Dolo)

| Interface | Consumed by |
|---|---|
| 1090 MHz antenna + RTL-SDR (USB) | `dump1090-mutability`, which Dolo reads over SBS on `localhost:30003` |
| GNSS module on the Pi's UART (`/dev/ttyAMA0`), NMEA 0183 / u-blox UBX | Dolo directly, via a `GnssReceiver` class |
| gpsd (`localhost:2947`), as an alternative GNSS source | Dolo directly, via `GpsdReceiver` |

### Network interface (Dolo ↔ Solo)

A USB-C Ethernet adapter on the iPad, or a shared LAN/Wi-Fi network between the Pi and the iPad.

### Configuration interface

`/etc/dolo/dolo.yaml` (YAML; unknown keys rejected at startup). Every option is documented in [`dolo/config/dolo.yaml`](dolo/config/dolo.yaml).

## Dependencies

### Dolo (Raspberry Pi)

Hardware:
- A Raspberry Pi (tested on a Pi 5) running Raspberry Pi OS / Debian with `apt`.
- An RTL-SDR dongle (tested with a NooElec NESDR SMArt) and a 1090 MHz antenna.
- Optional: a GNSS module on the GPIO UART (a u-blox MAX-M8Q HAT, or a GT-U7), or anything gpsd supports.

Software (installed by [`dolo/deploy/install.sh`](dolo/deploy/install.sh), or by hand for development — see [`dolo/README.md`](dolo/README.md#build-and-run-by-hand)):
- Build: `cmake`, `g++` (C++20), `protobuf-compiler`, `libprotobuf-dev`, `libyaml-cpp-dev`, `nlohmann-json3-dev`, `libgtest-dev`
- Runtime: `dump1090-mutability` (ADS-B decode), `rtl-sdr`, `avahi-daemon`, optionally `gpsd`

### Solo (iPad)

Hardware:
- A Mac with Xcode 26 or later and an Apple Developer account for signing.
- An iPad on iPadOS 26.2 or later.
- A network path to the Pi.

Software:
- `apple/swift-protobuf` 1.40.x, resolved automatically via Swift Package Manager on first open of `SoloDolo.xcodeproj`.

### Shared (only needed when changing the wire protocol)

- `protoc` and `protoc-gen-swift`, via `brew install swift-protobuf`. See [`docs/architecture.md`](docs/architecture.md#schema-rules).
