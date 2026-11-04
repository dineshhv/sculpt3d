import Foundation

/// Single-key shortcuts, delivered only while the viewport has keyboard focus
/// so they never fire while typing in a field.
extension ViewportController {
    /// Returns true when the key was used.
    func handleKey(_ characters: String) -> Bool {
        guard let ch = characters.lowercased().first else { return false }
        if ch == "\u{1b}", pointPick != nil {
            cancelPointPick()
            return true
        }
        if store.sketch != nil {
            if let tool = SketchTool.allCases.first(where: { $0.key == ch }) {
                store.setSketchTool(tool)
                return true
            }
            if ch == "\u{7f}" || ch == "\u{8}" {
                store.deleteSketchSelection()
                return true
            }
            return false
        }
        guard let id = ToolCatalog.keyShortcuts[ch], let ui,
              let command = ToolCatalog.all.first(where: { $0.id == id }), command.isEnabled(store) else { return false }
        command.run(store, self, ui)
        return true
    }
}
