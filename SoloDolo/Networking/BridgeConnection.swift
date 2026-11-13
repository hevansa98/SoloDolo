//
//  BridgeConnection.swift
//  SoloDolo
//
//  Author: photon
//
//  Owns the TCP connection to the SoloDolo bridge on the Raspberry Pi and
//  publishes its status (idle, connecting, waiting, connected, failed).
//

import Foundation
import Network
import Observation
import os

/// State of the current connection attempt to the bridge.
enum ConnectionStatus: Equatable {
    case idle
    case connecting
    case waiting(String)   // NWConnection can't reach the host yet and keeps retrying
    case connected
    case failed(String)
}

/// Owns the TCP connection to the SoloDolo bridge and publishes its status.
@Observable
final class BridgeConnection {
    private(set) var status: ConnectionStatus = .idle
    // Filled in once GnssFix and ServerHello frames are decoded
    private(set) var gps = GPSStatus()

    @ObservationIgnored private var connection: NWConnection?
    @ObservationIgnored private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "SoloDolo", category: "BridgeConnection")

    /// Cancels any existing connection and starts a new one.
    func connect(host: String, port: NWEndpoint.Port) {
        connection?.cancel()

        let newConnection = NWConnection(host: NWEndpoint.Host(host), port: port, using: .tcp)
        connection = newConnection
        status = .connecting

        newConnection.stateUpdateHandler = { [weak self] state in
            // Callbacks arrive on the main queue (see start below)
            MainActor.assumeIsolated {
                self?.handle(state, from: newConnection, endpoint: "\(host):\(port)")
            }
        }
        newConnection.start(queue: .main)
    }

    private func handle(_ state: NWConnection.State, from source: NWConnection, endpoint: String) {
        // Ignore late callbacks (e.g. .cancelled) from a connection that was replaced
        guard source === connection else { return }

        switch state {
        case .preparing:
            status = .connecting
        case .ready:
            logger.info("Connected to \(endpoint)")
            status = .connected
        case .waiting(let error):
            // Unreachable hosts land here rather than in .failed; NWConnection retries on its own
            logger.notice("Waiting on \(endpoint): \(error)")
            status = .waiting(error.localizedDescription)
        case .failed(let error):
            logger.error("Connection to \(endpoint) failed: \(error)")
            status = .failed(error.localizedDescription)
        case .cancelled:
            status = .idle
        default:
            break
        }
    }
}
