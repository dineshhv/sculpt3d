import MetalKit
import simd

/// Lines drawn with per-vertex colours (grid, axes, sketch overlay).
struct LineBatch {
    var positions: [Float] = []
    var colors: [SIMD4<Float>] = []

    var vertexCount: Int { colors.count }

    mutating func line(_ a: SIMD3<Float>, _ b: SIMD3<Float>, _ color: SIMD4<Float>) {
        positions += [a.x, a.y, a.z, b.x, b.y, b.z]
        colors += [color, color]
    }

    mutating func polyline(_ points: [SIMD3<Float>], _ color: SIMD4<Float>) {
        for (a, b) in zip(points, points.dropFirst()) { line(a, b, color) }
    }
}

enum DisplayMode: String, CaseIterable, Identifiable {
    case shaded, wireframe, xray, visualized, zebra, curvature

    var id: String { rawValue }

    var title: String {
        switch self {
        case .shaded: return "Shaded"
        case .wireframe: return "Wireframe"
        case .xray: return "X-Ray"
        case .visualized: return "Visualized"
        case .zebra: return "Zebra Analysis"
        case .curvature: return "Curvature Map"
        }
    }

    var symbol: String {
        switch self {
        case .shaded: return "cube.fill"
        case .wireframe: return "cube"
        case .xray: return "cube.transparent"
        case .visualized: return "sparkles"
        case .zebra: return "line.3.horizontal"
        case .curvature: return "circle.dotted.and.circle"
        }
    }

    /// Option-key shortcut in the View menu.
    var key: Character {
        switch self {
        case .shaded: return "s"
        case .wireframe: return "w"
        case .xray: return "x"
        case .visualized: return "v"
        case .zebra: return "z"
        case .curvature: return "c"
        }
    }

    /// Analysis and render modes draw surfaces without the CAD edge overlay.
    var drawsEdgesByDefault: Bool { self == .shaded || self == .xray || self == .wireframe }
}

/// Everything the renderer needs for one frame.
struct SceneState {
    var camera: Camera
    var selection: [Pick]
    var hover: Pick?
    var ghostBody: Int?
    var grid: LineBatch
    var overlay: LineBatch
    var displayMode: DisplayMode = .shaded
    /// Draw body edges over shaded surfaces (View ▸ Show Edges).
    var showEdges = true
    /// Also draw edges hidden behind surfaces, faintly (View ▸ Show Hidden Edges).
    var showHiddenEdges = false
    /// Section plane: xyz normal, w offset.
    var clip: SIMD4<Float>?
    var appearance: [Int: Appearance] = [:]
    /// Background, key light and ground shadow.
    var environment = EnvironmentSettings()
    /// Projection and field of view; applied to `camera` when drawing.
    var cameraSettings = CameraSettings()
    /// Reference images, drawn as textured quads on their planes.
    var images: [ReferenceImage] = []
}

final class Renderer {
    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private let surfacePipeline: MTLRenderPipelineState
    private let edgePipeline: MTLRenderPipelineState
    private let colorPipeline: MTLRenderPipelineState
    private let backgroundPipeline: MTLRenderPipelineState
    private let shadowPipeline: MTLRenderPipelineState
    private let imagePipeline: MTLRenderPipelineState
    private let catcherPipeline: MTLRenderPipelineState
    private let shadowMapPipeline: MTLRenderPipelineState
    private let gbufferPipeline: MTLRenderPipelineState
    private let aoPipeline: MTLRenderPipelineState
    private let blurPipeline: MTLRenderPipelineState
    private let prepassDepthState: MTLDepthStencilState
    /// Key light depth, rendered each frame in Visualized mode.
    private let shadowMap: MTLTexture
    /// Bound in place of the occlusion texture when it is off.
    private let whiteTexture: MTLTexture
    /// Screen-sized buffers for ambient occlusion: world position, normal,
    /// depth, raw and blurred occlusion.
    private var aoTargets: (position: MTLTexture, normal: MTLTexture, depth: MTLTexture, raw: MTLTexture, blurred: MTLTexture)?
    private let depthState: MTLDepthStencilState
    private let overlayDepthState: MTLDepthStencilState
    private let transparentDepthState: MTLDepthStencilState
    /// Passes only where something is already in front: hidden edges.
    private let hiddenDepthState: MTLDepthStencilState
    private let colorFormat: MTLPixelFormat
    private let depthFormat: MTLPixelFormat

    private struct GPUBody {
        var positions, normals, faceIds, indices: MTLBuffer
        var indexCount: Int
        var edgePositions, edgeIds: MTLBuffer?
        var edgeVertexCount: Int
        var boundsMin, boundsMax: SIMD3<Float>
        var shapeKey: UInt64 = 0
        /// Normals averaged across faces meeting at shallow angles; built on first use.
        var smoothNormals: MTLBuffer?
    }

    private var bodies: [Int: GPUBody] = [:]
    private var meshes: [Int: BodyMesh] = [:]
    #if os(macOS)
    static let shadowMapSize = 4096
    #else
    static let shadowMapSize = 2048
    #endif
    private var syncedVersion = -1
    private let images: ReferenceImageCache

    static let sampleCount = 4
    /// Largest side of a screenshot tile; bigger captures are rendered in tiles.
    static let tileSize = 2048
    /// Largest side of a captured image.
    static let maxImageSide: CGFloat = 16384

