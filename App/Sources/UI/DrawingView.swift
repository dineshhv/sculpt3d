import CoreGraphics
import SwiftUI
import UniformTypeIdentifiers
#if os(macOS)
import AppKit
#else
import UIKit
#endif

/// One projected view on a drawing sheet.
struct DrawingProjection {
    enum Kind: String, CaseIterable, Identifiable {
        case front, top, right, isometric
        var id: String { rawValue }
        var title: String { self == .isometric ? "Isometric" : rawValue.capitalized }

        /// Direction the view looks in, and the world direction shown as up.
        var direction: (look: SIMD3<Double>, up: SIMD3<Double>) {
            switch self {
            case .front: return ([0, 1, 0], [0, 0, 1])
            case .top: return ([0, 0, -1], [0, 1, 0])
            case .right: return ([-1, 0, 0], [0, 0, 1])
            case .isometric:
                let look = simd_normalize(SIMD3<Double>(-1, 1, -1))
                let up = simd_normalize(SIMD3<Double>(0, 0, 1) - look * look.z)
                return (look, up)
            }
        }
    }

    var kind: Kind
    var visible: [[SIMD2<Double>]]
    var hidden: [[SIMD2<Double>]]
    /// Exact model size across and up this view (orthographic views), for dimensions.
    var extents: SIMD2<Double>?

    var bounds: (min: SIMD2<Double>, max: SIMD2<Double>) {
        let pts = (visible + hidden).flatMap { $0 }
        guard let first = pts.first else { return (.zero, .zero) }
        return pts.reduce((first, first)) { (simd_min($0.0, $1), simd_max($0.1, $1)) }
    }
}

/// Paper sizes, in millimetres (landscape).
enum PaperSize: String, CaseIterable, Identifiable {
    case a4, a3, letter, tabloid
    var id: String { rawValue }
    var title: String { ["a4": "A4", "a3": "A3", "letter": "Letter", "tabloid": "Tabloid"][rawValue]! }
    var size: CGSize {
        switch self {
        case .a4: return CGSize(width: 297, height: 210)
        case .a3: return CGSize(width: 420, height: 297)
        case .letter: return CGSize(width: 279.4, height: 215.9)
        case .tabloid: return CGSize(width: 431.8, height: 279.4)
        }
    }
}

/// Settings of the drawing (View ▸ Drawing Properties).
struct DrawingSettings: Equatable {
    var paper = PaperSize.a3
    var views: Set<DrawingProjection.Kind> = Set(DrawingProjection.Kind.allCases)
    var showHidden = true
    var showDimensions = true
    /// Drawing scale as paper:model; nil fits the views to the sheet.
    var scale: Double?
    var title = "Untitled"
}

/// Everything drawn on the sheet, in paper millimetres with y down.
struct DrawingSheet {
    struct Stroke {
        var points: [CGPoint]
        var dashed = false
        var width: CGFloat = 0.35
    }
    struct Label {
        var text: String
        var at: CGPoint
        var size: CGFloat = 3.5
        var bold = false
    }

    var size: CGSize
    var strokes: [Stroke] = []
    var labels: [Label] = []
    var scale: Double = 1

