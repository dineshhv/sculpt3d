import SwiftUI
import ImageIO
import UniformTypeIdentifiers

extension UTType {
    static let sculpt3DModel = UTType(exportedAs: "com.higglerslab.sculpt3d.model")
    /// Documents saved under the app's earlier name, Open3D.
    static let legacyOpen3DModel = UTType(importedAs: "com.higglerslab.open3d.model")

    static let cadExchange: [UTType] = ["step", "stp", "iges", "igs", "stl", "brep"]
        .compactMap { UTType(filenameExtension: $0) }
}

/// The native document: the feature history, stored as JSON.
struct Sculpt3DFile: FileDocument {
    static var readableContentTypes: [UTType] { [.sculpt3DModel, .legacyOpen3DModel] }
    static var writableContentTypes: [UTType] { [.sculpt3DModel] }

    var features: [Feature] = []
    var settings = ProjectSettings()

    init(features: [Feature] = [], settings: ProjectSettings = ProjectSettings()) {
        self.features = features
        self.settings = settings
    }

    init(configuration: ReadConfiguration) throws {
        guard let data = configuration.file.regularFileContents else { throw CocoaError(.fileReadCorruptFile) }
        try self.init(data: data)
    }

    init(data: Data) throws {
        let payload = try JSONDecoder().decode(Payload.self, from: data)
        features = payload.features
        settings = payload.settings ?? ProjectSettings()
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: try encoded())
    }

    func encoded() throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try encoder.encode(Payload(features: features, settings: settings))
    }

    private struct Payload: Codable {
        var version = 2
        var features: [Feature]
        var settings: ProjectSettings?
    }
}

/// Bytes produced by an exporter, handed to SwiftUI's file exporter.
struct ExchangeFile: FileDocument {
    static var readableContentTypes: [UTType] { UTType.cadExchange }
    var data: Data

    init(data: Data) { self.data = data }

    init(configuration: ReadConfiguration) throws {
        data = configuration.file.regularFileContents ?? Data()
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }
}

enum ExchangeFormat: String, CaseIterable, Identifiable {
    case step, iges, stl, brep

    var id: String { rawValue }
    var title: String { rawValue.uppercased() }
    var contentType: UTType { UTType(filenameExtension: rawValue) ?? .data }
}

/// A PNG image handed to SwiftUI's file exporter (screenshots).
struct PNGFile: FileDocument {
    static var readableContentTypes: [UTType] { [.png] }
    var data: Data

    init(data: Data) { self.data = data }

    init(configuration: ReadConfiguration) throws {
        data = configuration.file.regularFileContents ?? Data()
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
        FileWrapper(regularFileWithContents: data)
    }

    static func encode(_ image: CGImage) -> Data? {
        let data = NSMutableData()
        guard let dest = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(dest, image, nil)
        return CGImageDestinationFinalize(dest) ? data as Data : nil
    }
}
