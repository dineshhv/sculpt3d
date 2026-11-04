import SwiftUI

@main
struct Sculpt3DApp: App {
    #if os(macOS)
    init() {
        // The MCP server lets AI assistants model in the open documents (localhost only).
        if MCPSettings.enabled { MCPHTTPTransport.shared.start() }
    }
    #endif

    var body: some Scene {
        #if os(macOS)
        // First page: shown at launch instead of an untitled document.
        Window("Welcome to Sculpt3D", id: WelcomeWindow.id) {
            WelcomeView()
        }
        .windowStyle(.hiddenTitleBar)
        .windowResizability(.contentSize)
        .defaultLaunchBehavior(.presented)
        .restorationBehavior(.disabled)
        #else
        // First page: the logo above the document browser, with New Document.
        DocumentGroupLaunchScene {
            NewDocumentButton("New Document")
        } background: {
            BrandBackground()
        } overlayAccessoryView: { geometry in
            // The system draws the "Sculpt3D" title; the mark sits above it.
            Image("BrandMark")
                .resizable()
                .scaledToFit()
                .frame(width: 128, height: 128)
                .shadow(color: Color(red: 0.04, green: 0.1, blue: 0.36).opacity(0.22), radius: 14, y: 8)
                .position(x: geometry.titleViewFrame.midX, y: geometry.titleViewFrame.midY - 200)
        }
        #endif

        DocumentGroup(newDocument: Sculpt3DFile()) { file in
            DocumentView(document: file.$document, name: file.fileURL?.lastPathComponent ?? "Untitled", url: file.fileURL)
        }
        .commands {
            ModelCommands()
            #if os(macOS)
            CommandGroup(after: .appSettings) { MCPMenu() }
            #endif
        }
        #if os(macOS)
        .defaultSize(width: 1280, height: 820)
        #endif
    }
}

enum WelcomeWindow {
    static let id = "welcome"
}

/// The model store and viewport of one document window.
@MainActor
final class DocumentSession: ObservableObject {
    let store: ModelStore
    let viewport: ViewportController

    init(_ file: Sculpt3DFile) {
        store = ModelStore(features: file.features, settings: file.settings)
        viewport = ViewportController(store: store)
    }
}

/// Bridges the SwiftUI document binding to the model store.
struct DocumentView: View {
    @Binding var document: Sculpt3DFile
    var name = "Untitled"
    var url: URL?
    /// Built once per window. SwiftUI re-creates this view whenever the
    /// document changes, so the store must not be built eagerly in `init`
    /// (that replayed and re-meshed the whole model on every edit).
    @StateObject private var session: DocumentSession
    private var store: ModelStore { session.store }
    private var viewport: ViewportController { session.viewport }
    #if os(macOS)
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow
    #endif

    #if os(macOS)
    @Environment(\.controlActiveState) private var activeState
    #endif

    init(document: Binding<Sculpt3DFile>, name: String = "Untitled", url: URL? = nil) {
        _document = document
        self.name = name
        self.url = url
        let file = document.wrappedValue
        _session = StateObject(wrappedValue: DocumentSession(file))
    }

    var body: some View {
        ContentView(store: store, viewport: viewport)
            .focusedSceneObject(store)
            .focusedSceneObject(viewport)
            .onAppear {
                store.onFeaturesChanged = { features in document.features = features }
                store.onSettingsChanged = { settings in document.settings = settings }
                DocumentRegistry.shared.register(store, viewport, name: name)
                store.documentName = (name as NSString).deletingPathExtension
                store.documentURL = url
                #if os(macOS)
                dismissWindow(id: WelcomeWindow.id)
                #endif
            }
            #if os(macOS)
            .onChange(of: name) {
                DocumentRegistry.shared.register(store, viewport, name: name)
                store.documentName = (name as NSString).deletingPathExtension
            }
            .onChange(of: url) { store.documentURL = url }
            .onChange(of: activeState) { if activeState == .key { DocumentRegistry.shared.activate(store) } }
            .onDisappear {
                DocumentRegistry.shared.unregister(store)
                // Closing the last document brings the first page back.
                let openWindow = openWindow
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                    if NSDocumentController.shared.documents.isEmpty { openWindow(id: WelcomeWindow.id) }
                }
            }
            #endif
    }
}
