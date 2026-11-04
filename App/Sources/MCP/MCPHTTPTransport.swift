import Foundation
import Network

/// MCP "Streamable HTTP" transport on 127.0.0.1. Each POST to /mcp carries
/// one JSON-RPC message (or batch) and gets a plain JSON response; the server
/// never streams, so GET returns 405 as the spec allows.
/// Only loopback connections are accepted, and browser requests from other
/// origins are refused (DNS-rebinding protection).
final class MCPHTTPTransport {
    static let shared = MCPHTTPTransport()
    static let defaultPort: UInt16 = 7317

    enum State: Equatable {
        case stopped
        case running(port: UInt16)
        case failed(String)
    }

    private var listener: NWListener?
    private let queue = DispatchQueue(label: "com.higglerslab.sculpt3d.mcp")
    /// Largest request body accepted (a model's JSON never comes near this).
    private let maxBody = 8 << 20

    @MainActor private(set) var state: State = .stopped {
        didSet { NotificationCenter.default.post(name: Self.stateChanged, object: nil) }
    }

    static let stateChanged = Notification.Name("MCPHTTPTransportStateChanged")

    var endpoint: String { "http://127.0.0.1:\(Self.defaultPort)/mcp" }

    @MainActor
    func start(port: UInt16 = defaultPort) {
        guard listener == nil else { return }
        do {
            let params = NWParameters.tcp
            params.requiredLocalEndpoint = .hostPort(host: "127.0.0.1", port: NWEndpoint.Port(rawValue: port)!)
            params.allowLocalEndpointReuse = true
            let l = try NWListener(using: params)
            l.newConnectionHandler = { [weak self] c in self?.accept(c) }
            l.stateUpdateHandler = { [weak self] s in
                Task { @MainActor in
                    switch s {
                    case .ready: self?.state = .running(port: port)
                    case let .failed(e): self?.state = .failed(e.localizedDescription); self?.stop()
                    default: break
                    }
                }
            }
            listener = l
            l.start(queue: queue)
        } catch {
            state = .failed(error.localizedDescription)
        }
    }

    @MainActor
    func stop() {
        listener?.cancel()
        listener = nil
        if case .failed = state { return }
        state = .stopped
    }

    // MARK: - Connections

    private func accept(_ c: NWConnection) {
        c.start(queue: queue)
        receive(c, buffer: Data())
    }

    /// Reads until a whole request (headers plus Content-Length body) is in.
    private func receive(_ c: NWConnection, buffer: Data) {
        c.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, done, error in
            guard let self else { return }
            var buffer = buffer
            if let data { buffer.append(data) }
            if let request = HTTPRequest(buffer) {
                if request.body.count < request.contentLength {
                    if done || error != nil || request.contentLength > self.maxBody { return c.cancel() }
                    return self.receive(c, buffer: buffer)
                }
                self.respond(to: request, on: c)
            } else if done || error != nil || buffer.count > self.maxBody {
                c.cancel()
            } else {
                self.receive(c, buffer: buffer)
            }
        }
    }

    private func respond(to req: HTTPRequest, on c: NWConnection) {
        if let origin = req.headers["origin"], !Self.isLocal(origin: origin) {
            return send(c, status: 403, json: MCPServer.errorBody("Origin not allowed"))
        }
        guard req.path == "/mcp" || req.path == "/" else { return send(c, status: 404, json: nil) }
        switch req.method {
        case "POST":
            guard let message = try? JSONSerialization.jsonObject(with: req.body.prefix(req.contentLength)) else {
                return send(c, status: 400, json: MCPServer.error(id: NSNull(), code: -32700, message: "Parse error"))
            }
            Task { @MainActor in
                let response = await MCPServer.shared.handle(message)
                self.queue.async {
                    // Notifications and client responses are acknowledged without a body.
                    if let response { self.send(c, status: 200, json: response) } else { self.send(c, status: 202, json: nil) }
                }
            }
        case "DELETE":
            send(c, status: 200, json: nil)
        default:
            send(c, status: 405, json: nil, extra: ["Allow": "POST, DELETE"])
        }
    }

    private func send(_ c: NWConnection, status: Int, json: Any?, extra: [String: String] = [:]) {
        let body = json.flatMap { try? JSONSerialization.data(withJSONObject: $0, options: [.withoutEscapingSlashes]) } ?? Data()
        var head = "HTTP/1.1 \(status) \(Self.reason(status))\r\n"
        if json != nil { head += "Content-Type: application/json\r\n" }
        head += "Content-Length: \(body.count)\r\nConnection: close\r\n"
        for (k, v) in extra { head += "\(k): \(v)\r\n" }
        head += "\r\n"
        c.send(content: Data(head.utf8) + body, completion: .contentProcessed { _ in c.cancel() })
    }

    private static func isLocal(origin: String) -> Bool {
        guard let host = URL(string: origin)?.host?.lowercased() else { return false }
        return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]"
    }

    private static func reason(_ status: Int) -> String {
        [200: "OK", 202: "Accepted", 400: "Bad Request", 403: "Forbidden", 404: "Not Found", 405: "Method Not Allowed"][status] ?? "Error"
    }
}

/// The parts of an HTTP/1.1 request the transport needs.
private struct HTTPRequest {
    let method: String
    let path: String
    let headers: [String: String]
    let contentLength: Int
    let body: Data

    /// Nil until the header block is complete.
    init?(_ data: Data) {
        guard let end = data.range(of: Data("\r\n\r\n".utf8)) else { return nil }
        let lines = String(decoding: data[..<end.lowerBound], as: UTF8.self).components(separatedBy: "\r\n")
        let parts = lines.first?.split(separator: " ") ?? []
        guard parts.count >= 2 else { return nil }
        method = String(parts[0]).uppercased()
        path = String(parts[1].split(separator: "?").first ?? "")
        var h: [String: String] = [:]
        for line in lines.dropFirst() {
            guard let colon = line.firstIndex(of: ":") else { continue }
            h[line[..<colon].trimmingCharacters(in: .whitespaces).lowercased()] = line[line.index(after: colon)...].trimmingCharacters(in: .whitespaces)
        }
        headers = h
        contentLength = Int(h["content-length"] ?? "") ?? 0
        body = Data(data[end.upperBound...])
    }
}

extension MCPServer {
    nonisolated static func errorBody(_ message: String) -> [String: Any] {
        ["jsonrpc": "2.0", "id": NSNull(), "error": ["code": -32000, "message": message]]
    }
}
