import Foundation

/// How feet and inches are written when the units are imperial.
enum FootInchFormat: String, CaseIterable, Identifiable {
    /// 1' 2 1/2"
    case architectural
    /// 1.208 ft, 14.5 in
    case decimal

    var id: String { rawValue }
    var title: String { self == .architectural ? "Architectural (1' 2 1/2\")" : "Decimal" }
}

/// How angles are written.
enum DegreeFormat: String, CaseIterable, Identifiable {
    case decimal
    case degreesMinutesSeconds

    var id: String { rawValue }
    var title: String { self == .decimal ? "Decimal Degrees (12.5°)" : "Degrees, Minutes, Seconds (12° 30′ 0″)" }
}

/// App-wide number display preferences (View ▸ Foot & Inch Format, Degree Format).
enum DisplayFormat {
    private static let footInchKey = "footInchFormat", degreeKey = "degreeFormat"

    static var footInch: FootInchFormat {
        get { UserDefaults.standard.string(forKey: footInchKey).flatMap(FootInchFormat.init) ?? .decimal }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: footInchKey); changed() }
    }

    static var degrees: DegreeFormat {
        get { UserDefaults.standard.string(forKey: degreeKey).flatMap(DegreeFormat.init) ?? .decimal }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: degreeKey); changed() }
    }

    static let didChange = Notification.Name("DisplayFormatDidChange")
    private static func changed() { NotificationCenter.default.post(name: didChange, object: nil) }
}

/// A length given in millimetres, written in `unit` with its symbol.
func formatLength(_ mm: Double, _ unit: LengthUnit) -> String {
    if unit.imperial && DisplayFormat.footInch == .architectural {
        return architectural(inches: mm / 25.4)
    }
    return "\(formatted(mm / unit.scale)) \(unit.symbol)"
}

/// Feet and inches to the nearest sixteenth: 1' 2 1/2".
func architectural(inches value: Double) -> String {
    let sign = value < 0 ? "-" : ""
    var sixteenths = Int((abs(value) * 16).rounded())
    let feet = sixteenths / (12 * 16)
    sixteenths -= feet * 12 * 16
    let whole = sixteenths / 16
    var frac = sixteenths % 16, den = 16
    while frac > 0 && frac % 2 == 0 { frac /= 2; den /= 2 }
    let inch = frac > 0 ? (whole > 0 ? "\(whole) \(frac)/\(den)\"" : "\(frac)/\(den)\"") : "\(whole)\""
    return feet > 0 ? "\(sign)\(feet)' \(inch)" : "\(sign)\(inch)"
}

/// An angle in degrees, written in the chosen degree format.
func formatAngle(_ degrees: Double) -> String {
    guard DisplayFormat.degrees == .degreesMinutesSeconds else { return "\(formatted(degrees))°" }
    let sign = degrees < 0 ? "-" : ""
    var seconds = Int((abs(degrees) * 3600).rounded())
    let d = seconds / 3600
    seconds -= d * 3600
    return "\(sign)\(d)° \(seconds / 60)′ \(seconds % 60)″"
}

/// An area or volume in mm² / mm³, written in `unit`.
func formatArea(_ mm2: Double, _ unit: LengthUnit) -> String { "\(formatted(mm2 / pow(unit.scale, 2))) \(unit.symbol)²" }
func formatVolume(_ mm3: Double, _ unit: LengthUnit) -> String { "\(formatted(mm3 / pow(unit.scale, 3))) \(unit.symbol)³" }
