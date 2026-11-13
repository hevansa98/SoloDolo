//
//  DashboardView.swift
//  SoloDolo
//

import SwiftUI

struct DashboardView: View {
    let bridge: BridgeConnection
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        // Tapping the indicator returns to the connect screen
        Button {
            dismiss()
        } label: {
            ConnectionIndicator(label: "D", status: bridge.status)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityHint("Shows connection settings")
        .padding()
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }
}

#Preview {
    DashboardView(bridge: BridgeConnection())
}