    static func layout(_ views: [DrawingProjection], settings: DrawingSettings, units: LengthUnit) -> DrawingSheet {
        var sheet = DrawingSheet(size: settings.paper.size)
        let w = sheet.size.width, h = sheet.size.height
        let margin: CGFloat = 10, titleHeight: CGFloat = 22
        // Border and title block.
        sheet.strokes.append(Stroke(points: [CGPoint(x: margin, y: margin), CGPoint(x: w - margin, y: margin),
                                             CGPoint(x: w - margin, y: h - margin), CGPoint(x: margin, y: h - margin),
                                             CGPoint(x: margin, y: margin)], width: 0.5))
        let tb = CGRect(x: w - margin - 120, y: h - margin - titleHeight, width: 120, height: titleHeight)
        sheet.strokes.append(Stroke(points: [CGPoint(x: tb.minX, y: tb.maxY), CGPoint(x: tb.minX, y: tb.minY),
                                             CGPoint(x: tb.maxX, y: tb.minY)], width: 0.5))
        sheet.strokes.append(Stroke(points: [CGPoint(x: tb.minX, y: tb.midY), CGPoint(x: tb.maxX, y: tb.midY)], width: 0.25))

        // Third-angle layout: top above front, right beside front, isometric top right.
        let area = CGRect(x: margin + 8, y: margin + 8, width: w - 2 * margin - 16, height: h - 2 * margin - titleHeight - 16)
        let cells: [DrawingProjection.Kind: CGRect] = [
            .top: CGRect(x: area.minX, y: area.minY, width: area.width / 2, height: area.height / 2),
            .isometric: CGRect(x: area.midX, y: area.minY, width: area.width / 2, height: area.height / 2),
            .front: CGRect(x: area.minX, y: area.midY, width: area.width / 2, height: area.height / 2),
            .right: CGRect(x: area.midX, y: area.midY, width: area.width / 2, height: area.height / 2),
        ]
        let shown = views.filter { settings.views.contains($0.kind) && !$0.visible.isEmpty }
        // One scale for every orthographic view so they line up and measure alike.
        var fit = Double.greatestFiniteMagnitude
        for v in shown {
            let b = v.bounds, cell = cells[v.kind]!.insetBy(dx: 12, dy: 12)
            let size = simd_max(b.max - b.min, SIMD2(repeating: 1e-6))
            fit = min(fit, Double(cell.width) / size.x, Double(cell.height) / size.y)
        }
        if fit == .greatestFiniteMagnitude { fit = 1 }
        let scale = settings.scale ?? niceScale(fit)
        sheet.scale = scale

        for v in shown {
            let cell = cells[v.kind]!
            let b = v.bounds, mid = (b.min + b.max) / 2
            func paper(_ p: SIMD2<Double>) -> CGPoint {
                CGPoint(x: Double(cell.midX) + (p.x - mid.x) * scale, y: Double(cell.midY) - (p.y - mid.y) * scale)
            }
            for line in v.visible { sheet.strokes.append(Stroke(points: line.map(paper), width: 0.4)) }
            if settings.showHidden && v.kind != .isometric {
                for line in v.hidden { sheet.strokes.append(Stroke(points: line.map(paper), dashed: true, width: 0.2)) }
            }
            sheet.labels.append(Label(text: v.kind.title.uppercased(), at: CGPoint(x: cell.midX, y: cell.maxY - 4), size: 3, bold: true))
            guard settings.showDimensions && v.kind != .isometric else { continue }
            // Overall size: width below the view, height to its left.
            let lo = paper(b.min), hi = paper(b.max)
            let size = v.extents ?? (b.max - b.min)
            let y = lo.y + 6, x = lo.x - 6
            func dim(_ v: Double) -> String { formatLength((v / units.scale * 10).rounded() / 10 * units.scale, units) }
            sheet.dimension(from: CGPoint(x: lo.x, y: y), to: CGPoint(x: hi.x, y: y), text: dim(size.x))
            sheet.dimension(from: CGPoint(x: x, y: lo.y), to: CGPoint(x: x, y: hi.y), text: dim(size.y))
        }
        sheet.labels.append(Label(text: settings.title, at: CGPoint(x: tb.midX, y: tb.minY + titleHeight / 4 + 1.5), size: 4.5, bold: true))
        let date = Date().formatted(date: .abbreviated, time: .omitted)
        sheet.labels.append(Label(text: "Scale \(scaleText(scale))   ·   Units \(units.symbol)   ·   \(date)   ·   Sculpt3D",
                                  at: CGPoint(x: tb.midX, y: tb.midY + titleHeight / 4 + 1), size: 2.6))
        return sheet
    }

