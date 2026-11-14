# SoloDolo

A portable traffic display. **Dolo** runs on a Raspberry Pi with an RTL-SDR and a GNSS module. It decodes ADS-B
aircraft broadcasts on 1090 MHz, reads the receiver's own position, and streams both to **Solo**, an iPad app.
Solo shows a live dashboard: a traffic map, GPS health, and the five closest aircraft.

```
1090 MHz antenna → RTL-SDR → dump1090 ─┐
                                       ├─▶ Dolo (Pi) ══ TCP :5555, protobuf ══▶ Solo (iPad)
GNSS antenna → GNSS module (NMEA) ─────┘        └─ :5556 JSON copy for debugging
```

For the design, wire protocol and state machines, see [`docs/architecture.md`](docs/architecture.md).
For Dolo's internals and configuration, see [`dolo/README.md`](dolo/README.md).

## Repository layout

| Path | Contents |
|---|---|
| `dolo/` | Pi bridge (C++20). Build files, sources, tests, `deploy/` scripts |
| `solo/` | iPad app sources (Xcode target `SoloDolo`) |
| `proto/` | `solodolo.proto`, the wire contract shared by both halves |
| `scripts/` | `generate-proto.sh` regenerates Solo's Swift protobuf code |
| `docs/` | Architecture document |
| `SoloDolo.xcodeproj` | Xcode project for Solo |

## What you need

**Dolo:**
- A Raspberry Pi (tested on a Pi 5) running Raspberry Pi OS / Debian with `apt`.
- An RTL-SDR dongle (tested with a NooElec NESDR SMArt) and a 1090 MHz antenna.
- Optional: a serial GNSS module (tested with a GT-U7 on the GPIO UART), or anything gpsd supports.

**Solo:**
- A Mac with Xcode 26 or later and an Apple Developer account for signing.
- An iPad on iPadOS 26.2 or later.
- A network path to the Pi: a USB-C Ethernet adapter, or the same LAN/Wi-Fi.

## Dolo (Raspberry Pi)

All commands run on the Pi, from the repository root, as your normal user. The scripts use `sudo` themselves.

### Install

```bash
./dolo/deploy/install.sh
```

The installer is safe to re-run. It:
- installs the build tools, `dump1090-mutability`, `rtl-sdr` and `avahi-daemon`;
- builds Dolo and installs it to `/usr/local/bin/dolo`;
- writes `/etc/dolo/dolo.yaml` if it doesn't exist yet;
- enables and starts the `dump1090-mutability`, `dolo` and `avahi-daemon` services, so the stack also starts on boot.

Options:

```bash
GNSS=serial ./dolo/deploy/install.sh             # fresh install that reads NMEA from the GNSS module's serial port
LAT=33.0462 LON=-96.9942 ./dolo/deploy/install.sh  # set the receiver position used when there's no GNSS fix
```

- **A serial GNSS module on a Pi 5** also needs `dtparam=uart0=on` in `/boot/firmware/config.txt`, and the serial console disabled.
- **After editing `/etc/dolo/dolo.yaml`**, restart Dolo (see below). Every option is documented in [`dolo/config/dolo.yaml`](dolo/config/dolo.yaml).
- **Receiver position:** `LAT`/`LON` update dump1090's config. On a fresh install they're also copied into `ownship_fallback` in `/etc/dolo/dolo.yaml`. On an existing install, edit `ownship_fallback` there yourself.

### Run

```bash
./dolo/deploy/start.sh           # start dump1090, wait for it, start dolo, print status
./dolo/deploy/start.sh restart   # restart both, e.g. after editing /etc/dolo/dolo.yaml
```

`start.sh` runs `install.sh` first if Dolo isn't installed yet. To check that Dolo is running:

```bash
hostname -I                      # the Pi's IP address, which you'll type into Solo
journalctl -u dolo -f            # Dolo's log; shows "client … connected" when Solo connects
nc localhost 5556                # watch the live stream as JSON (Ctrl-C to quit)
```

### Stop

```bash
./dolo/deploy/start.sh stop      # stops dolo, then dump1090-mutability
```

The services are still enabled, so they come back on the next boot. To turn that off:

```bash
sudo systemctl disable dolo dump1090-mutability
```

To remove Dolo entirely (dump1090 and `/etc/dolo` are kept):

```bash
./dolo/deploy/install.sh --uninstall
```

To build and run Dolo by hand without systemd, e.g. while developing, see [`dolo/README.md`](dolo/README.md#build-and-run).

## Solo (iPad)

### Install

1. Open `SoloDolo.xcodeproj` in Xcode. The SwiftProtobuf package (1.40.x) resolves automatically on first open.
2. Select the **SoloDolo** target → **Signing & Capabilities**, and choose your development team.
3. Connect the iPad, select it as the run destination, and press **⌘R**.
   The first time, trust the developer certificate on the iPad: **Settings → General → VPN & Device Management**.

The app stays installed on the iPad after this. You can launch it from the Home Screen without Xcode.

### Run

1. Start Dolo on the Pi, and connect the iPad to the Pi's network.
2. Open **Solo**. Enter the Pi's address (from `hostname -I`) and port **5555**, then tap **Connect**.
   The fields start out as `192.168.50.1` and `5555`.
3. The first time, iPadOS asks for **Local Network** access. Tap **Allow**, or Solo can't reach the Pi.
4. The dashboard opens once Dolo answers. If it doesn't, the connect screen shows why:
   - an orange gumball means "Waiting for host";
   - a red gumball means the connection failed, with the reason shown.

On the dashboard:
- The **map** shows ownship and every aircraft with a position.
- **GPS** shows fix, satellites, HDOP and PPS lock.
- **ADS-B** lists the five closest aircraft.

Until Dolo reports a 2D/3D GPS fix, distances are measured from an assumed position: Lewisville City Hall, shown as a gray dot on the map.

### Stop

- **Leave the dashboard:** tap the **D** gumball in the top-left. The connection stays open; tap **Connect** to reconnect or switch hosts.
- **Disconnect:** close Solo. Swipe up from the bottom edge and pause to open the app switcher, then swipe Solo away. Solo has no Disconnect button yet.
- **When running from Xcode:** press **⌘.** to stop the app.

If Dolo stops or the link drops, Solo shows a red "failed" status: immediately if Dolo closes the connection, or within about 15 seconds if the link goes silent. Tap **Connect** to try again.

### Testing without the Pi

Anything that speaks the protocol on port 5555 works, for example a small local stand-in server. In the Simulator, connect to `127.0.0.1`.

## Changing the wire protocol

Edit `proto/solodolo/v1/solodolo.proto`, following the schema rules in [`docs/architecture.md`](docs/architecture.md#schema-rules). Then:

```bash
brew install swift-protobuf        # once; provides protoc and protoc-gen-swift
./scripts/generate-proto.sh        # regenerates solo/Generated/solodolo.pb.swift
```

Dolo regenerates its C++ code automatically on the next build.
