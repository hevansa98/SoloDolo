//
//  ValidatedFieldStyle.swift
//  SoloDolo
//

import SwiftUI

extension View {
    /// Rounded text field border that turns red when `isInvalid` is true.
    func validatedFieldStyle(isInvalid: Bool) -> some View {
        let shape = RoundedRectangle(cornerRadius: 5)
        return self
            .padding(8)
            .background(isInvalid ? Color.red.opacity(0.2) : Color(.systemBackground), in: shape)
            .overlay(shape.stroke(isInvalid ? Color.red : Color.gray.opacity(0.5), lineWidth: 1))
    }
}
