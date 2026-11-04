#if os(macOS)
import AppKit
import SwiftUI

enum MCPSettings {
    static let key = "mcpServerEnabled"

    /// On unless the user turned it off.
    static var enabled: Bool {
        get { UserDefaults.standard.object(forKey: key) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: key) }
    }
}

/// App-menu entries: turn the MCP server on or off, and copy the command that
/// adds it to Claude Code.
struct MCPMenu: View {
    @State private var enabled = MCPSettings.enabled
    @State private var state: MCPHTTPTransport.State = .stopped

    var body: some View {
        Toggle("MCP Server", isOn: $enabled)
            .onChange(of: enabled) {
                MCPSettings.enabled = enabled
                if enabled { MCPHTTPTransport.shared.start() } else { MCPHTTPTransport.shared.stop() }
            }
        Text(status)
        Button("Copy Claude Code Setup Command") {
            NSPasteboard.general.clearContents()
            NSPasteboard.general.setString("claude mcp add --transport http sculpt3d \(MCPHTTPTransport.shared.endpoint)", forType: .string)
        }
        .disabled(!enabled)
        Divider()
            .onAppear { state = MCPHTTPTransport.shared.state }
            .onReceive(NotificationCenter.default.publisher(for: MCPHTTPTransport.stateChanged)) { _ in
                state = MCPHTTPTransport.shared.state
            }
    }

    private var status: String {
        switch state {
        case let .running(port): return "Listening on 127.0.0.1:\(port)"
        case let .failed(message): return "Not running: \(message)"
        case .stopped: return "Off"
        }
    }
}
#endif
