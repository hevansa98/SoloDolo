//
//  TrafficMap.swift
//  SoloDolo
//
//  Author: photon
//
//  Dashboard map: ownship (blue = current GPS fix, orange = last known
//  position; hidden until Dolo reports one) and every ADS-B aircraft with a
//  position, drawn pointing along its track.
//

import SwiftUI
import MapKit

struct TrafficMap: View {
    let aircraft: [NearbyAircraft]
    let ownship: Ownship?
    @State private var camera: MapCameraPosition

    init(aircraft: [NearbyAircraft], ownship: Ownship?) {
        self.aircraft = aircraft
        self.ownship = ownship
        _camera = State(initialValue: Self.region(around: ownship?.coordinate ?? Ownship.defaultMapCenter))
    }

    /// Roughly 30 nm across, so local traffic around ownship is visible at a glance.
    private static let spanMeters = 55_000.0

    private static func region(around center: CLLocationCoordinate2D) -> MapCameraPosition {
        .region(MKCoordinateRegion(center: center, latitudinalMeters: spanMeters, longitudinalMeters: spanMeters))
    }

    var body: some View {
        Map(position: $camera) {
            if let ownship {
                Annotation(ownship.isCurrent ? "Ownship" : "Ownship (last known)", coordinate: ownship.coordinate) {
                    Circle()
                        .fill(ownship.isCurrent ? .blue : .orange)
                        .stroke(.white, lineWidth: 2)
                        .frame(width: 14, height: 14)
                        .shadow(radius: 1)
                }
            }

            ForEach(aircraft) { plane in
                Annotation(plane.displayName, coordinate: plane.coordinate) {
                    AircraftSymbol(trackDeg: plane.trackDeg, onGround: plane.onGround)
                }
            }
        }
        .mapStyle(.standard(pointsOfInterest: .excludingAll))
        // Jump to ownship when the first position arrives. Later fixes don't move the
        // camera, so panning around isn't undone every second.
        .onChange(of: ownship == nil) {
            guard let ownship else { return }
            withAnimation { camera = Self.region(around: ownship.coordinate) }
        }
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
    TrafficMap(aircraft: NearbyAircraft.all(in: .previewSample, from: Ownship.defaultMapCenter),
               ownship: Ownship(gps: GPSStatus(), lastKnown: Ownship.defaultMapCenter))
        .frame(width: 400, height: 400)
}
