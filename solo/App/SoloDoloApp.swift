//
//  SoloDoloApp.swift
//  SoloDolo
//
//  Author: photon
//
//  App entry point. Creates the shared BridgeConnection and shows the
//  connect screen in the main window.
//

import SwiftUI

@main
struct SoloDoloApp: App {
    // Shared by every screen so they all reflect the same connection
    @State private var bridge = BridgeConnection()

    var body: some Scene {
        WindowGroup {
            ContentView(bridge: bridge)
        }
    }
}
