//
//  ContentView.swift
//  SoloDolo
//
//  Author: photon
//
//  Connect screen: host and port fields with inline validation and a Connect
//  button. Opens the dashboard automatically once the bridge connects.
//

import SwiftUI
import Network

struct ContentView: View {
    // Defaults match the bridge's DHCP address and protobuf port in docs/architecture.md
    @State private var hostText = "192.168.50.1"
    // Kept as raw text so Connect always validates exactly what's on screen
    @State private var portText = "5555"
    @State private var hostInvalid = false
    @State private var portInvalid = false
    @State private var showingDashboard = false
    let bridge: BridgeConnection

    var body: some View {
        VStack {
            ConnectionIndicator(label: "Dolo Connection", status: bridge.status)

            TextField("ADSB Source Host or IP", text: $hostText)
                .keyboardType(.URL)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .validatedFieldStyle(isInvalid: hostInvalid)
                .onSubmit { _ = validateHost() }
                // Clear the error as soon as the user starts correcting the value
                .onChange(of: hostText) { hostInvalid = false }

            TextField("ADSB Source Port", text: $portText)
                .keyboardType(.numberPad)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .validatedFieldStyle(isInvalid: portInvalid)
                .onSubmit { _ = validatePort() }
                .onChange(of: portText) { portInvalid = false }

            Button(action: connect) {
                Text("Connect")
                    .font(.headline)
                    .padding()
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            .tint(bridge.status.tint)

            if let message = bridge.status.message {
                Text(message)
                    .font(.footnote)
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
        // Jump to the dashboard as soon as the bridge connects
        .onChange(of: bridge.status) { _, status in
            if status == .connected { showingDashboard = true }
        }
        .fullScreenCover(isPresented: $showingDashboard) {
            DashboardView(bridge: bridge)
        }
    }

    private func connect() {
        // Validate both fields before bailing out so each one updates its color
        let host = validateHost()
        let port = validatePort()
        guard let host, let port else { return }
        bridge.connect(host: host, port: port)
    }

    /// Validates the host field, writes back the trimmed value, and updates its error color.
    private func validateHost() -> String? {
        let host = EndpointValidation.host(from: hostText)
        hostInvalid = host == nil
        if let host { hostText = host }
        return host
    }

    /// Validates the port field and updates its error color.
    private func validatePort() -> NWEndpoint.Port? {
        let port = EndpointValidation.port(from: portText)
        portInvalid = port == nil
        return port
    }
}

#Preview {
    ContentView(bridge: BridgeConnection())
}
