//
//  ConnectionIndicator.swift
//  SoloDolo
//
//  Author: photon
//
//  Status "gumball": a label and a dot colored by connection status. Also
//  defines the colors and messages shown for each ConnectionStatus.
//

import SwiftUI

/// Label followed by a dot colored by the bridge connection status.
struct ConnectionIndicator: View {
    let label: String
    let status: ConnectionStatus

    var body: some View {
        HStack {
            Text(label)
                .font(.subheadline)
                .fontWeight(.medium)

            Circle()
                .fill(status.tint)
                .frame(width: 10, height: 10)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("Dolo connection")
        .accessibilityValue(status.message ?? "Not connected")
    }
}

extension ConnectionStatus {
    var tint: Color {
        switch self {
        case .connected: .green
        case .waiting: .orange
        case .failed: .red
        case .idle, .connecting: .blue
        }
    }

    var message: String? {
        switch self {
        case .idle: nil
        case .connecting: "Connecting…"
        case .waiting(let reason): "Waiting for host: \(reason)"
        case .connected: "Connected"
        case .failed(let reason): "Connection failed: \(reason)"
        }
    }
}
