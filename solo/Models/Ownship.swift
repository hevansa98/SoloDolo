//
//  Ownship.swift
//  SoloDolo
//
//  Author: photon
//
//  Where "we" are: Dolo's GPS position when it has a 2D/3D fix, otherwise
//  an assumed location (Lewisville City Hall).
//

import CoreLocation

struct Ownship {
    /// Lewisville City Hall, 151 W Church St, Lewisville, TX. Used until Dolo reports a fix.
    static let assumedLocation = CLLocationCoordinate2D(latitude: 33.0462, longitude: -96.9942)

    let coordinate: CLLocationCoordinate2D
    /// `true` when there's no GPS fix and `coordinate` is the assumed location.
    let isAssumed: Bool

    init(gps: GPSStatus) {
        if let fix = gps.coordinate {
            coordinate = fix
            isAssumed = false
        } else {
            coordinate = Self.assumedLocation
            isAssumed = true
        }
    }
}
