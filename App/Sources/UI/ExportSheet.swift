import SwiftUI
import UniformTypeIdentifiers
#if os(macOS)
import AppKit
#endif

/// File ▸ Export… (every format, with favourites for Quick Export) and
/// File ▸ Export to Slicer… (open the model straight in a slicer).
struct ExportSheet: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var ui: UIState
    @State private var favorites = Set(QuickExport.formats)
    @State private var folder = QuickExport.folder
    @State private var slicerFormat = ExportKind.threeMF
    @State private var slicers = Slicer.installed
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text(ui.exportToSlicer ? "Export to Slicer" : "Export").font(.headline)
                Spacer()
                Text(scope).font(.callout).foregroundStyle(.secondary)
            }
            if ui.exportToSlicer { slicerList } else { formatList }
            HStack {
                if !ui.exportToSlicer {
                    Text("★ marks formats for Quick Export to Favorite Formats.").font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.cancelAction)
            }
        }
        .padding(20)
        .frame(width: 560, height: ui.exportToSlicer ? 420 : 640)
    }

    private var scope: String {
        let n = store.exportBodies.count
        return store.touchedBodies.isEmpty ? "All \(n) visible bodies" : "\(n) selected \(n == 1 ? "body" : "bodies")"
    }

    // MARK: Formats

    private var formatList: some View {
        VStack(alignment: .leading, spacing: 10) {
            List {
                ForEach(ExportKind.menu) { kind in
                    HStack(spacing: 10) {
                        Button { toggleFavorite(kind) } label: {
                            Image(systemName: favorites.contains(kind) ? "star.fill" : "star")
                                .foregroundStyle(favorites.contains(kind) ? Color.orange : Color.secondary)
                        }
                        .buttonStyle(.plain)
                        .disabled(!kind.available)
                        VStack(alignment: .leading, spacing: 1) {
                            Text("\(kind.title) (.\(kind.fileExtension))").font(.body.weight(.medium))
                            Text(kind.detail).font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        Button("Export…") {
                            ui.export(kind, from: store)
                            if ui.exportingAny { dismiss() }
                        }
                        .disabled(!kind.available)
                    }
                    .opacity(kind.available ? 1 : 0.5)
                    .padding(.vertical, 2)
                }
            }
            .listStyle(.inset)
            GroupBox("Quick Export") {
                HStack {
                    Image(systemName: "folder")
                    Text(folder.path).lineLimit(1).truncationMode(.middle).font(.callout)
                    Spacer()
                    #if os(macOS)
                    Button("Choose…") { chooseFolder() }
                    #endif
                    Button("Export Favorites Now") {
                        quickExport(store: store)
                        dismiss()
                    }
                    .disabled(favorites.isEmpty)
                }
            }
        }
    }

    private func toggleFavorite(_ kind: ExportKind) {
        if favorites.contains(kind) { favorites.remove(kind) } else { favorites.insert(kind) }
        QuickExport.formats = ExportKind.menu.filter(favorites.contains)
    }

    #if os(macOS)
    private func chooseFolder() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.canCreateDirectories = true
        panel.directoryURL = folder
        panel.prompt = "Use Folder"
        if panel.runModal() == .OK, let url = panel.url {
            folder = url
            QuickExport.folder = url
        }
    }
    #endif

    // MARK: Slicers

    private var slicerList: some View {
        VStack(alignment: .leading, spacing: 12) {
            Picker("Format", selection: $slicerFormat) {
                Text("3MF (keeps colours and separate parts)").tag(ExportKind.threeMF)
                Text("STL").tag(ExportKind.stl)
                Text("OBJ").tag(ExportKind.obj)
            }
            #if os(macOS)
            .pickerStyle(.radioGroup)
            #endif
            if slicers.isEmpty {
                Text("No slicer found in Applications. Choose one with Other App…, or install Bambu Studio, OrcaSlicer, PrusaSlicer or Cura.")
                    .font(.callout).foregroundStyle(.secondary)
            }
            List(slicers) { slicer in
                HStack {
                    #if os(macOS)
                    Image(nsImage: NSWorkspace.shared.icon(forFile: slicer.url.path)).resizable().frame(width: 28, height: 28)
                    #endif
                    Text(slicer.name)
                    Spacer()
                    Button("Open in \(slicer.name)") {
                        open(in: slicer.url)
                        dismiss()
                    }
                }
            }
            .listStyle(.inset)
            #if os(macOS)
            Button("Other App…") {
                let panel = NSOpenPanel()
                panel.allowedContentTypes = [.application]
                panel.directoryURL = URL(fileURLWithPath: "/Applications")
                panel.prompt = "Open Model"
                if panel.runModal() == .OK, let url = panel.url {
                    open(in: url)
                    dismiss()
                }
            }
            #endif
        }
    }

    private func open(in app: URL) {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("Sculpt3D Slicer", isDirectory: true)
        guard let file = store.write(slicerFormat, to: dir) else { return }
        #if os(macOS)
        NSWorkspace.shared.open([file], withApplicationAt: app, configuration: NSWorkspace.OpenConfiguration()) { _, error in
            if let error { Task { @MainActor in store.toast = "Couldn't open the slicer: \(error.localizedDescription)" } }
        }
        #endif
    }
}

/// Writes every favourite format to the Quick Export folder.
@MainActor
func quickExport(store: ModelStore) {
    let kinds = QuickExport.formats.filter(\.available)
    guard !kinds.isEmpty else { return store.toast = "Pick favourite formats in File ▸ Export… first" }
    let written = kinds.compactMap { store.write($0, to: QuickExport.folder) }
    if !written.isEmpty {
        store.toast = "Exported \(written.map(\.pathExtension).joined(separator: ", ")) to \(QuickExport.folder.lastPathComponent)"
        #if os(macOS)
        NSWorkspace.shared.activateFileViewerSelecting(written)
        #endif
    }
}
