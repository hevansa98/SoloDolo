//
//  Ownship.swift
//  SoloDolo
//
//  Author: photon
//
//  Where "we" are: Dolo's current GPS position, or the last known one after
//  the fix is lost. There is no ownship until Dolo has reported a position.
//

import CoreLocation

struct Ownship {
    /// Map center before any position is known: Lewisville City Hall, 151 W Church St, Lewisville, TX.
    static let defaultMapCenter = CLLocationCoordinate2D(latitude: 33.0462, longitude: -96.9942)

    let coordinate: CLLocationCoordinate2D
    /// `false` when the fix is lost and `coordinate` is the last known position.
    let isCurrent: Bool

    /// The current fix if there is one, else the last known position, else `nil`.
    init?(gps: GPSStatus, lastKnown: CLLocationCoordinate2D?) {
        if let fix = gps.coordinate {
            coordinate = fix
            isCurrent = true
        } else if let lastKnown {
            coordinate = lastKnown
            isCurrent = false
        } else {
            return nil
        }
    }
}
