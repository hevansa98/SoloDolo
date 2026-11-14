//
//  NearbyAircraft.swift
//  SoloDolo
//
//  Author: photon
//
//  Turns Dolo's TrafficSnapshot into aircraft ranked by distance from
//  ownship.
//

import CoreLocation

/// One aircraft, with distance and bearing measured from ownship.
struct NearbyAircraft: Identifiable {
    let id: UInt32              // 24-bit ICAO address
    let callsign: String?
    let squawk: String?
    let coordinate: CLLocationCoordinate2D
    let distanceNm: Double
    let bearingDeg: Double      // true bearing from ownship to the aircraft
    let altitudeFt: Int32?      // barometric, falling back to geometric
    let groundSpeedKt: Float?
    let trackDeg: Float?
    let onGround: Bool

    /// Callsign if known, otherwise the ICAO address as hex, e.g. "A1B2C3".
    var displayName: String {
        callsign ?? String(format: "%06X", id)
    }

    /// Every aircraft with a decoded position, nearest first.
    static func all(in snapshot: SDTrafficSnapshot?, from origin: CLLocationCoordinate2D) -> [NearbyAircraft] {
        guard let snapshot else { return [] }
        return snapshot.aircraft
            .compactMap { NearbyAircraft($0, from: origin) }
            .sorted { $0.distanceNm < $1.distanceNm }
    }

    /// The `limit` aircraft closest to `origin`. Aircraft without a decoded position are skipped.
    static func closest(in snapshot: SDTrafficSnapshot?, to origin: CLLocationCoordinate2D, limit: Int = 5) -> [NearbyAircraft] {
        Array(all(in: snapshot, from: origin).prefix(limit))
    }

    init?(_ aircraft: SDAircraft, from origin: CLLocationCoordinate2D) {
        guard aircraft.hasLatitudeDeg, aircraft.hasLongitudeDeg else { return nil }
        let position = CLLocationCoordinate2D(latitude: aircraft.latitudeDeg, longitude: aircraft.longitudeDeg)

        id = aircraft.icaoAddress
        callsign = aircraft.hasCallsign && !aircraft.callsign.isEmpty ? aircraft.callsign : nil
        squawk = aircraft.hasSquawk ? aircraft.squawk : nil
        coordinate = position
        distanceNm = Self.distanceNm(from: origin, to: position)
        bearingDeg = Self.bearingDeg(from: origin, to: position)
        altitudeFt = aircraft.hasAltitudeBaroFt ? aircraft.altitudeBaroFt
                   : aircraft.hasAltitudeGeomFt ? aircraft.altitudeGeomFt : nil
        groundSpeedKt = aircraft.hasGroundSpeedKt ? aircraft.groundSpeedKt : nil
        trackDeg = aircraft.hasTrackTrueDeg ? aircraft.trackTrueDeg : nil
        onGround = aircraft.onGround
    }

    // MARK: - Geodesy

    private static let metersPerNauticalMile = 1852.0

    static func distanceNm(from a: CLLocationCoordinate2D, to b: CLLocationCoordinate2D) -> Double {
        let meters = CLLocation(latitude: a.latitude, longitude: a.longitude)
            .distance(from: CLLocation(latitude: b.latitude, longitude: b.longitude))
        return meters / metersPerNauticalMile
    }

    /// Initial great-circle bearing, 0–360° true.
    static func bearingDeg(from a: CLLocationCoordinate2D, to b: CLLocationCoordinate2D) -> Double {
        let lat1 = a.latitude * .pi / 180, lat2 = b.latitude * .pi / 180
        let dLon = (b.longitude - a.longitude) * .pi / 180
        let y = sin(dLon) * cos(lat2)
        let x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(dLon)
        let degrees = atan2(y, x) * 180 / .pi
        return (degrees + 360).truncatingRemainder(dividingBy: 360)
    }
}
