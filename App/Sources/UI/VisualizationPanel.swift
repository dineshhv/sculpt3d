import SwiftUI
import UniformTypeIdentifiers
import simd

/// Visualize mode's right-hand panel: material library, environment and camera.
struct VisualizationPanel: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var viewport: ViewportController

    enum Tab: String, CaseIterable, Identifiable {
        case material = "Material", environment = "Environment", camera = "Camera"
        var id: String { rawValue }
    }

    @ObservedObject var ui: UIState
    private var tab: Tab { ui.visualizeTab }
    @State private var query = ""
    @State private var collapsed: Set<String> = []
    @State private var capture = PNGFile(data: Data())
    @State private var savingCapture = false
    @State private var capturing = false

    init(store: ModelStore, viewport: ViewportController, ui: UIState) {
        self.store = store
        self.viewport = viewport
        self.ui = ui
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Picker("", selection: $ui.visualizeTab) {
                ForEach(Tab.allCases) { Text($0.rawValue).tag($0) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            ScrollView {
                VStack(alignment: .leading, spacing: 12) {
                    switch tab {
                    case .material: materialTab
                    case .environment: environmentTab
                    case .camera: cameraTab
                    }
                }
                .padding(.bottom, 4)
            }
        }
        .padding(12)
        .frame(width: 300)
        .background(PanelBackground(), in: RoundedRectangle(cornerRadius: 16, style: .continuous))
        .shadow(color: .black.opacity(0.1), radius: 10, y: 4)
        .fileExporter(isPresented: $savingCapture, document: capture, contentType: .png,
                      defaultFilename: "Sculpt3D Render.png") { _ in }
    }

    // MARK: Material

    private var targets: [Int] { store.appearanceTargets }

    private var current: Appearance {
        targets.first.flatMap { store.settings.appearance[$0] } ?? Appearance()
    }

    private func apply(_ change: (inout Appearance) -> Void) {
        var a = current
        change(&a)
        store.setAppearance(a, for: targets)
    }

    /// Distinct appearances on the model's bodies with the bodies using each.
    private var usedMaterials: [(appearance: Appearance, bodies: [Int])] {
        var groups: [(appearance: Appearance, bodies: [Int])] = []
        for id in store.bodyIds {
            let a = store.settings.appearance[id] ?? Appearance()
            if let i = groups.firstIndex(where: { $0.appearance == a }) {
                groups[i].bodies.append(id)
            } else {
                groups.append((a, [id]))
            }
        }
        return groups
    }

    private func label(for a: Appearance) -> (finish: String, name: String) {
        if let id = a.preset, let p = MaterialPreset.preset(id: id) { return (p.finish, p.name) }
        if a == Appearance() { return ("Default", "Material") }
        let finish = a.opacity < 1 ? "Translucent" : a.metallic > 0.5 ? (a.roughness < 0.3 ? "Polished" : "Brushed")
            : a.roughness < 0.3 ? "Glossy" : a.roughness > 0.7 ? "Matte" : "Satin"
        return (finish, a.metallic > 0.5 ? "Custom Metal" : "Custom")
    }

    private var filteredLibrary: [String: [MaterialPreset]] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        let hits = q.isEmpty ? MaterialPreset.library
            : MaterialPreset.library.filter { "\($0.family) \($0.finish) \($0.name)".lowercased().contains(q) }
        return Dictionary(grouping: hits, by: \.family)
    }

    @ViewBuilder
    private var materialTab: some View {
        HStack(spacing: 6) {
            Image(systemName: "magnifyingglass").foregroundStyle(Color.gray)
            TextField("Search materials", text: $query)
                .textFieldStyle(.plain)
            if !query.isEmpty {
                Button { query = "" } label: { Image(systemName: "xmark.circle.fill").foregroundStyle(Color.gray) }
                    .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(.quaternary.opacity(0.6), in: RoundedRectangle(cornerRadius: 8, style: .continuous))

        Text(store.selection.isEmpty ? "Applies to all bodies" : "Applies to \(targets.count) selected bod\(targets.count == 1 ? "y" : "ies")")
            .font(.caption)
            .foregroundStyle(Color.gray)

        if query.isEmpty && !store.bodyIds.isEmpty {
            sectionHeader("Used materials", collapsible: false)
            VStack(spacing: 2) {
                ForEach(Array(usedMaterials.enumerated()), id: \.offset) { _, used in
                    let l = label(for: used.appearance)
                    Button {
                        store.selection = used.bodies.map { Pick.body($0) }
                    } label: {
                        HStack(spacing: 10) {
                            MaterialSphere(appearance: used.appearance, size: 28)
                            VStack(alignment: .leading, spacing: 0) {
                                Text(l.finish).font(.caption2).foregroundStyle(Color.gray)
                                Text(l.name).font(.callout)
                            }
                            Spacer(minLength: 0)
                            Text("\(used.bodies.count)")
                                .font(.caption.monospacedDigit())
                                .foregroundStyle(Color.gray)
                            Image(systemName: "chevron.right").font(.caption).foregroundStyle(Color.gray.opacity(0.7))
                        }
                        .padding(.vertical, 4)
                        .padding(.horizontal, 6)
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .help("Select the bodies using this material")
                }
            }
        }

        let library = filteredLibrary
        ForEach(MaterialPreset.families.filter { library[$0] != nil }, id: \.self) { family in
            let open = !collapsed.contains(family) || !query.isEmpty
            sectionHeader(family, collapsible: query.isEmpty, open: open)
            if open {
                LazyVGrid(columns: [GridItem(.flexible(), spacing: 10), GridItem(.flexible(), spacing: 10)], spacing: 12) {
                    ForEach(library[family] ?? []) { preset in
                        presetTile(preset)
                    }
                }
            }
        }
        if filteredLibrary.isEmpty {
            Text("No material matches “\(query)”").font(.callout).foregroundStyle(Color.gray)
        }

        sectionHeader("Custom", collapsible: false)
        customEditor
    }

    private func sectionHeader(_ title: String, collapsible: Bool, open: Bool = true) -> some View {
        Button {
            guard collapsible else { return }
            if collapsed.contains(title) { collapsed.remove(title) } else { collapsed.insert(title) }
        } label: {
            HStack {
                Text(title).font(.subheadline.weight(.semibold))
                Spacer()
                if collapsible {
                    Image(systemName: "chevron.right")
                        .font(.caption.weight(.semibold))
                        .foregroundStyle(Color.gray)
                        .rotationEffect(.degrees(open ? 90 : 0))
                }
            }
            .padding(.top, 6)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .animation(.snappy, value: open)
    }

    private func presetTile(_ preset: MaterialPreset) -> some View {
        let active = current.preset == preset.id
        return Button {
            store.setAppearance(preset.appearance, for: targets)
        } label: {
            VStack(spacing: 4) {
                MaterialSphere(appearance: preset.appearance, size: 64)
                    .padding(6)
                    .background(active ? Color.accentColor.opacity(0.15) : .clear,
                                in: RoundedRectangle(cornerRadius: 12, style: .continuous))
                    .overlay(RoundedRectangle(cornerRadius: 12, style: .continuous)
                        .strokeBorder(active ? Color.accentColor : .clear, lineWidth: 2))
                Text(preset.finish).font(.caption2).foregroundStyle(Color.gray)
                Text(preset.name).font(.caption).lineLimit(1).minimumScaleFactor(0.8)
            }
            .frame(maxWidth: .infinity)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(targets.isEmpty)
    }

    @ViewBuilder
    private var customEditor: some View {
        let a = current
        HStack {
            MaterialSphere(appearance: a, size: 40)
            ColorPicker("Colour", selection: Binding(get: { Color(rgb: a.color) }, set: { c in
                apply { $0.color = c.rgb; $0.preset = nil }
            }), supportsOpacity: false)
        }
        .disabled(targets.isEmpty)
        sliderRow("Metallic", value: a.metallic) { v in apply { $0.metallic = v; $0.preset = nil } }
        sliderRow("Roughness", value: a.roughness) { v in apply { $0.roughness = v; $0.preset = nil } }
        sliderRow("Opacity", value: a.opacity, range: 0.05...1) { v in apply { $0.opacity = v; $0.preset = nil } }
        Button("Reset to default") { store.setAppearance(Appearance(), for: targets) }
            .controlSize(.small)
            .disabled(targets.isEmpty)
    }

    private func sliderRow(_ title: String, value: Float, range: ClosedRange<Float> = 0...1,
                           set: @escaping (Float) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).font(.caption)
                Spacer()
                Text(String(format: "%.2f", value)).font(.caption.monospacedDigit()).foregroundStyle(Color.gray)
            }
            Slider(value: Binding(get: { value }, set: set), in: range)
                .controlSize(.small)
        }
        .disabled(targets.isEmpty)
    }

    // MARK: Environment

    private static let backgrounds: [SIMD3<Float>] = [
        [0.90, 0.91, 0.93], [1, 1, 1], [0.74, 0.76, 0.79], [0.36, 0.38, 0.42],
        [0.12, 0.13, 0.15], [0.80, 0.86, 0.94], [0.93, 0.89, 0.82],
    ]

    @ViewBuilder
    private var environmentTab: some View {
        let env = $store.settings.environment
        sectionHeader("Background", collapsible: false)
        HStack(spacing: 8) {
            ForEach(Self.backgrounds.indices, id: \.self) { i in
                let c = Self.backgrounds[i]
                let on = simd_distance(store.settings.environment.background, c) < 0.01
                Button { env.wrappedValue.background = c } label: {
                    Circle()
                        .fill(Color(rgb: c))
                        .overlay(Circle().strokeBorder(on ? Color.accentColor : .black.opacity(0.15), lineWidth: on ? 3 : 1))
                        .frame(width: 24, height: 24)
                }
                .buttonStyle(.plain)
            }
            Spacer(minLength: 0)
        }
        ColorPicker("Colour", selection: Binding(get: { Color(rgb: env.wrappedValue.background) },
                                                 set: { env.wrappedValue.background = $0.rgb }), supportsOpacity: false)
        Toggle("Gradient", isOn: env.gradient)
        Toggle("Show grid", isOn: env.showGrid)

        sectionHeader("Lighting", collapsible: false)
        labeledSlider("Intensity", value: env.lightIntensity, range: 0...2, format: "%.1f×")
        labeledSlider("Direction", value: env.lightAzimuth, range: -180...180, format: "%.0f°")
        labeledSlider("Height", value: env.lightElevation, range: 10...85, format: "%.0f°")
        Toggle("Ground shadow", isOn: env.groundShadow)

        sectionHeader("Rendering", collapsible: false)
        Toggle("Cast shadows", isOn: env.castShadows)
        if store.settings.environment.castShadows {
            labeledSlider("Shadow softness", value: env.shadowSoftness, range: 0...1, format: "%.2f")
        }
        Toggle("Ambient occlusion", isOn: env.ambientOcclusion)
        if store.settings.environment.ambientOcclusion {
            labeledSlider("Occlusion", value: env.occlusionStrength, range: 0...1, format: "%.2f")
        }
        Toggle("Smooth shading", isOn: env.smoothShading)

        Button("Reset environment") { env.wrappedValue = EnvironmentSettings() }
            .controlSize(.small)
            .padding(.top, 4)
    }

    private func labeledSlider(_ title: String, value: Binding<Float>, range: ClosedRange<Float>, format: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(title).font(.caption)
                Spacer()
                Text(String(format: format, value.wrappedValue)).font(.caption.monospacedDigit()).foregroundStyle(Color.gray)
            }
            Slider(value: value, in: range).controlSize(.small)
        }
    }

    // MARK: Camera

    @ViewBuilder
    private var cameraTab: some View {
        let cam = $store.settings.camera
        sectionHeader("Projection", collapsible: false)
        Picker("Projection", selection: cam.orthographic) {
            Text("Perspective").tag(false)
            Text("Orthographic").tag(true)
        }
        .pickerStyle(.segmented)
        .labelsHidden()
        labeledSlider("Field of view", value: cam.fieldOfView, range: 20...90, format: "%.0f°")
            .disabled(cam.wrappedValue.orthographic)
            .opacity(cam.wrappedValue.orthographic ? 0.5 : 1)

        sectionHeader("Capture", collapsible: false)
        Text("Renders the current view at 4× resolution with the environment and materials.")
            .font(.caption)
            .foregroundStyle(Color.gray)
        Button {
            runCapture()
        } label: {
            Label(capturing ? "Rendering…" : "Capture", systemImage: "camera.aperture")
                .frame(maxWidth: .infinity)
        }
        .buttonStyle(.borderedProminent)
        .disabled(capturing)
    }

    private func runCapture() {
        capturing = true
        // Let the button show its busy state before the (synchronous) render.
        DispatchQueue.main.async {
            defer { capturing = false }
            guard let image = viewport.screenshot(scale: 4), let data = PNGFile.encode(image) else { return }
            capture = PNGFile(data: data)
            savingCapture = true
        }
    }
}