    /// A linear dimension: extension of the measured span with arrow ticks and text.
    mutating func dimension(from a: CGPoint, to b: CGPoint, text: String) {
        strokes.append(Stroke(points: [a, b], width: 0.18))
        let d = CGPoint(x: b.x - a.x, y: b.y - a.y), len = max(hypot(d.x, d.y), 1e-6)
        let u = CGPoint(x: d.x / len, y: d.y / len), n = CGPoint(x: -u.y, y: u.x)
        for (p, s) in [(a, 1.0), (b, -1.0)] {
            let tip = p, back = CGPoint(x: p.x + u.x * 2.2 * s, y: p.y + u.y * 2.2 * s)
            strokes.append(Stroke(points: [CGPoint(x: back.x + n.x * 0.7, y: back.y + n.y * 0.7), tip,
                                           CGPoint(x: back.x - n.x * 0.7, y: back.y - n.y * 0.7)], width: 0.18))
        }
        // Text sits beside the line: further out when the line is vertical and the text runs across it.
        let across = abs(u.y) > abs(u.x) ? 1.5 + CGFloat(text.count) * 0.85 : 2.2
        let mid = CGPoint(x: (a.x + b.x) / 2 - n.x * across, y: (a.y + b.y) / 2 - n.y * across)
        labels.append(Label(text: text, at: mid, size: 2.8))
    }

    /// The largest standard scale (1:1, 1:2, 1:5, 2:1, …) that fits.
    static func niceScale(_ fit: Double) -> Double {
        let steps = [1.0, 2, 5]
        var best = 1e-9
        for e in -6...4 {
            for s in steps {
                let up = s * pow(10, Double(e))
                if up <= fit { best = max(best, up) }
            }
        }
        return best
    }

    static func scaleText(_ s: Double) -> String {
        s >= 1 ? "\(formatted(s)):1" : "1:\(formatted(1 / s))"
    }

    // MARK: Output

    /// Draws into a context whose units are paper millimetres scaled by `k`, y down.
    func draw(in ctx: CGContext, k: CGFloat) {
        ctx.setLineCap(.round)
        ctx.setLineJoin(.round)
        ctx.setStrokeColor(CGColor(red: 0.08, green: 0.09, blue: 0.12, alpha: 1))
        for s in strokes where s.points.count >= 2 {
            ctx.setLineWidth(s.width * k)
            ctx.setLineDash(phase: 0, lengths: s.dashed ? [1.6 * k, 1.0 * k] : [])
            ctx.beginPath()
            ctx.addLines(between: s.points.map { CGPoint(x: $0.x * k, y: $0.y * k) })
            ctx.strokePath()
        }
    }

    var pdf: Data {
        let data = NSMutableData()
        let k: CGFloat = 72 / 25.4
        var box = CGRect(origin: .zero, size: CGSize(width: size.width * k, height: size.height * k))
        guard let consumer = CGDataConsumer(data: data as CFMutableData),
              let ctx = CGContext(consumer: consumer, mediaBox: &box, nil) else { return Data() }
        ctx.beginPDFPage(nil)
        // PDF space is y up; flip so the sheet's y-down layout reads right.
        ctx.translateBy(x: 0, y: box.height)
        ctx.scaleBy(x: 1, y: -1)
        draw(in: ctx, k: k)
        for l in labels {
            drawText(l, in: ctx, k: k)
        }
        ctx.endPDFPage()
        ctx.closePDF()
        return data as Data
    }

    private func drawText(_ l: Label, in ctx: CGContext, k: CGFloat) {
        let font = CTFontCreateWithName((l.bold ? "Helvetica-Bold" : "Helvetica") as CFString, l.size * k, nil)
        let attributed = NSAttributedString(string: l.text, attributes: [NSAttributedString.Key(kCTFontAttributeName as String): font])
        let line = CTLineCreateWithAttributedString(attributed)
        let width = CTLineGetTypographicBounds(line, nil, nil, nil)
        ctx.saveGState()
        ctx.translateBy(x: l.at.x * k - CGFloat(width) / 2, y: l.at.y * k + l.size * k * 0.35)
        ctx.scaleBy(x: 1, y: -1)
        ctx.textPosition = .zero
        CTLineDraw(line, ctx)
        ctx.restoreGState()
    }

