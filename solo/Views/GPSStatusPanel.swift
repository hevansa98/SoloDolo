//
//  GPSStatusPanel.swift
//  SoloDolo
//
//  Author: photon
//
//  Dashboard tile listing GPS health as gumballs (fix, satellites, HDOP,
//  PPS lock) and the current position: lat/lon as DD.MM.SSSS, altitude MSL.
//

import SwiftUI

struct GPSStatusPanel: View {
    let gps: GPSStatus

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("GPS")
                .font(.headline)

            row("Fix", value: fixText, quality: gps.fixQuality)
            row("Satellites", value: satellitesText, quality: gps.satelliteQuality)
            row("HDOP", value: gps.hdop.map { $0.formatted(.number.precision(.fractionLength(1))) },
                quality: gps.hdopQuality)
            row("PPS", value: gps.ppsLocked.map { $0 ? "Locked" : "Unlocked" }, quality: gps.ppsQuality)

            Divider()

            positionRow("Lat", value: gps.latitudeDeg.map { CoordinateFormatter.dms($0, isLatitude: true) })
            positionRow("Lon", value: gps.longitudeDeg.map { CoordinateFormatter.dms($0, isLatitude: false) })
            positionRow("Alt", value: gps.altitudeMslM.map(CoordinateFormatter.altitudeMSL))

            Spacer(minLength: 0)
        }
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .background(Color(.secondarySystemBackground))
    }

    private func row(_ label: String, value: String?, quality: SignalQuality) -> some View {
        HStack {
            Text(label)
            Spacer()
            Text(value ?? "—")
                .foregroundStyle(.secondary)
                .monospacedDigit()
            StatusDot(color: quality.color)
        }
        .font(.subheadline)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label)
        .accessibilityValue("\(value ?? "No data"), \(quality.description)")
    }

    /// Position values have no quality dot; they're blank ("—") without a fix.
    private func positionRow(_ label: String, value: String?) -> some View {
        HStack {
            Text(label)
            Spacer()
            Text(value ?? "—")
                .foregroundStyle(.secondary)
                .monospacedDigit()
        }
        .font(.subheadline)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label)
        .accessibilityValue(value ?? "No data")
    }

    private var fixText: String? {
        switch gps.fixMode {
        case .threeD: "3D"
        case .twoD: "2D"
        case .noFix: "No fix"
        case nil: nil
        }
    }

    /// Satellites used out of visible, e.g. "8/12".
    private var satellitesText: String? {
        guard let used = gps.satellitesUsed else { return nil }
        guard let visible = gps.satellitesVisible else { return "\(used)" }
        return "\(used)/\(visible)"
    }
}

#Preview("No data") {
    GPSStatusPanel(gps: GPSStatus())
        .frame(width: 320, height: 320)
}

#Preview("Mixed") {
    GPSStatusPanel(gps: GPSStatus(fixMode: .threeD, satellitesUsed: 5, satellitesVisible: 11, hdop: 6.3,
                                  latitudeDeg: 33.0462, longitudeDeg: -96.9942, altitudeMslM: 165, ppsLocked: true))
        .frame(width: 320, height: 320)
}
