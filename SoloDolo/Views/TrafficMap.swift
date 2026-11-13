//
//  TrafficMap.swift
//  SoloDolo
//
//  Author: photon
//
//  Dashboard map: ownship at the center and every ADS-B aircraft with a
//  position, drawn as a plane pointing along its track.
//

import SwiftUI
import MapKit

struct TrafficMap: View {
    let aircraft: [NearbyAircraft]
    let ownship: CLLocationCoordinate2D

    /// Roughly 30 nm across, so local traffic around ownship is visible at a glance.
    private static let initialSpanMeters = 55_000.0

    var body: some View {
        Map(initialPosition: .region(MKCoordinateRegion(center: ownship,
                                                        latitudinalMeters: Self.initialSpanMeters,
                                                        longitudinalMeters: Self.initialSpanMeters))) {
            Annotation("Ownship", coordinate: ownship) {
                Circle()
                    .fill(.blue)
                    .stroke(.white, lineWidth: 2)
                    .frame(width: 14, height: 14)
                    .shadow(radius: 1)
            }

            ForEach(aircraft) { plane in
                Annotation(plane.displayName, coordinate: plane.coordinate) {
                    AircraftSymbol(trackDeg: plane.trackDeg, onGround: plane.onGround)
                }
            }
        }
        .mapStyle(.standard(pointsOfInterest: .excludingAll))
    }
}

/// Plane glyph rotated to the aircraft's track. Gray when on the ground.
private struct AircraftSymbol: View {
    let trackDeg: Float?
    let onGround: Bool

    var body: some View {
        Image(systemName: "airplane")
            .font(.system(size: 18, weight: .semibold))
            .foregroundStyle(onGround ? .gray : .orange)
            // The SF Symbol points east (090°), so subtract 90 to align it with true track
            .rotationEffect(.degrees(Double(trackDeg ?? 90) - 90))
            .shadow(color: .black.opacity(0.4), radius: 1)
    }
}

#Preview {
    TrafficMap(aircraft: NearbyAircraft.all(in: .previewSample, from: Ownship.assumedLocation),
               ownship: Ownship.assumedLocation)
        .frame(width: 400, height: 400)
}
