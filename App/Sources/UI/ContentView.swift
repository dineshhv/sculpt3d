import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @StateObject private var ui = UIState()
    @State private var width: CGFloat = 0
    /// Where the rail's tool icons are, so the open flyout sits beside them.
    @State private var railMenu: CGRect = .zero
    @Environment(\.horizontalSizeClass) private var sizeClass

    private var compact: Bool {
        #if os(iOS)
        return sizeClass == .compact
        #else
        return false
        #endif
    }

    var body: some View {
        ZStack {
            ViewportView(controller: viewport)
                .ignoresSafeArea()
            if store.sketch != nil {
                SketchAnnotationsView(store: store, viewport: viewport)
                    .ignoresSafeArea()
            }
            if viewport.showPinnedMeasurements && !viewport.pointMeasurements.isEmpty {
                MeasurementLabels(viewport: viewport)
                    .ignoresSafeArea()
            }
            overlays
            if ui.showSearch {
                Color.black.opacity(0.08)
                    .ignoresSafeArea()
                    .onTapGesture { ui.showSearch = false }
                VStack {
                    SearchPalette(store: store, viewport: viewport, ui: ui)
                        .padding(.top, 80)
                    Spacer()
                }
            }
        }
        .background(GeometryReader { g in Color.clear.onAppear { width = g.size.width }.onChange(of: g.size.width) { _, w in width = w } })
        .environment(\.lengthUnit, store.settings.units)
        .focusedSceneObject(ui)
        .onAppear { viewport.ui = ui }
        // The viewport only redraws on model changes; mode changes alter what it draws too.
        .onChange(of: ui.visualizing) { _, _ in viewport.redraw() }
        .onChange(of: store.sketch != nil) { _, inSketch in
            if inSketch { viewport.faceSketchPlane() }
        }
        .fileImporter(isPresented: $ui.importing, allowedContentTypes: UTType.cadExchange, allowsMultipleSelection: true) { result in
            if case let .success(urls) = result { urls.forEach(store.importFile) }
            viewport.fitAll()
        }
        .fileExporter(isPresented: $ui.exporting, document: ui.exportFile, contentType: ui.exportFormat.contentType,
                      defaultFilename: "Model.\(ui.exportFormat.rawValue)") { _ in }
        .fileImporter(isPresented: $ui.importingImage, allowedContentTypes: [.image]) { result in
            if case let .success(url) = result { store.insertImage(url) }
        }
        .fileImporter(isPresented: $ui.importingProject, allowedContentTypes: [.sculpt3DModel, .legacyOpen3DModel]) { result in
            if case let .success(url) = result { store.insertProject(url) }
        }
        .sheet(isPresented: $ui.showVariables) { VariablesPanel(store: store) }
        .fileExporter(isPresented: $ui.savingScreenshot, document: ui.screenshot, contentType: .png,
                      defaultFilename: "Sculpt3D Screenshot.png") { _ in }
        .sheet(isPresented: compactBinding(\.showItems)) { sheet("Items") { ItemsPanel(store: store) } }
        .modifier(MenuSheets(store: store, viewport: viewport, ui: ui))
        .sheet(isPresented: compactBinding(\.showHistory)) { sheet("History") { HistoryPanel(store: store) } }
    }

    private var overlays: some View {
        HStack(alignment: .top, spacing: 10) {
            ZStack(alignment: .topLeading) {
                ToolRail(store: store, viewport: viewport, ui: ui, compact: compact)
                    .frame(maxHeight: .infinity, alignment: .top)
                // The open group's tools sit right beside the rail's icons, level
                // with them, drawn over the rail's wider label column.
                if let menu = ui.openMenu, store.sketch == nil, !viewport.measuring, !ui.visualizing {
                    TopClampedLayout(top: railMenu.minY) {
                        ToolFlyout(category: menu, store: store, viewport: viewport, ui: ui, compact: compact)
                    }
                    .frame(maxHeight: .infinity)
                    .padding(.leading, railMenu.maxX + 8)
                    .transition(.move(edge: .leading).combined(with: .opacity))
                }
            }
            if ui.showItems && !compact && ui.openMenu == nil && store.sketch == nil {
                ItemsPanel(store: store)
                    .frame(width: 260)
                    .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            }
            VStack(spacing: 10) {
                if let session = viewport.pointPick {
                    PointPickBanner(session: session) { viewport.cancelPointPick() }
                }
                if viewport.showPinnedMeasurements && !viewport.measuring && !viewport.pinnedMeasurements.isEmpty {
                    PinnedMeasurementsCard(viewport: viewport)
                }
                if viewport.measuring {
                    Text("Select objects to list available measurements.")
                        .font(.callout)
                        .padding(.horizontal, 14)
                        .padding(.vertical, 8)
                        .background(.thickMaterial, in: Capsule())
                }
                if let toast = store.toast {
                    ToastView(text: toast) { store.toast = nil }
                        .transition(.move(edge: .top).combined(with: .opacity))
                }
                Spacer()
                bottomBar
            }
            // The middle column gives way first, so panels never widen the window.
            .frame(minWidth: 0, maxWidth: .infinity)
            .layoutPriority(-1)
            VStack(alignment: .trailing, spacing: 10) {
                ViewControlsColumn(store: store, viewport: viewport, ui: ui, compact: compact)
                if store.sketch != nil && !ui.visualizing {
                    FitScroll { ConstraintColumn(store: store, viewport: viewport) }
                }
            }
            if !compact { rightPanel }
        }
        .coordinateSpace(name: OverlaySpace.name)
        .onPreferenceChange(RailMenuFrameKey.self) { railMenu = $0 }
        .padding(10)
        .animation(.snappy, value: store.toast)
        .animation(.snappy, value: ui.openMenu)
    }

    @ViewBuilder
    private var rightPanel: some View {
        if ui.visualizing {
            VisualizationPanel(store: store, viewport: viewport, ui: ui)
                .frame(maxHeight: .infinity, alignment: .top)
        } else if viewport.measuring {
            MeasureSidePanel(store: store, viewport: viewport)
                .frame(width: 280)
                .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        } else if (ui.showHistory || ui.showProperties) && !(store.sketch != nil && width < 1200) {
            // While sketching on narrower screens the constraint column needs the room.
            VStack(spacing: 10) {
                if ui.showProperties {
                    PropertiesPanel(store: store, viewport: viewport)
                        .frame(width: 250)
                        .frame(maxHeight: ui.showHistory ? 340 : .infinity)
                        .fixedSize(horizontal: false, vertical: ui.showHistory)
                        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                }
                if ui.showHistory {
                    HistoryPanel(store: store)
                        .frame(width: 250)
                        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
                }
            }
        }
    }

    @ViewBuilder
    private var bottomBar: some View {
        VStack(spacing: 8) {
            if viewport.sectionEnabled { SectionPanel(viewport: viewport) }
            if store.draft != nil {
                OperationPanel(store: store)
            } else if store.sketch != nil {
                SketchBar(store: store, viewport: viewport)
            } else if viewport.measuring && compact {
                MeasurePanel(store: store, viewport: viewport)
            } else if !ui.visualizing {
                SelectionBar(store: store)
            }
        }
    }

    /// Items and History open as sheets on narrow (iPhone) screens.
    private func compactBinding(_ key: ReferenceWritableKeyPath<UIState, Bool>) -> Binding<Bool> {
        Binding(get: { compact && ui[keyPath: key] }, set: { ui[keyPath: key] = $0 })
    }

    private func sheet(_ title: String, @ViewBuilder content: () -> some View) -> some View {
        NavigationStack {
            content()
                .navigationTitle(title)
                #if os(iOS)
                .navigationBarTitleDisplayMode(.inline)
                #endif
        }
        .presentationDetents([.medium, .large])
    }
}