// MARK: - Sphere preview

/// A material ball drawn with gradients: shaded body, environment bands for
/// metals, a specular highlight sized by roughness and a Fresnel rim.
struct MaterialSphere: View {
    let appearance: Appearance
    var size: CGFloat = 56

    var body: some View {
        let a = appearance
        let rough = CGFloat(min(max(a.roughness, 0.02), 1))
        let metal = CGFloat(min(max(a.metallic, 0), 1))
        let alpha = Double(min(max(a.opacity, 0.05), 1))
        let base = a.color
        ZStack {
            if alpha < 1 {
                // Glass: faint body, strong rim and reflections.
                Circle().fill(Color(rgb: base).opacity(0.18 + alpha * 0.5))
                Circle().fill(RadialGradient(colors: [.clear, Color(rgb: base * 0.6).opacity(0.5)],
                                             center: .center, startRadius: size * 0.2, endRadius: size * 0.5))
            } else {
                // Diffuse body shading, lit from the upper left.
                Circle().fill(RadialGradient(colors: [Color(rgb: lift(base, 0.25 * Float(1 - metal))), Color(rgb: base),
                                                      Color(rgb: base * Float(0.35 + 0.25 * (1 - metal)))],
                                             center: UnitPoint(x: 0.35, y: 0.3), startRadius: 0, endRadius: size * 0.75))
                if metal > 0.01 {
                    // Studio reflection: bright walls, dark horizon band, floor.
                    Circle().fill(LinearGradient(stops: metalStops(base, rough: rough),
                                                 startPoint: .top, endPoint: .bottom))
                        .opacity(Double(metal))
                    Circle().fill(RadialGradient(colors: [.clear, .black.opacity(0.35)],
                                                 center: .center, startRadius: size * 0.25, endRadius: size * 0.52))
                        .opacity(Double(metal))
                }
            }
            // Specular highlight: small and sharp when smooth, broad and dim when rough.
            let hs = size * (0.12 + 0.35 * rough)
            Ellipse()
                .fill(Color.white)
                .frame(width: hs * 1.25, height: hs)
                .blur(radius: size * (0.015 + 0.12 * rough))
                .opacity(Double((1 - rough) * 0.85 + 0.08))
                .offset(x: -size * 0.17, y: -size * 0.2)
            // Fresnel rim.
            Circle().strokeBorder(LinearGradient(colors: [.white.opacity(0.55 * Double(1 - rough) + 0.1), .white.opacity(0.05)],
                                                 startPoint: .top, endPoint: .bottom), lineWidth: max(1, size * 0.03))
        }
        .frame(width: size, height: size)
        .clipShape(Circle())
        .overlay(Circle().strokeBorder(.black.opacity(0.12), lineWidth: 0.5))
        .compositingGroup()
        .shadow(color: .black.opacity(0.18), radius: size * 0.05, y: size * 0.04)
        .accessibilityHidden(true)
    }

