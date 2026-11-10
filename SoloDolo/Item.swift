//
//  Item.swift
//  SoloDolo
//
//  Created by odin on 11/9/26.
//

import Foundation
import SwiftData

@Model
final class Item {
    var timestamp: Date
    
    init(timestamp: Date) {
        self.timestamp = timestamp
    }
}
