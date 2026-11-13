//
//  FrameCodec.swift
//  SoloDolo
//
//  Author: photon
//
//  Length-prefixed framing used on the Dolo stream: a 4-byte big-endian
//  length followed by that many bytes of protobuf Envelope.
//

import Foundation

enum FrameCodec {
    /// Matches Dolo's kMaxFrameBytes, so a corrupt length can't force a huge allocation.
    static let maxFrameBytes = 1 << 20

    static func encode(_ payload: Data) -> Data {
        var length = UInt32(payload.count).bigEndian
        var frame = Data(bytes: &length, count: 4)
        frame.append(payload)
        return frame
    }
}

enum FrameError: LocalizedError {
    case tooLarge(Int)

    var errorDescription: String? {
        switch self {
        case .tooLarge(let length): "Frame of \(length) bytes exceeds the 1 MiB limit"
        }
    }
}

/// Buffers bytes from the socket and splits them into whole frames.
/// TCP can deliver several frames, or part of one, in a single read.
struct FrameDecoder {
    private var buffer = Data()

    mutating func append(_ data: Data) {
        buffer.append(data)
    }

    /// Returns the next complete frame's payload, or `nil` if more bytes are needed.
    mutating func nextFrame() throws -> Data? {
        guard buffer.count >= 4 else { return nil }
        let length = buffer.prefix(4).reduce(0) { ($0 << 8) | Int($1) }
        guard length <= FrameCodec.maxFrameBytes else { throw FrameError.tooLarge(length) }
        guard buffer.count >= 4 + length else { return nil }

        let start = buffer.startIndex + 4
        let payload = buffer.subdata(in: start..<start + length)
        // Re-base so indices start at 0 again instead of growing forever
        buffer = Data(buffer[(start + length)...])
        return payload
    }
}
