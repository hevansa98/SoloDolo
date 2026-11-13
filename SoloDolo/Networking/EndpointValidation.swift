//
//  EndpointValidation.swift
//  SoloDolo
//

import Foundation
import Network

/// Parses user-entered host and port text into values `NWConnection` can use.
enum EndpointValidation {
    /// Returns the trimmed host if it's an IPv4 address or hostname, otherwise `nil`.
    static func host(from text: String) -> String? {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        return isValidIPv4(trimmed) || isValidHostname(trimmed) ? trimmed : nil
    }

    /// Returns the port for 1–65535. Empty or non-numeric text is invalid.
    static func port(from text: String) -> NWEndpoint.Port? {
        let trimmed = text.trimmingCharacters(in: .whitespaces)
        // Port 0 is a wildcard for listeners and can't be connected to
        guard trimmed.allSatisfy(\.isASCIIDigit),
              let value = UInt16(trimmed), value > 0 else { return nil }
        return NWEndpoint.Port(rawValue: value)
    }

    /// Dotted-quad IPv4 with each octet 0–255, e.g. "192.168.0.1".
    static func isValidIPv4(_ text: String) -> Bool {
        let octets = text.split(separator: ".", omittingEmptySubsequences: false)
        guard octets.count == 4 else { return false }
        return octets.allSatisfy { octet in
            guard !octet.isEmpty, octet.count <= 3, octet.allSatisfy(\.isASCIIDigit),
                  let value = Int(octet) else { return false }
            return (0...255).contains(value)
        }
    }

    /// RFC 1123 hostname, e.g. "solodolo.local". All-numeric names are rejected
    /// so a malformed IPv4 address like "192.168.1" isn't accepted as a hostname.
    static func isValidHostname(_ text: String) -> Bool {
        guard !text.isEmpty, text.count <= 253 else { return false }
        let labels = text.split(separator: ".", omittingEmptySubsequences: false)
        guard !labels.allSatisfy({ $0.allSatisfy(\.isASCIIDigit) }) else { return false }
        return labels.allSatisfy { label in
            guard (1...63).contains(label.count),
                  label.first != "-", label.last != "-" else { return false }
            return label.allSatisfy { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-") }
        }
    }
}

private extension Character {
    var isASCIIDigit: Bool { isASCII && isNumber }
}
