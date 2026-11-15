# Dolo

The Raspberry Pi half of SoloDolo (C++20). Reads ADS-B from dump1090 (NooElec RTL-SDR) and GNSS (from a
MAX-M8Q or GT-U7 module over the Pi's UART, or from gpsd), and streams both to the **Solo** iPad app over TCP.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for how Dolo is put together (data flow, hub, GNSS receiver classes).

## Compartmentalization

- `dolo/` is self-contained; it never includes or reads anything from `solo/` (the iOS app).
- The only shared artifact is the wire contract `../proto/solodolo/v1/solodolo.proto`
  (see `../docs/architecture.md` §4). CMake generates the C++ from it at build time; nothing generated is committed.
- Solo likewise only needs the `.proto`, never anything under `dolo/`.

## Data flow

```
antenna → SDR → dump1090-mutability ──SBS :30003──▶ Dolo ──protobuf :5555──▶ Solo (iPad)
                (demodulate/decode)                  merge, range/bearing,    └ JSON copy on :5556
                                                     1 Hz snapshots
```

## Layout

| Path | Purpose |
|---|---|
| `src/sbs.*` | dump1090 SBS line parser |
| `src/aircraft.*` | merges per-ICAO state, prunes stale aircraft, builds `TrafficSnapshot` |
| `src/gnss/receiver.*` | `GnssReceiver`: the interface Dolo uses for any GNSS hardware or source |
| `src/gnss/nmea.*`, `ubx.*` | NMEA 0183 and u-blox UBX protocol codecs; know nothing about hardware |
| `src/gnss/stream.*` | splits the UART byte stream into NMEA sentences and UBX frames |
| `src/gnss/uart_receiver.*` | `UartGnssReceiver`: serial port + the sentence-to-state mapping shared by UART modules |
| `src/gnss/max_m8q.*`, `gt_u7.*` | one class per module (`MaxM8qReceiver`: UBX with NMEA fallback, `GtU7Receiver`: NMEA) |
| `src/gnss/gpsd.*` | `GpsdReceiver`: gpsd's TPV/SKY JSON stream |
| `src/gnss/state.*`, `factory.*` | `GnssState` → `GnssFix`; config name → receiver class |
| `src/serial.*` | serial port, bytes in and out (reopens the device after errors) |
| `src/hub.*` | :5555 protobuf (handshake, ping/pong, replay of latest state) and :5556 NDJSON debug mirror |
| `src/net.*`, `src/event_loop.*` | non-blocking sockets and a single-threaded epoll loop |
| `src/config.*`, `config/dolo.yaml` | YAML configuration (unknown keys are rejected) |
| `tests/tests.cpp` | unit tests plus a hub integration test; no test framework needed |
| `deploy/` | systemd unit, avahi `_solodolo._tcp` record |

## Build and run

```bash
sudo apt install cmake g++ protobuf-compiler libprotobuf-dev libyaml-cpp-dev nlohmann-json3-dev
cd dolo
cmake -S . -B build && cmake --build build -j
./build/dolo_tests

# 1. Start the ADS-B decoder first; Dolo reads its SBS output on :30003.
sudo systemctl start dump1090-mutability
ss -ltn | grep :30003          # wait until this shows a listener
# 2. Start Dolo (it retries the SBS connection every 5 s, so the order is forgiving but traffic stays empty until step 1).
./build/dolo --config config/dolo.yaml
nc localhost 5556              # watch the JSON stream
```

Stop in the reverse order: `Ctrl-C` Dolo, then `sudo systemctl stop dump1090-mutability`. If dump1090 is not installed or
configured yet, run `./deploy/install.sh`, or for a quick test run it by hand instead of step 1:
`dump1090-mutability --net --net-sbs-port 30003 --gain -10 --fix --quiet`.

Configuration lives in YAML; see `config/dolo.yaml` for every option. Without `--config`, Dolo uses
`/etc/dolo/dolo.yaml` if it exists, else built-in defaults.

ADS-B source: `dump1090-mutability` (config in `/etc/default/dump1090-mutability`, SBS on :30003 bound to
localhost). `sudo systemctl enable --now dump1090-mutability` starts it now and on every boot.

GNSS is off until `gnss.enabled: true`. `gnss.receiver` picks the hardware: `max-m8q` (u-blox MAX-M8Q HAT, the
default), `gt-u7`, or `gpsd` (follows a running gpsd). The two module receivers read straight from `gnss.device`
(Pi 5 GPIO UART: `dtparam=uart0=on` in `config.txt`, no serial console, `/dev/ttyAMA0`, 9600 baud unless `gnss.baud`
says otherwise). The MAX-M8Q receiver switches the module to UBX messages at startup, which needs the module's RX
wired to the Pi's TX (GPIO14, pin 8); without that wire it simply stays on NMEA. See `ARCHITECTURE.md`. While GNSS is off or has no fix, `ownship_fallback` (the receiver location) is used so range and
bearing are still populated.

### Adding a GNSS receiver

Each hardware model is its own class behind `GnssReceiver`, so swapping parts never touches `main.cpp`:

1. Subclass `UartGnssReceiver` (UART modules that speak NMEA, optionally with a vendor binary protocol) and implement `onGsa`/`onGsv` for how that module
   reports fix type and satellites in view; or subclass `GnssReceiver` directly for a different protocol.
2. Add the class to `CMakeLists.txt` and one line each to `isKnownGnssReceiver` and `makeGnssReceiver`
   in `src/gnss/factory.cpp`.
3. Add a test next to `testMaxM8q` that feeds real captured sentences through `feed()`.

## Install as a service

`./deploy/start.sh` is the one-command way to run everything: it installs Dolo if needed (`install.sh`), otherwise
starts `dump1090-mutability`, waits for its SBS port, starts `dolo`, and prints the service status and the latest GNSS
report. `./deploy/start.sh restart` after editing the config, `./deploy/start.sh stop` to stop both.

```bash
./deploy/install.sh              # installs packages, dump1090, builds and installs Dolo, starts all services
./deploy/install.sh --uninstall  # removes Dolo (keeps dump1090 and /etc/dolo)
```

It is safe to re-run and never overwrites an existing `/etc/dolo/dolo.yaml`. On a fresh install the receiver
position in `dolo.yaml` is copied from dump1090's `LAT`/`LON`; to set a new one, run
`LAT=.. LON=.. ./deploy/install.sh` (dump1090 config) and edit `ownship_fallback` in `/etc/dolo/dolo.yaml`.

Services: `dump1090-mutability` (decoder, SBS on localhost:30003), `dolo` (starts after it, restarts on failure),
`avahi-daemon` (advertises `_solodolo._tcp`). Logs: `journalctl -u dolo -f`.

## Status

Verified end-to-end on this Pi: NooElec NESDR SMArt → dump1090-mutability → Dolo, ~47 aircraft with range/bearing
on :5556. The gpsd path is tested against canned messages only; the NMEA and UBX paths against data captured from a live GT-U7 and MAX-M8Q (3D fix on both). Not yet tested against the Solo app.