    var svg: Data {
        var out = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"\(size.width)mm\" height=\"\(size.height)mm\" "
        out += "viewBox=\"0 0 \(size.width) \(size.height)\">\n<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n"
        out += "<g fill=\"none\" stroke=\"#15171f\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
        for s in strokes where s.points.count >= 2 {
            let pts = s.points.map { String(format: "%.3f,%.3f", $0.x, $0.y) }.joined(separator: " ")
            out += "<polyline points=\"\(pts)\" stroke-width=\"\(s.width)\"\(s.dashed ? " stroke-dasharray=\"1.6 1\"" : "")/>\n"
        }
        out += "</g>\n<g font-family=\"Helvetica, Arial, sans-serif\" fill=\"#15171f\" text-anchor=\"middle\">\n"
        for l in labels {
            let text = l.text.replacingOccurrences(of: "&", with: "&amp;").replacingOccurrences(of: "<", with: "&lt;")
            out += String(format: "<text x=\"%.2f\" y=\"%.2f\" font-size=\"%.2f\"%@>%@</text>\n", l.at.x, l.at.y + l.size * 0.35, l.size,
                          l.bold ? " font-weight=\"bold\"" : "", text)
        }
        out += "</g>\n</svg>\n"
        return Data(out.utf8)
    }
}

extension ModelStore {
    /// Hidden-line projections of the visible bodies.
    func drawingProjections() -> [DrawingProjection] {
        let bodies = visibleMeshes.keys.sorted().map { NSNumber(value: $0) }
        guard !bodies.isEmpty else { return [] }
        var lo = SIMD3<Double>(repeating: .greatestFiniteMagnitude), hi = -lo
        // Tessellated bounds are tight; the kernel's box is padded by tolerances.
        for b in bodies { if let m = meshes[b.intValue] { lo = simd_min(lo, SIMD3<Double>(m.boundsMin)); hi = simd_max(hi, SIMD3<Double>(m.boundsMax)) } }
        let size = hi - lo
        return DrawingProjection.Kind.allCases.map { kind in
            let d = kind.direction
            var hidden: NSArray?
            let visible = kernel.drawingView(bodies, direction: d.look, up: d.up, hidden: &hidden)
            func polylines(_ list: [NSData]) -> [[SIMD2<Double>]] {
                list.map { data in (data as Data).withUnsafeBytes { Array($0.bindMemory(to: SIMD2<Double>.self)) } }
            }
            let extents: SIMD2<Double>? = switch kind {
            case .front: SIMD2(size.x, size.z)
            case .top: SIMD2(size.x, size.y)
            case .right: SIMD2(size.y, size.z)
            case .isometric: nil
            }
            return DrawingProjection(kind: kind, visible: polylines(visible as [NSData]),
                                     hidden: polylines((hidden as? [NSData]) ?? []), extents: extents)
        }
    }
}

