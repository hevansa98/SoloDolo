//
//  TrafficPanel.swift
//  SoloDolo
//
//  Author: photon
//
//  Dashboard tile listing the closest ADS-B aircraft to ownship: callsign,
//  distance, bearing, altitude, speed, track, and squawk.
//

import SwiftUI

struct TrafficPanel: View {
    let aircraft: [NearbyAircraft]
    let totalCount: Int

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack(alignment: .firstTextBaseline) {
                Text("ADS-B")
                    .font(.headline)
                Spacer()
                Text("Closest \(aircraft.count) of \(totalCount)")
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
            }

            if aircraft.isEmpty {
                Text("No aircraft with a position yet")
                    .foregroundStyle(.secondary)
            } else {
                table
            }
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .topLeading)
        .background(Color(.secondarySystemBackground), in: RoundedRectangle(cornerRadius: 12))
    }

    private var table: some View {
        Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 8) {
            GridRow {
                ForEach(Self.columns, id: \.self) { Text($0) }
            }
            .font(.caption)
            .foregroundStyle(.secondary)
            .accessibilityHidden(true)

            Divider()

            ForEach(aircraft) { plane in
                GridRow {
                    Text(plane.displayName)
                        .fontWeight(.medium)
                    Text(plane.distanceNm.formatted(.number.precision(.fractionLength(1))) + " nm")
                    Text(Self.degrees(plane.bearingDeg))
                    Text(altitudeText(plane))
                    Text(plane.groundSpeedKt.map { "\(Int($0.rounded())) kt" } ?? "—")
                    Text(plane.trackDeg.map { Self.degrees(Double($0)) } ?? "—")
                    Text(plane.squawk ?? "—")
                }
                .monospacedDigit()
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(accessibilityText(plane))
            }
        }
        .font(.subheadline)
    }

    private static let columns = ["Callsign", "Distance", "Bearing", "Altitude", "Speed", "Track", "Squawk"]

    /// Zero-padded heading, e.g. "045°".
    private static func degrees(_ value: Double) -> String {
        String(format: "%03d°", Int(value.rounded()) % 360)
    }

    private func altitudeText(_ plane: NearbyAircraft) -> String {
        if plane.onGround { return "GND" }
        return plane.altitudeFt.map { "\($0.formatted()) ft" } ?? "—"
    }

    private func accessibilityText(_ plane: NearbyAircraft) -> String {
        var parts = [
            plane.displayName,
            "\(plane.distanceNm.formatted(.number.precision(.fractionLength(1)))) nautical miles",
            "bearing \(Int(plane.bearingDeg.rounded()) % 360) degrees",
            altitudeText(plane) == "—" ? "altitude unknown" : altitudeText(plane).replacingOccurrences(of: "GND", with: "on ground"),
        ]
        if let squawk = plane.squawk { parts.append("squawk \(squawk)") }
        return parts.joined(separator: ", ")
    }
}

#Preview("Traffic") {
    TrafficPanel(aircraft: NearbyAircraft.closest(in: .previewSample, to: Ownship.assumedLocation),
                 totalCount: SDTrafficSnapshot.previewSample.aircraft.count)
        .padding()
}

#Preview("Empty") {
    TrafficPanel(aircraft: [], totalCount: 0)
        .padding()
}

extension SDTrafficSnapshot {
    /// Made-up traffic around Lewisville / DFW for previews.
    static var previewSample: SDTrafficSnapshot {
        func plane(_ icao: UInt32, _ callsign: String?, _ lat: Double, _ lon: Double, alt: Int32?,
                   kt: Float, track: Float, squawk: String? = nil, ground: Bool = false) -> SDAircraft {
            var a = SDAircraft()
            a.icaoAddress = icao
            if let callsign { a.callsign = callsign }
            a.latitudeDeg = lat
            a.longitudeDeg = lon
            if let alt { a.altitudeBaroFt = alt }
            a.groundSpeedKt = kt
            a.trackTrueDeg = track
            if let squawk { a.squawk = squawk }
            a.onGround = ground
            return a
        }
        var snapshot = SDTrafficSnapshot()
        snapshot.aircraft = [
            plane(0xA1B2C3, "AAL1423", 32.98, -97.03, alt: 4_200, kt: 210, track: 177, squawk: "3341"),
            plane(0xA4F001, "SWA88", 33.10, -96.88, alt: 11_000, kt: 320, track: 245, squawk: "5012"),
            plane(0xA0C9E2, nil, 33.07, -97.02, alt: 2_500, kt: 105, track: 90, squawk: "1200"),
            plane(0xAB12CD, "N512TX", 33.069, -96.887, alt: nil, kt: 0, track: 0, ground: true),
            plane(0xA77001, "UAL2201", 32.90, -97.04, alt: 1_800, kt: 160, track: 178, squawk: "2207"),
            plane(0xA33333, "DAL910", 33.30, -97.30, alt: 23_000, kt: 450, track: 120, squawk: "4410"),
            plane(0xA55555, "ENY3301", 32.85, -96.85, alt: 7_500, kt: 280, track: 350, squawk: "6623"),
        ]
        var noPosition = SDAircraft()
        noPosition.icaoAddress = 0xA99999
        snapshot.aircraft.append(noPosition)   // skipped: no decoded position yet
        return snapshot
    }
}
