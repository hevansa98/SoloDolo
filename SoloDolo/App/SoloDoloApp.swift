//
//  SoloDoloApp.swift
//  SoloDolo
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
