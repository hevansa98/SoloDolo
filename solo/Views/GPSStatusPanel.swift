//
//  GPSStatusPanel.swift
//  SoloDolo
//
//  Author: photon
//
//  Dashboard tile listing GPS health as gumballs: fix, satellites, HDOP,
//  and PPS lock.
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
    GPSStatusPanel(gps: GPSStatus(fixMode: .threeD, satellitesUsed: 5, satellitesVisible: 11,
                                  hdop: 6.3, ppsLocked: true))
        .frame(width: 320, height: 320)
}