struct DrawingFile: FileDocument {
    static var readableContentTypes: [UTType] { [.pdf, .svg, .data] }
    static var writableContentTypes: [UTType] { [.pdf, .svg, UTType(filenameExtension: "dxf") ?? .data, .data] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

/// File ▸ New Drawing: standard views of the model on a sheet, exportable as PDF or SVG.
struct DrawingWindow: View {
    @ObservedObject var store: ModelStore
    @ObservedObject var ui: UIState
    @State private var projections: [DrawingProjection] = []
    @State private var exporting = false
    @State private var exportFile = DrawingFile(data: Data())
    @State private var exportType = UTType.pdf
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        let sheet = DrawingSheet.layout(projections, settings: ui.drawing, units: store.settings.units)
        HStack(spacing: 0) {
            VStack(spacing: 0) {
                HStack {
                    Text("Drawing").font(.headline)
                    Spacer()
                    Button { ui.drawingProperties.toggle() } label: { Label("Properties", systemImage: "slider.horizontal.3") }
                    Menu {
                        Button("PDF…") { export(sheet.pdf, .pdf) }
                        Button("SVG…") { export(sheet.svg, .svg) }
                        Button("DXF…") { export(SketchVector.dxf(sheet: sheet), UTType(filenameExtension: "dxf") ?? .data) }
                    } label: { Label("Export", systemImage: "square.and.arrow.up") }
                    .fixedSize()
                    Button("Done") { dismiss() }.keyboardShortcut(.cancelAction)
                }
                .padding(12)
                Divider()
                GeometryReader { g in
                    let k = min((g.size.width - 40) / sheet.size.width, (g.size.height - 40) / sheet.size.height)
                    Canvas { ctx, size in
                        let origin = CGPoint(x: (size.width - sheet.size.width * k) / 2, y: (size.height - sheet.size.height * k) / 2)
                        ctx.fill(Path(CGRect(origin: origin, size: CGSize(width: sheet.size.width * k, height: sheet.size.height * k))),
                                 with: .color(.white))
                        ctx.translateBy(x: origin.x, y: origin.y)
                        for s in sheet.strokes where s.points.count >= 2 {
                            var path = Path()
                            path.addLines(s.points.map { CGPoint(x: $0.x * k, y: $0.y * k) })
                            ctx.stroke(path, with: .color(Color(red: 0.08, green: 0.09, blue: 0.12)),
                                       style: StrokeStyle(lineWidth: max(s.width * k, 0.5), lineCap: .round, lineJoin: .round,
                                                          dash: s.dashed ? [1.6 * k, 1.0 * k] : []))
                        }
                        for l in sheet.labels {
                            ctx.draw(Text(l.text).font(.system(size: l.size * k, weight: l.bold ? .bold : .regular))
                                        .foregroundColor(Color(red: 0.08, green: 0.09, blue: 0.12)),
                                     at: CGPoint(x: l.at.x * k, y: l.at.y * k))
                        }
                    }
                    .shadow(color: .black.opacity(0.15), radius: 8, y: 3)
                }
                .background(Color(white: 0.86))
                .overlay {
                    if projections.isEmpty {
                        Text(store.visibleMeshes.isEmpty ? "Add a body to the model to draw it." : "Projecting views…")
                            .foregroundStyle(.secondary)
                    }
                }
            }
            if ui.drawingProperties {
                Divider()
                DrawingPropertiesPanel(settings: $ui.drawing, scale: sheet.scale)
                    .frame(width: 250)
            }
        }
        .frame(minWidth: 1000, idealWidth: 1300, minHeight: 700, idealHeight: 900)
        .presentationSizing(.page)
        .task {
            if ui.drawing.title == "Untitled" { ui.drawing.title = store.documentName }
            projections = store.drawingProjections()
        }
        .fileExporter(isPresented: $exporting, document: exportFile, contentType: exportType,
                      defaultFilename: "\(ui.drawing.title) Drawing") { _ in }
    }

    private func export(_ data: Data, _ type: UTType) {
        exportFile = DrawingFile(data: data)
        exportType = type
        exporting = true
    }
}

struct DrawingPropertiesPanel: View {
    @Binding var settings: DrawingSettings
    var scale: Double

    var body: some View {
        Form {
            Section("Sheet") {
                TextField("Title", text: $settings.title)
                Picker("Paper", selection: $settings.paper) {
                    ForEach(PaperSize.allCases) { Text($0.title).tag($0) }
                }
                Picker("Scale", selection: $settings.scale) {
                    Text("Fit (\(DrawingSheet.scaleText(scale)))").tag(Double?.none)
                    ForEach([10.0, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01], id: \.self) {
                        Text(DrawingSheet.scaleText($0)).tag(Double?.some($0))
                    }
                }
            }
            Section("Views") {
                ForEach(DrawingProjection.Kind.allCases) { kind in
                    Toggle(kind.title, isOn: Binding(get: { settings.views.contains(kind) },
                                                     set: { on in if on { settings.views.insert(kind) } else { settings.views.remove(kind) } }))
                }
            }
            Section("Lines") {
                Toggle("Hidden lines", isOn: $settings.showHidden)
                Toggle("Overall dimensions", isOn: $settings.showDimensions)
            }
        }
        .formStyle(.grouped)
    }
}
