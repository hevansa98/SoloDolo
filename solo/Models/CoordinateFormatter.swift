//
//  CoordinateFormatter.swift
//  SoloDolo
//
//  Author: photon
//
//  Formats latitude/longitude as degrees.minutes.seconds (DD.MM.SSSS, the
//  last four digits being seconds to hundredths) and altitude as feet MSL.
//

import Foundation

/// Pure string formatting, so it's nonisolated and usable from any context (e.g. `Optional.map`).
nonisolated enum CoordinateFormatter {
    /// e.g. 33.046200 → "N 33.02.4632", -96.994200 → "W 096.59.3912".
    static func dms(_ degrees: Double, isLatitude: Bool) -> String {
        let hemisphere = isLatitude ? (degrees < 0 ? "S" : "N") : (degrees < 0 ? "W" : "E")
        // Round once in hundredths of a second so 59.999" carries into the minutes
        let hundredths = Int((abs(degrees) * 360_000).rounded())
        let wholeDegrees = hundredths / 360_000
        let minutes = (hundredths % 360_000) / 6_000
        let seconds = hundredths % 6_000
        let degreeDigits = isLatitude ? "%02d" : "%03d"
        return "\(hemisphere) " + String(format: "\(degreeDigits).%02d.%04d", wholeDegrees, minutes, seconds)
    }

    private static let feetPerMeter = 3.28084

    /// e.g. 165.0 m → "541 ft MSL".
    static func altitudeMSL(meters: Double) -> String {
        "\(Int((meters * feetPerMeter).rounded()).formatted()) ft MSL"
    }
}
