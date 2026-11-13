//
//  BridgeConnection.swift
//  SoloDolo
//
//  Author: photon
//
//  Owns the TCP connection to Dolo (the Raspberry Pi bridge): sends the
//  ClientHello handshake, decodes Envelope frames, keeps the link alive
//  with Ping/Pong, and publishes connection status, GPS, and traffic.
//

import Foundation
import Network
import Observation
import os
import SwiftProtobuf

/// State of the current connection attempt to the bridge.
enum ConnectionStatus: Equatable {
    case idle
    case connecting        // TCP connecting, or waiting for ServerHello
    case waiting(String)   // NWConnection can't reach the host yet and keeps retrying
    case connected         // ServerHello received; data is streaming
    case failed(String)
}

/// Owns the TCP connection to the SoloDolo bridge and publishes what it streams.
@Observable
final class BridgeConnection {
    /// Must match Dolo's kProtocolVersion; Dolo closes the connection otherwise.
    static let protocolVersion: UInt32 = 1
    private static let pingInterval: Duration = .seconds(5)
    private static let pongTimeout: Duration = .seconds(10)

    private(set) var status: ConnectionStatus = .idle
    private(set) var gps = GPSStatus()
    private(set) var traffic: SDTrafficSnapshot?
    private(set) var bridgeVersion: String?

    @ObservationIgnored private var connection: NWConnection?
    @ObservationIgnored private var decoder = FrameDecoder()
    @ObservationIgnored private var keepaliveTask: Task<Void, Never>?
    @ObservationIgnored private var pingSeq: UInt32 = 0
    @ObservationIgnored private var lastPong = ContinuousClock.now
    @ObservationIgnored private let logger = Logger(subsystem: Bundle.main.bundleIdentifier ?? "SoloDolo", category: "BridgeConnection")

    /// Cancels any existing connection and starts a new one.
    func connect(host: String, port: NWEndpoint.Port) {
        stop()

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

    // MARK: - Connection lifecycle

    private func handle(_ state: NWConnection.State, from source: NWConnection, endpoint: String) {
        // Ignore late callbacks from a connection that was replaced or stopped
        guard source === connection else { return }

        switch state {
        case .preparing:
            status = .connecting
        case .ready:
            logger.info("TCP connected to \(endpoint); sending ClientHello")
            // Dolo drops clients that don't send ClientHello within 10 s
            send(.clientHello(clientHello()))
            receive(on: source)
        case .waiting(let error):
            // Unreachable hosts land here rather than in .failed; NWConnection retries on its own
            logger.notice("Waiting on \(endpoint): \(error)")
            status = .waiting(error.localizedDescription)
        case .failed(let error):
            fail("Connection to \(endpoint) failed", reason: error.localizedDescription)
        default:
            break
        }
    }

    /// Tears down the current connection without touching `status`.
    private func stop() {
        keepaliveTask?.cancel()
        keepaliveTask = nil
        // Clear first so the connection's own .cancelled callback is ignored
        let old = connection
        connection = nil
        old?.cancel()

        decoder = FrameDecoder()
        gps = GPSStatus()
        traffic = nil
        bridgeVersion = nil
    }

    private func fail(_ context: String, reason: String) {
        logger.error("\(context): \(reason)")
        stop()
        status = .failed(reason)
    }

    // MARK: - Receiving

    private func receive(on source: NWConnection) {
        source.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
            MainActor.assumeIsolated {
                self?.handleReceive(data, isComplete: isComplete, error: error, from: source)
            }
        }
    }

    private func handleReceive(_ data: Data?, isComplete: Bool, error: NWError?, from source: NWConnection) {
        guard source === connection else { return }

        if let data, !data.isEmpty {
            decoder.append(data)
            do {
                while let payload = try decoder.nextFrame() {
                    handle(try SDEnvelope(serializedBytes: payload))
                    // Handling a frame can fail the connection (e.g. version mismatch)
                    guard source === connection else { return }
                }
            } catch {
                fail("Bad frame from Dolo", reason: error.localizedDescription)
                return
            }
        }

        if let error {
            fail("Receive failed", reason: error.localizedDescription)
        } else if isComplete {
            // Dolo closes the socket on a bad handshake, an oversized frame, or shutdown
            fail("Dolo closed the connection", reason: "Dolo closed the connection")
        } else {
            receive(on: source)
        }
    }

    private func handle(_ envelope: SDEnvelope) {
        switch envelope.payload {
        case .serverHello(let hello):
            guard hello.protocolVersion == Self.protocolVersion else {
                fail("Unsupported protocol", reason: "Dolo speaks protocol v\(hello.protocolVersion), Solo speaks v\(Self.protocolVersion)")
                return
            }
            logger.info("ServerHello from Dolo \(hello.bridgeVersion)")
            bridgeVersion = hello.bridgeVersion
            gps.ppsLocked = hello.ppsLocked
            status = .connected
            startKeepalive()
        case .gnss(let fix):
            gps.update(from: fix)
        case .traffic(let snapshot):
            traffic = snapshot
        case .pong:
            lastPong = .now
        case .clientHello, .ping, nil:
            // Client-to-server messages, or a payload this build doesn't know about yet
            break
        }
    }

    // MARK: - Sending

    private func send(_ payload: SDEnvelope.OneOf_Payload) {
        guard let connection else { return }
        var envelope = SDEnvelope()
        envelope.sentUnixMs = UInt64(Date().timeIntervalSince1970 * 1000)
        envelope.payload = payload
        do {
            let bytes: Data = try envelope.serializedBytes()
            connection.send(content: FrameCodec.encode(bytes), completion: .contentProcessed { [weak self] error in
                guard let error else { return }
                MainActor.assumeIsolated {
                    self?.logger.error("Send failed: \(error)")
                }
            })
        } catch {
            logger.error("Couldn't encode envelope: \(error)")
        }
    }

    private func clientHello() -> SDClientHello {
        var hello = SDClientHello()
        hello.protocolVersion = Self.protocolVersion
        let info = Bundle.main.infoDictionary
        let version = info?["CFBundleShortVersionString"] as? String ?? "?"
        let build = info?["CFBundleVersion"] as? String ?? "?"
        hello.appVersion = "\(version) (\(build))"
        return hello
    }

    /// Pings every 5 s; gives up if Dolo hasn't answered for 10 s.
    private func startKeepalive() {
        keepaliveTask?.cancel()
        lastPong = .now
        keepaliveTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(for: Self.pingInterval)
                guard let self, !Task.isCancelled else { return }
                if ContinuousClock.now - self.lastPong > Self.pongTimeout {
                    self.fail("Keepalive timed out", reason: "No response from Dolo for 10 seconds")
                    return
                }
                self.pingSeq &+= 1
                var ping = SDPing()
                ping.seq = self.pingSeq
                self.send(.ping(ping))
            }
        }
    }
}

private extension GPSStatus {
    mutating func update(from fix: SDGnssFix) {
        switch fix.fixMode {
        case .noFix: fixMode = .noFix
        case .fixMode2D: fixMode = .twoD
        case .fixMode3D: fixMode = .threeD
        case .unspecified, .UNRECOGNIZED: fixMode = nil
        }
        satellitesUsed = Int(fix.satellitesUsed)
        satellitesVisible = Int(fix.satellitesVisible)
        hdop = fix.hasHdop ? Double(fix.hdop) : nil
    }
}
