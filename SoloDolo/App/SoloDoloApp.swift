//
//  SoloDoloApp.swift
//  SoloDolo
//
//  Created by odin on 11/9/26.
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
