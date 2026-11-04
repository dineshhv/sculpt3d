import Foundation

/// Model Context Protocol endpoint for Sculpt3D: JSON-RPC 2.0 messages in,
/// responses out. The transport (HTTP on the Mac) hands each message here.
/// Tools act on the active open document, live, so changes show up in the
/// window as they happen.
@MainActor
final class MCPServer {
    static let shared = MCPServer()

    static let supportedVersions = ["2025-06-18", "2025-03-26", "2024-11-05"]
    let tools = MCPToolbox()

    private init() {}

    /// Handles one decoded JSON-RPC message or batch. Returns the response
    /// object (or array), or nil when nothing should be sent back.
    func handle(_ message: Any) async -> Any? {
        if let batch = message as? [Any] {
            var out: [Any] = []
            for m in batch {
                if let r = await handleOne(m) { out.append(r) }
            }
            return out.isEmpty ? nil : out
        }
        return await handleOne(message)
    }

    private func handleOne(_ message: Any) async -> Any? {
        guard let msg = message as? [String: Any], msg["jsonrpc"] as? String == "2.0" else {
            return Self.error(id: NSNull(), code: -32600, message: "Invalid request")
        }
        // Notifications and responses from the client need no reply.
        guard let method = msg["method"] as? String, let id = msg["id"] else { return nil }
        let params = msg["params"] as? [String: Any] ?? [:]

        switch method {
        case "initialize":
            let requested = params["protocolVersion"] as? String ?? ""
            let version = Self.supportedVersions.contains(requested) ? requested : Self.supportedVersions[0]
            return Self.result(id: id, [
                "protocolVersion": version,
                "capabilities": ["tools": ["listChanged": false]],
                "serverInfo": [
                    "name": "sculpt3d",
                    "title": "Sculpt3D",
                    "version": Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0",
                ],
                "instructions": Self.instructions,
            ])
        case "ping":
            return Self.result(id: id, [:])
        case "tools/list":
            return Self.result(id: id, ["tools": tools.definitions])
        case "tools/call":
            guard let name = params["name"] as? String else {
                return Self.error(id: id, code: -32602, message: "Missing tool name")
            }
            guard tools.has(name) else {
                return Self.error(id: id, code: -32602, message: "Unknown tool: \(name)")
            }
            let args = params["arguments"] as? [String: Any] ?? [:]
            do {
                let content = try await tools.call(name, args)
                return Self.result(id: id, ["content": content])
            } catch {
                // Tool failures go back to the model as results it can act on.
                let text = (error as? MCPToolError)?.message ?? error.localizedDescription
                return Self.result(id: id, ["content": [["type": "text", "text": text]], "isError": true])
            }
        default:
            return Self.error(id: id, code: -32601, message: "Method not found: \(method)")
        }
    }

    nonisolated static func result(id: Any, _ result: [String: Any]) -> [String: Any] {
        ["jsonrpc": "2.0", "id": id, "result": result]
    }

    nonisolated static func error(id: Any, code: Int, message: String) -> [String: Any] {
        ["jsonrpc": "2.0", "id": id, "error": ["code": code, "message": message]]
    }

    static let instructions = """
    Sculpt3D is a parametric B-rep CAD app (OpenCascade kernel). Tools act on the active open document and \
    show up live in its window. Units are millimetres and degrees; Z is up. Every modeling tool appends a \
    feature to the parametric history, so undo, redo, update_feature and delete_feature work on it.
    Body ids come from get_model and can change when earlier history changes, so read them again after edits. \
    Face and edge indices come from get_body_topology. Use screenshot to check the result visually.
    """
}

/// A failure reported back to the model as a tool result.
struct MCPToolError: Error {
    let message: String
    init(_ message: String) { self.message = message }
}

/// The documents open in the app, most recently active first.
@MainActor
final class DocumentRegistry {
    static let shared = DocumentRegistry()

    final class Entry {
        let id = UUID()
        weak var store: ModelStore?
        weak var viewport: ViewportController?
        var name: String

        init(store: ModelStore, viewport: ViewportController, name: String) {
            self.store = store
            self.viewport = viewport
            self.name = name
        }
    }

    private var entries: [Entry] = []
    /// Bumped on every registration, so callers can wait for a new document.
    private(set) var registrations = 0

    var open: [Entry] {
        entries.removeAll { $0.store == nil }
        return entries
    }

    var active: Entry? { open.first }

    func register(_ store: ModelStore, _ viewport: ViewportController, name: String) {
        if let e = entries.first(where: { $0.store === store }) {
            e.name = name
            activate(store)
            return
        }
        entries.insert(Entry(store: store, viewport: viewport, name: name), at: 0)
        registrations += 1
    }

    func activate(_ store: ModelStore) {
        guard let i = entries.firstIndex(where: { $0.store === store }) else { return }
        entries.insert(entries.remove(at: i), at: 0)
    }

    func activate(id: UUID) -> Bool {
        guard let i = open.firstIndex(where: { $0.id == id }) else { return false }
        entries.insert(entries.remove(at: i), at: 0)
        return true
    }

    func unregister(_ store: ModelStore) {
        entries.removeAll { $0.store == nil || $0.store === store }
    }
}
