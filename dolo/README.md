# Dolo

The Raspberry Pi half of SoloDolo (C++20). Reads ADS-B from dump1090 (NooElec RTL-SDR) and, once a GNSS HAT
is fitted, GNSS from gpsd, and streams both to the **Solo** iPad app over TCP.

## Compartmentalization

- `dolo/` is self-contained; it never includes or reads anything from `SoloDolo/` (the iOS app).
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
| `src/gnss.*` | gpsd TPV/SKY → `GnssFix` (optional) |
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
./build/dolo --config config/dolo.yaml
nc localhost 5556              # watch the JSON stream
```

Configuration lives in YAML; see `config/dolo.yaml` for every option. Without `--config`, Dolo uses
`/etc/dolo/dolo.yaml` if it exists, else built-in defaults.

ADS-B source: `dump1090-mutability` (config in `/etc/default/dump1090-mutability`, SBS on :30003 bound to
localhost). `sudo systemctl enable --now dump1090-mutability`, or for a quick test run
`dump1090-mutability --net --net-sbs-port 30003 --gain -10 --fix --quiet`.

GNSS is off until `gnss.enabled: true`. Until then set `ownship_fallback` to the receiver location so range and
bearing are populated.

## Install as a service

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
on :5556. The gnss path is tested against canned gpsd messages only (no HAT). Not yet tested against the Solo app.