private struct LengthUnitKey: EnvironmentKey {
    static let defaultValue = LengthUnit.mm
}

extension EnvironmentValues {
    var lengthUnit: LengthUnit {
        get { self[LengthUnitKey.self] }
        set { self[LengthUnitKey.self] = newValue }
    }
}

struct ToolButtonStyle: ButtonStyle {
    var prominent = false

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 15, weight: .medium))
            .frame(minWidth: 36, minHeight: 36)
            .padding(.horizontal, 4)
            .foregroundStyle(prominent ? Color.white : Color.primary)
            .background(prominent ? AnyShapeStyle(Color.accentColor) : AnyShapeStyle(.regularMaterial),
                        in: RoundedRectangle(cornerRadius: 10, style: .continuous))
            .opacity(configuration.isPressed ? 0.6 : 1)
            .contentShape(Rectangle())
    }
}

struct ToastView: View {
    let text: String
    let dismiss: () -> Void

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.orange)
            Text(text).font(.callout).lineLimit(3)
            Button(action: dismiss) { Image(systemName: "xmark") }.buttonStyle(.plain)
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 10)
        .background(.regularMaterial, in: Capsule())
        .task {
            try? await Task.sleep(for: .seconds(4))
            dismiss()
        }
    }
}


/// Sheets, importers and exporters opened from the menu bar.
struct MenuSheets: ViewModifier {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController
    @ObservedObject var ui: UIState

    func body(content: Content) -> some View {
        content
            .sheet(isPresented: $ui.showDrawing) { DrawingWindow(store: store, ui: ui) }
            .sheet(isPresented: $ui.showFieldOfView) { FieldOfViewSheet(store: store) }
            .sheet(item: $ui.sketchTransform) { kind in SketchTransformSheet(kind: kind, store: store) }
            .sheet(isPresented: $ui.showSketchText) { SketchTextSheet(store: store) }
            .fileImporter(isPresented: $ui.importingMaterial, allowedContentTypes: [.json]) { result in
                if case let .success(url) = result { store.importMaterial(url) }
            }
            .fileImporter(isPresented: $ui.importingDecal, allowedContentTypes: [.image]) { result in
                if case let .success(url) = result { store.insertDecal(url) }
            }
            .fileImporter(isPresented: $ui.importingHeightMap, allowedContentTypes: [.image]) { result in
                if case let .success(url) = result { store.insertTerrain(heightMap: url) }
            }
            .sheet(isPresented: $ui.showExport) { ExportSheet(store: store, ui: ui) }
            .fileExporter(isPresented: $ui.exportingAny, document: ui.anyExportFile, contentType: ui.anyExportType,
                          defaultFilename: ui.anyExportName) { result in
                if case let .success(url) = result { store.toast = "Exported \(url.lastPathComponent)" }
            }
            .fileExporter(isPresented: $ui.publishing, document: ui.publishFile, contentType: .html,
                          defaultFilename: ui.publishName) { result in
                if case .success = result { store.toast = "Published \(ui.publishName)" }
            }
    }
}

/// Pinned measurements, kept on screen outside Measure mode.
struct PinnedMeasurementsCard: View {
    @ObservedObject var viewport: ViewportController

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            ForEach(Array(viewport.pinnedMeasurements.enumerated()), id: \.offset) { i, text in
                HStack(spacing: 6) {
                    Image(systemName: "pin.fill").font(.caption2).foregroundStyle(.secondary)
                    Text(text).font(.caption.monospacedDigit())
                    Button { viewport.pinnedMeasurements.remove(at: i) } label: { Image(systemName: "xmark") }
                        .buttonStyle(.plain).font(.caption2).foregroundStyle(.secondary)
                }
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(.thickMaterial, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}