    init?(view: MTKView) {
        guard let device = view.device ?? MTLCreateSystemDefaultDevice(),
              let queue = device.makeCommandQueue(),
              let library = device.makeDefaultLibrary() else { return nil }
        self.device = device
        self.queue = queue
        images = ReferenceImageCache(device: device, queue: queue)
        view.device = device
        view.colorPixelFormat = .bgra8Unorm
        view.depthStencilPixelFormat = .depth32Float
        view.sampleCount = Self.sampleCount
        view.clearColor = Self.clearColor(EnvironmentSettings())

        func pipeline(_ vertex: String, _ fragment: String) throws -> MTLRenderPipelineState {
            let d = MTLRenderPipelineDescriptor()
            d.vertexFunction = library.makeFunction(name: vertex)
            d.fragmentFunction = library.makeFunction(name: fragment)
            d.rasterSampleCount = Self.sampleCount
            d.depthAttachmentPixelFormat = view.depthStencilPixelFormat
            let c = d.colorAttachments[0]!
            c.pixelFormat = view.colorPixelFormat
            c.isBlendingEnabled = true
            c.sourceRGBBlendFactor = .sourceAlpha
            c.destinationRGBBlendFactor = .oneMinusSourceAlpha
            c.sourceAlphaBlendFactor = .one
            c.destinationAlphaBlendFactor = .oneMinusSourceAlpha
            return try device.makeRenderPipelineState(descriptor: d)
        }

        do {
            surfacePipeline = try pipeline("surface_vertex", "surface_fragment")
            edgePipeline = try pipeline("edge_vertex", "edge_fragment")
            colorPipeline = try pipeline("color_vertex", "color_fragment")
            backgroundPipeline = try pipeline("background_vertex", "background_fragment")
            shadowPipeline = try pipeline("shadow_vertex", "shadow_fragment")
            imagePipeline = try pipeline("image_vertex", "image_fragment")
            catcherPipeline = try pipeline("catcher_vertex", "catcher_fragment")

            let sm = MTLRenderPipelineDescriptor()
            sm.vertexFunction = library.makeFunction(name: "shadowmap_vertex")
            sm.fragmentFunction = library.makeFunction(name: "shadowmap_fragment")
            sm.depthAttachmentPixelFormat = .depth32Float
            shadowMapPipeline = try device.makeRenderPipelineState(descriptor: sm)

            let gb = MTLRenderPipelineDescriptor()
            gb.vertexFunction = library.makeFunction(name: "gbuffer_vertex")
            gb.fragmentFunction = library.makeFunction(name: "gbuffer_fragment")
            gb.depthAttachmentPixelFormat = .depth32Float
            gb.colorAttachments[0].pixelFormat = .rgba32Float
            gb.colorAttachments[1].pixelFormat = .rgba16Float
            gbufferPipeline = try device.makeRenderPipelineState(descriptor: gb)

            func fullscreen(_ fragment: String) throws -> MTLRenderPipelineState {
                let d = MTLRenderPipelineDescriptor()
                d.vertexFunction = library.makeFunction(name: "fullscreen_vertex")
                d.fragmentFunction = library.makeFunction(name: fragment)
                d.colorAttachments[0].pixelFormat = .r16Float
                return try device.makeRenderPipelineState(descriptor: d)
            }
            aoPipeline = try fullscreen("ao_fragment")
            blurPipeline = try fullscreen("blur_fragment")
        } catch {
            print("Pipeline error: \(error)")
            return nil
        }

        let depth = MTLDepthStencilDescriptor()
        depth.depthCompareFunction = .lessEqual
        depth.isDepthWriteEnabled = true
        depthState = device.makeDepthStencilState(descriptor: depth)!
        let overlay = MTLDepthStencilDescriptor()
        overlay.depthCompareFunction = .always
        overlay.isDepthWriteEnabled = false
        overlayDepthState = device.makeDepthStencilState(descriptor: overlay)!
        let transparent = MTLDepthStencilDescriptor()
        transparent.depthCompareFunction = .lessEqual
        transparent.isDepthWriteEnabled = false
        transparentDepthState = device.makeDepthStencilState(descriptor: transparent)!
        let hidden = MTLDepthStencilDescriptor()
        hidden.depthCompareFunction = .greater
        hidden.isDepthWriteEnabled = false
        hiddenDepthState = device.makeDepthStencilState(descriptor: hidden)!
        let prepass = MTLDepthStencilDescriptor()
        prepass.depthCompareFunction = .less
        prepass.isDepthWriteEnabled = true
        prepassDepthState = device.makeDepthStencilState(descriptor: prepass)!
        colorFormat = view.colorPixelFormat
        depthFormat = view.depthStencilPixelFormat

        let sd = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .depth32Float, width: Self.shadowMapSize,
                                                          height: Self.shadowMapSize, mipmapped: false)
        sd.usage = [.renderTarget, .shaderRead]
        sd.storageMode = .private
        guard let shadowMap = device.makeTexture(descriptor: sd) else { return nil }
        self.shadowMap = shadowMap
        let wd = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .r16Float, width: 1, height: 1, mipmapped: false)
        wd.usage = .shaderRead
        guard let white = device.makeTexture(descriptor: wd) else { return nil }
        var one: UInt16 = 0x3C00  // 1.0 as a half float
        white.replace(region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0, withBytes: &one, bytesPerRow: 2)
        whiteTexture = white
    }

    private func buffer<T>(_ array: [T]) -> MTLBuffer? {
        guard !array.isEmpty else { return nil }
        return array.withUnsafeBytes { device.makeBuffer(bytes: $0.baseAddress!, length: $0.count, options: .storageModeShared) }
    }

    private static func packed(_ v: [SIMD3<Float>]) -> [Float] {
        v.flatMap { [$0.x, $0.y, $0.z] }
    }

    func sync(meshes: [Int: BodyMesh], version: Int) {
        guard version != syncedVersion else { return }
        syncedVersion = version
        self.meshes = meshes
        let previous = bodies
        bodies = meshes.compactMapValues { m in
            // Unchanged geometry keeps its buffers (and smoothed normals).
            if m.shapeKey != 0, let old = previous[m.id], old.shapeKey == m.shapeKey { return old }
            guard let p = buffer(Self.packed(m.positions)), let n = buffer(Self.packed(m.normals)),
                  let f = buffer(m.vertexFace), let i = buffer(m.indices) else { return nil }
            return GPUBody(positions: p, normals: n, faceIds: f, indices: i, indexCount: m.indices.count,
                           edgePositions: buffer(Self.packed(m.edgePositions)), edgeIds: buffer(m.edgeIds),
                           edgeVertexCount: m.edgeIds.count, boundsMin: m.boundsMin, boundsMax: m.boundsMax, shapeKey: m.shapeKey)
        }
    }

    // MARK: Environment

    /// Bottom and top of the background gradient.
    static func backgroundColors(_ env: EnvironmentSettings) -> (bottom: SIMD3<Float>, top: SIMD3<Float>) {
        let bg = simd_clamp(env.background, SIMD3(repeating: 0), SIMD3(repeating: 1))
        guard env.gradient else { return (bg, bg) }
        return (bg * 0.94, simd_min(bg * 1.08 + 0.03, SIMD3(repeating: 1)))
    }

    static func clearColor(_ env: EnvironmentSettings) -> MTLClearColor {
        let c = env.background
        return MTLClearColor(red: Double(c.x), green: Double(c.y), blue: Double(c.z), alpha: 1)
    }

    /// The key light sits 50° above the ground, `lightAzimuth` degrees to the
    /// right of the viewer, so models stay well lit while orbiting.
    static func keyLight(camera: Camera, azimuth degrees: Float, elevation elevationDegrees: Float = 50) -> SIMD3<Float> {
        var back = SIMD3<Float>(-camera.forward.x, -camera.forward.y, 0)
        if simd_length(back) < 1e-3 { back = SIMD3(-camera.up.x, -camera.up.y, 0) }
        back = simd_normalize(back)
        let side = simd_normalize(SIMD3<Float>(back.y, -back.x, 0))  // viewer's right
        let a = degrees * .pi / 180, elevation = min(max(elevationDegrees, 5), 89) * .pi / 180
        let horizontal = back * cos(a) + side * sin(a)
        return simd_normalize(horizontal * cos(elevation) + SIMD3(0, 0, sin(elevation)))
    }

    // MARK: Drawing

    func draw(in view: MTKView, scene: SceneState) {
        view.clearColor = Self.clearColor(scene.environment)
        guard let pass = view.currentRenderPassDescriptor, let drawable = view.currentDrawable,
              let commands = queue.makeCommandBuffer() else { return }
        pass.colorAttachments[0].clearColor = Self.clearColor(scene.environment)
        let pixels = (Int(view.drawableSize.width), Int(view.drawableSize.height))
        var frame = frameUniforms(scene: scene, size: view.bounds.size, tile: [1, 1, 0, 0])
        encodePrepasses(commands, scene: scene, frame: &frame, pixels: pixels)
        guard let encoder = commands.makeRenderCommandEncoder(descriptor: pass) else { return }
        encode(encoder, scene: scene, frame: frame)
        commands.present(drawable)
        commands.commit()
    }

    /// Renders one frame off screen at `size` points × `scale` for screenshots
    /// and captures. Large images (e.g. 4× "Capture") are rendered in tiles of
    /// at most `tileSize` pixels, so any scale up to `maxImageSide` works.
    func image(scene: SceneState, size: CGSize, scale: CGFloat) -> CGImage? {
        let scale = min(scale, Self.maxImageSide / max(size.width, size.height, 1))
        let w = Int((size.width * scale).rounded()), h = Int((size.height * scale).rounded())
        guard w > 0, h > 0 else { return nil }
        let tw = min(w, Self.tileSize), th = min(h, Self.tileSize)
        func texture(_ format: MTLPixelFormat, samples: Int, usage: MTLTextureUsage) -> MTLTexture? {
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format, width: tw, height: th, mipmapped: false)
            d.textureType = samples > 1 ? .type2DMultisample : .type2D
            d.sampleCount = samples
            d.usage = usage
            d.storageMode = samples > 1 || format == depthFormat ? .private : .shared
            return device.makeTexture(descriptor: d)
        }
        guard let msaa = texture(colorFormat, samples: Self.sampleCount, usage: .renderTarget),
              let resolve = texture(colorFormat, samples: 1, usage: [.renderTarget, .shaderRead]),
              let depth = texture(depthFormat, samples: Self.sampleCount, usage: .renderTarget) else { return nil }
        let pass = MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture = msaa
        pass.colorAttachments[0].resolveTexture = resolve
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].clearColor = Self.clearColor(scene.environment)
        pass.colorAttachments[0].storeAction = .multisampleResolve
        pass.depthAttachment.texture = depth
        pass.depthAttachment.loadAction = .clear
        pass.depthAttachment.clearDepth = 1
        pass.depthAttachment.storeAction = .dontCare

        var bytes = [UInt8](repeating: 0, count: w * h * 4)
        for y0 in stride(from: 0, to: h, by: th) {
            for x0 in stride(from: 0, to: w, by: tw) {
                let cw = min(tw, w - x0), ch = min(th, h - y0)
                // This tile's part of the full image in NDC (y up), and the
                // clip-space transform that stretches it over the viewport.
                let nx0 = Float(x0) / Float(w) * 2 - 1, nx1 = Float(x0 + cw) / Float(w) * 2 - 1
                let ny1 = 1 - Float(y0) / Float(h) * 2, ny0 = 1 - Float(y0 + ch) / Float(h) * 2
                let sx = 2 / (nx1 - nx0), sy = 2 / (ny1 - ny0)
                let tile = SIMD4<Float>(sx, sy, -(nx1 + nx0) / (nx1 - nx0), -(ny1 + ny0) / (ny1 - ny0))
                guard let commands = queue.makeCommandBuffer() else { return nil }
                var frame = frameUniforms(scene: scene, size: size, tile: tile)
                encodePrepasses(commands, scene: scene, frame: &frame, pixels: (cw, ch))
                guard let encoder = commands.makeRenderCommandEncoder(descriptor: pass) else { return nil }
                encoder.setViewport(MTLViewport(originX: 0, originY: 0, width: Double(cw), height: Double(ch), znear: 0, zfar: 1))
                encode(encoder, scene: scene, frame: frame)
                commands.commit()
                commands.waitUntilCompleted()
                bytes.withUnsafeMutableBytes { raw in
                    resolve.getBytes(raw.baseAddress! + (y0 * w + x0) * 4, bytesPerRow: w * 4,
                                     from: MTLRegionMake2D(0, 0, cw, ch), mipmapLevel: 0)
                }
            }
        }
        // BGRA → premultiplied little-endian ARGB, which is BGRA in memory.
        let info = CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue | CGBitmapInfo.byteOrder32Little.rawValue)
        guard let provider = CGDataProvider(data: Data(bytes) as CFData) else { return nil }
        return CGImage(width: w, height: h, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: w * 4,
                       space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: info, provider: provider,
                       decode: nil, shouldInterpolate: true, intent: .defaultIntent)
    }

    private func camera(for scene: SceneState) -> Camera {
        var camera = scene.camera
        if !camera.matches(scene.cameraSettings) { camera.settings = scene.cameraSettings }
        return camera
    }

    private func frameUniforms(scene: SceneState, size: CGSize, tile: SIMD4<Float>) -> FrameUniforms {
        let camera = camera(for: scene)
        let env = scene.environment
        let background = Self.backgroundColors(env)

        var frame = FrameUniforms()
        frame.viewProj = camera.viewProjection(size: size)
        frame.eye = camera.eye
        frame.clipPlane = scene.clip ?? .zero
        frame.clipEnabled = scene.clip == nil ? 0 : 1
        frame.orthographic = camera.orthographic ? 1 : 0
        frame.lightIntensity = max(env.lightIntensity, 0)
        frame.shadowStrength = env.groundShadow ? 1 : 0
        frame.viewDir = camera.forward
        frame.keyLight = Self.keyLight(camera: camera, azimuth: env.lightAzimuth, elevation: env.lightElevation)
        frame.bgBottom = background.bottom
        frame.bgTop = background.top
        frame.tile = tile
        let lo = bodies.values.map(\.boundsMin).reduce(SIMD3<Float>(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
        let hi = bodies.values.map(\.boundsMax).reduce(SIMD3<Float>(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
        frame.modelSize = bodies.isEmpty ? 100 : max(simd_length(hi - lo), 1)
        frame.rendered = scene.displayMode == .visualized ? 1 : 0
        return frame
    }

    private func encode(_ encoder: MTLRenderCommandEncoder, scene: SceneState, frame: FrameUniforms) {
        var frame = frame
        let camera = camera(for: scene)
        let env = scene.environment
        encoder.setVertexBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
        encoder.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
        encoder.setFrontFacing(.counterClockwise)
        encoder.setCullMode(.none)

        // Background gradient.
        encoder.setDepthStencilState(overlayDepthState)
        encoder.setRenderPipelineState(backgroundPipeline)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)

        let surfaces = scene.displayMode != .wireframe
        if env.groundShadow && surfaces && scene.displayMode != .xray {
            drawShadows(encoder, scene: scene, camera: camera, key: frame.keyLight,
                        scale: frame.shadowsEnabled != 0 ? 0.55 : 1)
        }
        encoder.setFragmentTexture(frame.aoEnabled != 0 ? aoTargets?.blurred : whiteTexture, index: 0)
        encoder.setFragmentTexture(shadowMap, index: 1)
        if frame.shadowsEnabled != 0 { drawShadowCatcher(encoder, scene: scene, camera: camera) }

        encoder.setDepthStencilState(depthState)
        drawLines(scene.grid, encoder: encoder)

        // Opaque bodies first, then reference images, then translucent
        // bodies (glass, the ghost) so they blend over both.
        func late(_ id: Int) -> Bool { id == scene.ghostBody || (scene.appearance[id]?.opacity ?? 1) < 1 }
        let order = bodies.keys.sorted()
        for id in order where !late(id) { drawBody(id, encoder: encoder, scene: scene, frame: &frame) }
        drawImages(scene.images, encoder: encoder)
        for id in order where late(id) { drawBody(id, encoder: encoder, scene: scene, frame: &frame) }
        if scene.showHiddenEdges && scene.displayMode != .wireframe {
            for id in order { drawHiddenEdges(id, encoder: encoder, frame: &frame) }
        }

        encoder.setDepthStencilState(overlayDepthState)
        drawLines(scene.overlay, encoder: encoder)
        encoder.endEncoding()
    }

    private func drawBody(_ id: Int, encoder: MTLRenderCommandEncoder, scene: SceneState, frame: inout FrameUniforms) {
        guard let body = bodies[id] else { return }
        let faceSel = scene.selection.compactMap { if case let .face(b, i) = $0, b == id { return Int32(i) } else { return nil } }
        let edgeSel = scene.selection.compactMap { if case let .edge(b, i) = $0, b == id { return Int32(i) } else { return nil } }
        let bodySelected = scene.selection.contains(.body(id))
        let ghost = id == scene.ghostBody

        var faceHover: Int32 = -1, edgeHover: Int32 = -1
        switch scene.hover {
        case let .face(b, i)? where b == id: faceHover = Int32(i)
        case let .edge(b, i)? where b == id: edgeHover = Int32(i)
        default: break
        }

        let look = scene.appearance[id] ?? Appearance()
        let xray = scene.displayMode == .xray
        var uniforms = BodyUniforms()
        uniforms.baseColor = SIMD4(look.color, 1)
        uniforms.highlightColor = [1.0, 0.55, 0.12, 1]
        uniforms.hoverColor = [0.25, 0.55, 1.0, 1]
        uniforms.bodySelected = bodySelected ? 1 : 0
        uniforms.hoverId = faceHover
        uniforms.selectedCount = Int32(faceSel.count)
        uniforms.ghost = ghost ? 1 : 0
        uniforms.metallic = simd_clamp(look.metallic, 0, 1)
        uniforms.roughness = simd_clamp(look.roughness, 0, 1)
        uniforms.opacity = xray ? 0.22 : simd_clamp(look.opacity, 0, 1)
        uniforms.flat = xray ? 1 : 0
        uniforms.shading = scene.displayMode == .zebra ? 1 : scene.displayMode == .curvature ? 2 : 0

        if scene.displayMode != .wireframe {
            encoder.setDepthStencilState(uniforms.opacity < 1 || ghost ? transparentDepthState : depthState)
            encoder.setRenderPipelineState(surfacePipeline)
            encoder.setDepthBias(1, slopeScale: 1.5, clamp: 0)
            encoder.setVertexBuffer(body.positions, offset: 0, index: Int(BufferPositions))
            encoder.setVertexBuffer(normals(id, scene: scene) ?? body.normals, offset: 0, index: Int(BufferNormals))
            encoder.setVertexBuffer(body.faceIds, offset: 0, index: Int(BufferIds))
            encoder.setFragmentBytes(&uniforms, length: MemoryLayout<BodyUniforms>.stride, index: Int(FragmentBody))
            var sel = faceSel.isEmpty ? [Int32(-1)] : faceSel
            encoder.setFragmentBytes(&sel, length: sel.count * 4, index: Int(FragmentSelected))
            encoder.drawIndexedPrimitives(type: .triangle, indexCount: body.indexCount, indexType: .uint32,
                                          indexBuffer: body.indices, indexBufferOffset: 0)
            encoder.setDepthBias(0, slopeScale: 0, clamp: 0)
            encoder.setDepthStencilState(depthState)
        }

        // Edges follow View ▸ Show Edges; selected or hovered ones always show.
        let edgesOn = scene.displayMode == .wireframe || (scene.showEdges && scene.displayMode.drawsEdgesByDefault)
        let showEdges = edgesOn || !edgeSel.isEmpty || edgeHover >= 0
        guard showEdges, let ep = body.edgePositions, let ei = body.edgeIds else { return }
        uniforms.baseColor = ghost ? [0.2, 0.22, 0.26, 0.35] : [0.16, 0.18, 0.22, 1]
        uniforms.hoverId = edgeHover
        uniforms.selectedCount = Int32(edgeSel.count)
        var esel = edgeSel.isEmpty ? [Int32(-1)] : edgeSel
        encoder.setRenderPipelineState(edgePipeline)
        encoder.setVertexBuffer(ep, offset: 0, index: Int(BufferPositions))
        encoder.setVertexBuffer(ei, offset: 0, index: Int(BufferIds))
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<BodyUniforms>.stride, index: Int(BufferBody))
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<BodyUniforms>.stride, index: Int(FragmentBody))
        encoder.setFragmentBytes(&esel, length: esel.count * 4, index: Int(FragmentSelected))
        encoder.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
        encoder.drawPrimitives(type: .line, vertexStart: 0, vertexCount: body.edgeVertexCount)
    }

    /// Edges behind surfaces, drawn faint where the depth test says they're covered.
    private func drawHiddenEdges(_ id: Int, encoder: MTLRenderCommandEncoder, frame: inout FrameUniforms) {
        guard let body = bodies[id], let ep = body.edgePositions, let ei = body.edgeIds else { return }
        var uniforms = BodyUniforms()
        uniforms.baseColor = [0.25, 0.28, 0.34, 0.38]
        uniforms.hoverId = -1
        uniforms.selectedCount = 0
        var none: [Int32] = [-1]
        encoder.setDepthStencilState(hiddenDepthState)
        encoder.setRenderPipelineState(edgePipeline)
        encoder.setVertexBuffer(ep, offset: 0, index: Int(BufferPositions))
        encoder.setVertexBuffer(ei, offset: 0, index: Int(BufferIds))
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<BodyUniforms>.stride, index: Int(BufferBody))
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<BodyUniforms>.stride, index: Int(FragmentBody))
        encoder.setFragmentBytes(&none, length: 4, index: Int(FragmentSelected))
        encoder.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
        encoder.drawPrimitives(type: .line, vertexStart: 0, vertexCount: body.edgeVertexCount)
        encoder.setDepthStencilState(depthState)
    }

    /// Soft contact shadows: a blurred rounded footprint per body on the
    /// ground (z = 0, or lower when a body reaches below it). Bodies higher
    /// above the ground cast wider, fainter shadows, nudged away from the key light.
    private func drawShadows(_ encoder: MTLRenderCommandEncoder, scene: SceneState, camera: Camera, key: SIMD3<Float>,
                             scale: Float = 1) {
        let casters = bodies.filter { $0.key != scene.ghostBody && (scene.appearance[$0.key]?.opacity ?? 1) > 0.05 }
        guard !casters.isEmpty else { return }
        let ground = min(0, casters.values.map(\.boundsMin.z).min() ?? 0)
        // Seen from below the ground the shadow would only get in the way.
        if camera.orthographic ? camera.forward.z >= -0.02 : camera.eye.z <= ground { return }
        let away = -SIMD2(key.x, key.y) / max(simd_length(SIMD2(key.x, key.y)), 1e-4)
        encoder.setDepthStencilState(transparentDepthState)
        encoder.setRenderPipelineState(shadowPipeline)
        for (id, body) in casters {
            let lo = body.boundsMin, hi = body.boundsMax
            let half = SIMD2(hi.x - lo.x, hi.y - lo.y) / 2
            let size = max(simd_reduce_max(hi - lo), 1e-3)
            let height = max(lo.z - ground, 0)
            let lift = height / size
            var strength: Float = 0.5 * exp(-lift * 1.6) * scale
            // Translucent bodies cast lighter shadows.
            strength *= 0.35 + 0.65 * min(scene.appearance[id]?.opacity ?? 1, 1)
            guard strength > 0.02 else { continue }
            let tallness = min((hi.z - lo.z) / max(simd_reduce_max(half) * 2, 1e-3), 2)
            let center = SIMD2(lo.x + hi.x, lo.y + hi.y) / 2 + away * (height * 0.25 + (hi.z - lo.z) * 0.15 * tallness)
            var u = ShadowUniforms()
            u.rect = SIMD4(center.x, center.y, half.x, half.y)
            u.z = ground
            // Wide enough to show past the footprint, like a soft studio shadow.
            u.radius = size * 0.15 + height * 0.35 + (hi.z - lo.z) * 0.12
            u.strength = strength
            u.corner = min(half.x, half.y) * 0.6
            encoder.setVertexBytes(&u, length: MemoryLayout<ShadowUniforms>.stride, index: Int(BufferBody))
            encoder.setFragmentBytes(&u, length: MemoryLayout<ShadowUniforms>.stride, index: Int(FragmentBody))
            encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        }
    }

    // MARK: Visualized passes

    /// Bodies that block light and occlude: everything mostly opaque.
    private func solidIds(_ scene: SceneState) -> [Int] {
        bodies.keys.sorted().filter { $0 != scene.ghostBody && (scene.appearance[$0]?.opacity ?? 1) >= 0.6 }
    }

    /// Smoothed normals for Visualized mode, when Smooth Shading is on.
    private func normals(_ id: Int, scene: SceneState) -> MTLBuffer? {
        guard scene.displayMode == .visualized, scene.environment.smoothShading, var body = bodies[id] else { return nil }
        if let n = body.smoothNormals { return n }
        guard let mesh = meshes[id] else { return nil }
        body.smoothNormals = buffer(Self.packed(Self.smoothNormals(positions: mesh.positions, normals: mesh.normals)))
        bodies[id] = body
        return body.smoothNormals
    }

    /// Averages each vertex normal with those of coincident vertices on
    /// neighbouring faces that bend by less than `limit` degrees, so a
    /// revolved polyline shades like one smooth surface but sharp creases stay.
    static func smoothNormals(positions: [SIMD3<Float>], normals: [SIMD3<Float>], limit: Float = 42) -> [SIMD3<Float>] {
        guard positions.count == normals.count, !positions.isEmpty else { return normals }
        let lo = positions.reduce(SIMD3<Float>(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
        let hi = positions.reduce(SIMD3<Float>(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
        let q = max(simd_length(hi - lo), 1e-3) * 2e-5
        var groups: [SIMD3<Int32>: [Int32]] = [:]
        groups.reserveCapacity(positions.count / 2)
        for (i, p) in positions.enumerated() {
            let k = SIMD3<Int32>(Int32((p.x / q).rounded()), Int32((p.y / q).rounded()), Int32((p.z / q).rounded()))
            groups[k, default: []].append(Int32(i))
        }
        let cosLimit = cos(limit * .pi / 180)
        var out = normals
        for members in groups.values where members.count > 1 {
            for i in members {
                let ni = normals[Int(i)]
                var sum = SIMD3<Float>.zero
                for j in members where simd_dot(ni, normals[Int(j)]) > cosLimit { sum += normals[Int(j)] }
                let l = simd_length(sum)
                if l > 1e-6 { out[Int(i)] = sum / l }
            }
        }
        return out
    }

    /// Shadow map and ambient occlusion passes ahead of the main pass, for
    /// Visualized mode. Sets the matching fields of `frame`.
    private func encodePrepasses(_ commands: MTLCommandBuffer, scene: SceneState, frame: inout FrameUniforms,
                                 pixels: (Int, Int)) {
        let env = scene.environment
        guard scene.displayMode == .visualized else { return }
        let solids = solidIds(scene)
        guard !solids.isEmpty else { return }

        if env.castShadows {
            let lo = solids.compactMap { bodies[$0]?.boundsMin }.reduce(SIMD3<Float>(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
            var hi = solids.compactMap { bodies[$0]?.boundsMax }.reduce(SIMD3<Float>(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
            hi.z = max(hi.z, lo.z + 1e-3)
            let radius = max(simd_length(hi - lo) / 2, 1) * 1.08
            frame.lightViewProj = Self.lightMatrix(key: frame.keyLight, center: (lo + hi) / 2, radius: radius)
            let texel = radius * 2 / Float(Self.shadowMapSize)
            let soft = simd_clamp(env.shadowSoftness, 0, 1)
            frame.shadowRadius = (1.5 + soft * soft * 26) / Float(Self.shadowMapSize) * (4096 / Float(Self.shadowMapSize)).squareRoot()
            frame.shadowBias = texel * 2.5
            frame.shadowsEnabled = 1

            let pass = MTLRenderPassDescriptor()
            pass.depthAttachment.texture = shadowMap
            pass.depthAttachment.loadAction = .clear
            pass.depthAttachment.clearDepth = 1
            pass.depthAttachment.storeAction = .store
            if let e = commands.makeRenderCommandEncoder(descriptor: pass) {
                e.label = "Shadow map"
                e.setRenderPipelineState(shadowMapPipeline)
                e.setDepthStencilState(prepassDepthState)
                e.setCullMode(.none)
                e.setDepthBias(0, slopeScale: 1.5, clamp: 0)
                e.setVertexBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
                e.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
                for id in solids {
                    guard let b = bodies[id] else { continue }
                    e.setVertexBuffer(b.positions, offset: 0, index: Int(BufferPositions))
                    e.drawIndexedPrimitives(type: .triangle, indexCount: b.indexCount, indexType: .uint32,
                                            indexBuffer: b.indices, indexBufferOffset: 0)
                }
                e.endEncoding()
            }
        }

        if env.ambientOcclusion && env.occlusionStrength > 0, let t = aoTextures(width: pixels.0, height: pixels.1) {
            frame.aoEnabled = 1
            frame.aoStrength = simd_clamp(env.occlusionStrength, 0, 1)
            frame.aoRadius = frame.modelSize * 0.022
            let viewport = MTLViewport(originX: 0, originY: 0, width: Double(pixels.0), height: Double(pixels.1), znear: 0, zfar: 1)

            let g = MTLRenderPassDescriptor()
            g.colorAttachments[0].texture = t.position
            g.colorAttachments[0].loadAction = .clear
            g.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 0)
            g.colorAttachments[0].storeAction = .store
            g.colorAttachments[1].texture = t.normal
            g.colorAttachments[1].loadAction = .clear
            g.colorAttachments[1].storeAction = .store
            g.depthAttachment.texture = t.depth
            g.depthAttachment.loadAction = .clear
            g.depthAttachment.clearDepth = 1
            g.depthAttachment.storeAction = .dontCare
            if let e = commands.makeRenderCommandEncoder(descriptor: g) {
                e.label = "Occlusion G-buffer"
                e.setViewport(viewport)
                e.setRenderPipelineState(gbufferPipeline)
                e.setDepthStencilState(prepassDepthState)
                e.setCullMode(.none)
                e.setVertexBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
                e.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
                for id in solids {
                    guard let b = bodies[id] else { continue }
                    e.setVertexBuffer(b.positions, offset: 0, index: Int(BufferPositions))
                    e.setVertexBuffer(normals(id, scene: scene) ?? b.normals, offset: 0, index: Int(BufferNormals))
                    e.drawIndexedPrimitives(type: .triangle, indexCount: b.indexCount, indexType: .uint32,
                                            indexBuffer: b.indices, indexBufferOffset: 0)
                }
                e.endEncoding()
            }

            func fullscreen(_ target: MTLTexture, _ pipeline: MTLRenderPipelineState, _ label: String,
                            _ bind: (MTLRenderCommandEncoder) -> Void) {
                let d = MTLRenderPassDescriptor()
                d.colorAttachments[0].texture = target
                d.colorAttachments[0].loadAction = .dontCare
                d.colorAttachments[0].storeAction = .store
                guard let e = commands.makeRenderCommandEncoder(descriptor: d) else { return }
                e.label = label
                e.setViewport(viewport)
                e.setRenderPipelineState(pipeline)
                e.setFragmentBytes(&frame, length: MemoryLayout<FrameUniforms>.stride, index: Int(BufferFrame))
                bind(e)
                e.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
                e.endEncoding()
            }
            var size = SIMD2<Float>(Float(pixels.0), Float(pixels.1))
            fullscreen(t.raw, aoPipeline, "Occlusion") { e in
                e.setFragmentBytes(&size, length: MemoryLayout<SIMD2<Float>>.stride, index: Int(FragmentBody))
                e.setFragmentTexture(t.position, index: 0)
                e.setFragmentTexture(t.normal, index: 1)
            }
            fullscreen(t.blurred, blurPipeline, "Occlusion blur") { e in
                e.setFragmentTexture(t.raw, index: 0)
                e.setFragmentTexture(t.position, index: 1)
            }
        }
    }

    /// Screen buffers for occlusion, at least `width` × `height`.
    private func aoTextures(width: Int, height: Int) -> (position: MTLTexture, normal: MTLTexture, depth: MTLTexture, raw: MTLTexture, blurred: MTLTexture)? {
        if let t = aoTargets, t.position.width >= width, t.position.height >= height { return t }
        func make(_ format: MTLPixelFormat) -> MTLTexture? {
            let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: format, width: max(width, 1), height: max(height, 1), mipmapped: false)
            d.usage = [.renderTarget, .shaderRead]
            d.storageMode = .private
            return device.makeTexture(descriptor: d)
        }
        guard let p = make(.rgba32Float), let n = make(.rgba16Float), let d = make(.depth32Float),
              let r = make(.r16Float), let b = make(.r16Float) else { return nil }
        aoTargets = (p, n, d, r, b)
        return aoTargets
    }

    /// Orthographic view from the key light that covers a sphere.
    static func lightMatrix(key: SIMD3<Float>, center: SIMD3<Float>, radius: Float) -> simd_float4x4 {
        let f = -simd_normalize(key)
        let eye = center - f * radius * 2
        let upRef: SIMD3<Float> = abs(f.z) > 0.99 ? [0, 1, 0] : [0, 0, 1]
        let r = simd_normalize(simd_cross(f, upRef)), u = simd_cross(r, f)
        let view = simd_float4x4(rows: [
            SIMD4(r.x, r.y, r.z, -simd_dot(r, eye)),
            SIMD4(u.x, u.y, u.z, -simd_dot(u, eye)),
            SIMD4(-f.x, -f.y, -f.z, simd_dot(f, eye)),
            SIMD4(0, 0, 0, 1),
        ])
        // View-space z runs from -radius (nearest) to -3·radius (farthest).
        let near = radius * 0.9, far = radius * 3.1
        let proj = simd_float4x4(rows: [
            SIMD4(1 / radius, 0, 0, 0),
            SIMD4(0, 1 / radius, 0, 0),
            SIMD4(0, 0, -1 / (far - near), -near / (far - near)),
            SIMD4(0, 0, 0, 1),
        ])
        return proj * view
    }

    /// A floor under the model that shows the cast shadow and nothing else.
    private func drawShadowCatcher(_ encoder: MTLRenderCommandEncoder, scene: SceneState, camera: Camera) {
        let solids = solidIds(scene)
        let lo = solids.compactMap { bodies[$0]?.boundsMin }.reduce(SIMD3<Float>(repeating: .greatestFiniteMagnitude)) { simd_min($0, $1) }
        let hi = solids.compactMap { bodies[$0]?.boundsMax }.reduce(SIMD3<Float>(repeating: -.greatestFiniteMagnitude)) { simd_max($0, $1) }
        guard lo.x <= hi.x else { return }
        let ground = min(0, lo.z)
        if camera.orthographic ? camera.forward.z >= -0.02 : camera.eye.z <= ground { return }
        var rect = SIMD4<Float>((lo.x + hi.x) / 2, (lo.y + hi.y) / 2, simd_length(hi - lo) * 1.2, ground)
        encoder.setDepthStencilState(transparentDepthState)
        encoder.setRenderPipelineState(catcherPipeline)
        encoder.setVertexBytes(&rect, length: MemoryLayout<SIMD4<Float>>.stride, index: Int(BufferBody))
        encoder.setFragmentBytes(&rect, length: MemoryLayout<SIMD4<Float>>.stride, index: Int(FragmentBody))
        encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
    }

    private func drawImages(_ list: [ReferenceImage], encoder: MTLRenderCommandEncoder) {
        images.beginFrame()
        defer { images.endFrame() }
        guard !list.isEmpty else { return }
        encoder.setDepthStencilState(transparentDepthState)
        encoder.setRenderPipelineState(imagePipeline)
        for image in list where image.opacity > 0 {
            guard let texture = images.texture(for: image.png) else { continue }
            let plane = image.plane
            var u = ImageUniforms()
            u.origin = SIMD3<Float>(plane.origin)
            u.xAxis = SIMD3<Float>(simd_normalize(plane.xDir) * image.width / 2)
            u.yAxis = SIMD3<Float>(simd_normalize(plane.yDir) * image.height / 2)
            u.opacity = Float(min(max(image.opacity, 0), 1))
            encoder.setVertexBytes(&u, length: MemoryLayout<ImageUniforms>.stride, index: Int(BufferBody))
            encoder.setFragmentBytes(&u, length: MemoryLayout<ImageUniforms>.stride, index: Int(FragmentBody))
            encoder.setFragmentTexture(texture, index: 0)
            encoder.drawPrimitives(type: .triangleStrip, vertexStart: 0, vertexCount: 4)
        }
        encoder.setDepthStencilState(depthState)
    }

    private func drawLines(_ batch: LineBatch, encoder: MTLRenderCommandEncoder) {
        guard batch.vertexCount > 0, let p = buffer(batch.positions), let c = buffer(batch.colors) else { return }
        encoder.setRenderPipelineState(colorPipeline)
        encoder.setVertexBuffer(p, offset: 0, index: Int(BufferPositions))
        encoder.setVertexBuffer(c, offset: 0, index: Int(BufferColors))
        encoder.drawPrimitives(type: .line, vertexStart: 0, vertexCount: batch.vertexCount)
    }
}
