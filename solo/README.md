# Solo

The iPad half of SoloDolo (SwiftUI, Xcode target `SoloDolo`). Connects to **Dolo** over TCP and shows a live
dashboard: a traffic map, GPS health, and the five closest aircraft.

See [`../docs/architecture.md`](../docs/architecture.md) for the wire protocol, connection state machine and
dashboard design. For the source file layout, see [`../docs/architecture.md#2-repository-layout`](../docs/architecture.md#2-repository-layout).

## Install

1. Open `../SoloDolo.xcodeproj` in Xcode. The SwiftProtobuf package (1.40.x) resolves automatically on first open.
2. Select the **SoloDolo** target → **Signing & Capabilities**, and choose your development team.
3. Connect the iPad, select it as the run destination, and press **⌘R**.
   The first time, trust the developer certificate on the iPad: **Settings → General → VPN & Device Management**.

The app stays installed on the iPad after this. You can launch it from the Home Screen without Xcode.

## Run

1. Start Dolo on the Pi, and connect the iPad to the Pi's network.
2. Open **Solo**. Enter the Pi's address (from `hostname -I` on the Pi) and port **5555**, then tap **Connect**.
   The fields start out as `192.168.50.1` and `5555`.
3. The first time, iPadOS asks for **Local Network** access. Tap **Allow**, or Solo can't reach the Pi.
4. The dashboard opens once Dolo answers. If it doesn't, the connect screen shows why:
   - an orange gumball means "Waiting for host";
   - a red gumball means the connection failed, with the reason shown.

On the dashboard:
- The **map** shows ownship and every aircraft with a position.
- **GPS** shows fix, satellites, HDOP and PPS lock.
- **ADS-B** lists the five closest aircraft.

Ownship is a **blue** dot while Dolo has a GPS fix, and an **orange** dot at the last known position after the fix is lost. No dot is drawn until Dolo has reported a position. Until then, the ADS-B table uses the range and bearing Dolo measures from its `ownship_fallback`.

## Stop

- **Leave the dashboard:** tap the **D** gumball in the top-left. The connection stays open; tap **Connect** to reconnect or switch hosts.
- **Disconnect:** close Solo. Swipe up from the bottom edge and pause to open the app switcher, then swipe Solo away. Solo has no Disconnect button yet.
- **When running from Xcode:** press **⌘.** to stop the app.

If Dolo stops or the link drops, Solo shows a red "failed" status: immediately if Dolo closes the connection, or within about 15 seconds if the link goes silent. Tap **Connect** to try again.

## Testing without the Pi

Anything that speaks the protocol on port 5555 works, for example a small local stand-in server. In the Simulator, connect to `127.0.0.1`.

## Source map

| File | Responsibility |
|---|---|
| `App/SoloDoloApp.swift` | Entry point; owns the single shared `BridgeConnection` |
| `Networking/BridgeConnection.swift` | TCP connection, handshake, receive loop, keepalive; publishes `status`, `gps`, `traffic` |
| `Networking/FrameCodec.swift` | 4-byte length-prefix framing; 1 MiB frame limit |
| `Networking/EndpointValidation.swift` | Host (IPv4 or hostname) and port (1–65535) validation |
| `Models/GPSStatus.swift` | Fix, satellites, HDOP, PPS, position, and their quality thresholds |
| `Models/Ownship.swift` | Ownship position: the current GPS fix, or the last known one; none before the first fix |
| `Models/CoordinateFormatter.swift` | Lat/lon as DD.MM.SSSS (seconds to hundredths) and altitude as ft MSL |
| `Models/NearbyAircraft.swift` | Distance/bearing from ownship; sorting by distance |
| `Views/ContentView.swift` | Connect screen; opens the dashboard once connected |
| `Views/DashboardView.swift` | Map + GPS tiles, ADS-B table below |
| `Views/TrafficMap.swift`, `GPSStatusPanel.swift`, `TrafficPanel.swift` | The three dashboard tiles |
| `Views/ConnectionIndicator.swift`, `StatusDot.swift`, `ValidatedFieldStyle.swift` | Shared UI pieces |

`Generated/solodolo.pb.swift` is generated from `../proto/solodolo/v1/solodolo.proto` by `../scripts/generate-proto.sh`. Don't edit it by hand.