    private func lift(_ c: SIMD3<Float>, _ amount: Float) -> SIMD3<Float> { c + (SIMD3(repeating: 1) - c) * amount }

    private func metalStops(_ c: SIMD3<Float>, rough: CGFloat) -> [Gradient.Stop] {
        // Rough metals blur the bands towards their average.
        let r = Float(rough)
        func band(_ k: Float) -> Color {
            let v = c * (k + (0.62 - k) * min(r * 1.4, 1))
            return Color(rgb: simd_min(v, SIMD3(repeating: 1)))
        }
        return [
            .init(color: band(1.15), location: 0),
            .init(color: band(0.95), location: 0.3),
            .init(color: band(0.75), location: 0.5),
            .init(color: band(0.18), location: 0.56),
            .init(color: band(0.32), location: 0.72),
            .init(color: band(0.55), location: 1),
        ]
    }
}

// MARK: - Colour conversion

extension Color {
    init(rgb c: SIMD3<Float>) {
        self.init(.sRGB, red: Double(c.x), green: Double(c.y), blue: Double(c.z), opacity: 1)
    }

    /// sRGB components, clamped to 0…1.
    var rgb: SIMD3<Float> {
        #if os(macOS)
        let ns = NSColor(self).usingColorSpace(.sRGB) ?? .gray
        let v = SIMD3<Float>(Float(ns.redComponent), Float(ns.greenComponent), Float(ns.blueComponent))
        #else
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        UIColor(self).getRed(&r, green: &g, blue: &b, alpha: &a)
        let v = SIMD3<Float>(Float(r), Float(g), Float(b))
        #endif
        return simd_clamp(v, SIMD3(repeating: 0), SIMD3(repeating: 1))
    }
}

/// An opaque panel background. Secondary and tinted content inside a
/// scrolling material panel can lose all contrast on iOS, so this panel uses
/// a plain surface.
struct PanelBackground: ShapeStyle {
    func resolve(in environment: EnvironmentValues) -> Color {
        environment.colorScheme == .dark ? Color(white: 0.16) : Color(white: 0.97)
    }
}
