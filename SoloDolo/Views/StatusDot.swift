//
//  StatusDot.swift
//  SoloDolo
//
//  Author: photon
//
//  The colored "gumball" dot used by status indicators, plus the color for
//  each SignalQuality.
//

import SwiftUI

/// Small filled circle showing a status color.
struct StatusDot: View {
    let color: Color

    var body: some View {
        Circle()
            .fill(color)
            .frame(width: 10, height: 10)
    }
}

extension SignalQuality {
    var color: Color {
        switch self {
        case .good: .green
        case .degraded: .orange
        case .bad: .red
        case .unknown: .gray
        }
    }

    /// Spoken by VoiceOver so the status doesn't rely on color alone.
    var description: String {
        switch self {
        case .good: "Good"
        case .degraded: "Degraded"
        case .bad: "Bad"
        case .unknown: "No data"
        }
    }
}
