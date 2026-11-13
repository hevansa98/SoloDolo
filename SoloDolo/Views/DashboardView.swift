//
//  DashboardView.swift
//  SoloDolo
//
//  Author: photon
//
//  Main dashboard, shown once connected. The connection indicator in the
//  top-left returns to the connect screen when tapped.
//

import SwiftUI
import MapKit

struct DashboardView: View {
    let bridge: BridgeConnection
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            // Tapping the indicator returns to the connect screen
            Button {
                dismiss()
            } label: {
                ConnectionIndicator(label: "D", status: bridge.status)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityHint("Shows connection settings")
            
            Spacer()

            HStack(spacing: 16) {
                TrafficMap(aircraft: NearbyAircraft.all(in: bridge.traffic, from: Ownship.assumedLocation),
                           ownship: Ownship.assumedLocation)
                    .dashboardPanel()
                
                Spacer()

                GPSStatusPanel(gps: bridge.gps)
                    .dashboardPanel()
            }

            TrafficPanel(aircraft: NearbyAircraft.closest(in: bridge.traffic, to: Ownship.assumedLocation),
                         totalCount: bridge.traffic?.aircraft.count ?? 0)

            Spacer(minLength: 0)
        }
        .padding()
    }
}

private extension View {
    /// Square tile with rounded corners, sized to share its row equally.
    func dashboardPanel() -> some View {
        self
            .aspectRatio(1, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: 12))
    }
}

#Preview {
    DashboardView(bridge: BridgeConnection())
}
