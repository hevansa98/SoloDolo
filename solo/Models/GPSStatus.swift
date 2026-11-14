//
//  GPSStatus.swift
//  SoloDolo
//
//  Author: photon
//
//  Ownship GPS health reported by the bridge (fix mode, satellites, HDOP,
//  PPS lock) and the thresholds that grade each reading.
//

import CoreLocation

/// How healthy a single GPS reading is. Views map this to a color.
enum SignalQuality {
    case unknown    // no data from the bridge yet
    case good
    case degraded
    case bad
}

/// Latest GPS health from the bridge. Every field is `nil` until data arrives.
struct GPSStatus: Equatable {
    enum FixMode {
        case noFix, twoD, threeD
    }

    var fixMode: FixMode?          // GnssFix.fix_mode
    var satellitesUsed: Int?       // GnssFix.satellites_used
    var satellitesVisible: Int?    // GnssFix.satellites_visible
    var hdop: Double?              // GnssFix.hdop
    var latitudeDeg: Double?       // GnssFix.latitude_deg, only with a 2D/3D fix
    var longitudeDeg: Double?      // GnssFix.longitude_deg, only with a 2D/3D fix
    var ppsLocked: Bool?           // ServerHello.pps_locked

    /// Ownship position from the GPS fix, or `nil` without a 2D/3D fix.
    var coordinate: CLLocationCoordinate2D? {
        guard let latitudeDeg, let longitudeDeg else { return nil }
        return CLLocationCoordinate2D(latitude: latitudeDeg, longitude: longitudeDeg)
    }

    var fixQuality: SignalQuality {
        switch fixMode {
        case .threeD: .good
        case .twoD: .degraded       // no altitude
        case .noFix: .bad
        case nil: .unknown
        }
    }

    /// 4 satellites is the minimum for a 3D fix; 6+ leaves margin for dropouts.
    var satelliteQuality: SignalQuality {
        guard let satellitesUsed else { return .unknown }
        switch satellitesUsed {
        case 6...: return .good
        case 4...5: return .degraded
        default: return .bad
        }
    }

    /// HDOP ≤ 2 is good, up to 5 is usable, above that the position is unreliable.
    var hdopQuality: SignalQuality {
        guard let hdop else { return .unknown }
        switch hdop {
        case ...2: return .good
        case ...5: return .degraded
        default: return .bad
        }
    }

    /// An unlocked PPS only affects timing precision, not position, so it's degraded rather than bad.
    var ppsQuality: SignalQuality {
        guard let ppsLocked else { return .unknown }
        return ppsLocked ? .good : .degraded
    }
}
