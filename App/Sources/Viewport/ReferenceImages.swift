import CoreGraphics
import ImageIO
import Metal

/// Decodes reference image PNGs into mipmapped Metal textures, cached by a
/// content fingerprint so a frame never re-decodes an unchanged image.
final class ReferenceImageCache {
    private struct Key: Hashable {
        let count: Int
        let digest: Int
    }

    private let device: MTLDevice
    private let queue: MTLCommandQueue
    private var textures: [Key: MTLTexture] = [:]
    /// Images that failed to decode, so they aren't retried every frame.
    private var failed: Set<Key> = []
    private var used: Set<Key> = []

    /// Longest texture side; larger images are scaled down.
    static let maxSide = 4096

    init(device: MTLDevice, queue: MTLCommandQueue) {
        self.device = device
        self.queue = queue
    }

    /// Cheap content fingerprint: length, ~4k bytes sampled across the file
    /// and the whole tail (compressed data plus CRCs), so two PNGs of the
    /// same size don't collide the way `Data.hashValue` (first bytes only) would.
    private static func key(_ data: Data) -> Key {
        var h = Hasher()
        h.combine(data.count)
        data.withUnsafeBytes { raw in
            let n = raw.count
            guard n > 0 else { return }
            let step = max(1, n / 4096)
            var i = 0
            while i < n {
                h.combine(raw[i])
                i += step
            }
            h.combine(bytes: UnsafeRawBufferPointer(rebasing: raw[max(0, n - 512)..<n]))
        }
        return Key(count: data.count, digest: h.finalize())
    }

    func beginFrame() { used.removeAll(keepingCapacity: true) }

    /// Drops textures for images no longer in the scene.
    func endFrame() {
        if textures.count > used.count { textures = textures.filter { used.contains($0.key) } }
    }

    func texture(for png: Data) -> MTLTexture? {
        let k = Self.key(png)
        used.insert(k)
        if let t = textures[k] { return t }
        guard !failed.contains(k) else { return nil }
        guard let t = makeTexture(png) else {
            failed.insert(k)
            return nil
        }
        textures[k] = t
        return t
    }

    /// RGBA8, premultiplied (CoreGraphics only draws premultiplied), first row
    /// = top of the image. The image shader un-premultiplies.
    private func makeTexture(_ data: Data) -> MTLTexture? {
        guard let source = CGImageSourceCreateWithData(data as CFData, nil),
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil) else { return nil }
        let fit = min(1, Double(Self.maxSide) / Double(max(image.width, image.height, 1)))
        let w = max(1, Int(Double(image.width) * fit)), h = max(1, Int(Double(image.height) * fit))
        var bytes = [UInt8](repeating: 0, count: w * h * 4)
        let drawn: Bool = bytes.withUnsafeMutableBytes { raw in
            guard let ctx = CGContext(data: raw.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w * 4,
                                      space: CGColorSpace(name: CGColorSpace.sRGB) ?? CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return false }
            ctx.interpolationQuality = .high
            ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
            return true
        }
        guard drawn else { return nil }
        let d = MTLTextureDescriptor.texture2DDescriptor(pixelFormat: .rgba8Unorm, width: w, height: h, mipmapped: true)
        d.usage = .shaderRead
        guard let texture = device.makeTexture(descriptor: d) else { return nil }
        texture.replace(region: MTLRegionMake2D(0, 0, w, h), mipmapLevel: 0, withBytes: bytes, bytesPerRow: w * 4)
        if texture.mipmapLevelCount > 1, let commands = queue.makeCommandBuffer(), let blit = commands.makeBlitCommandEncoder() {
            blit.generateMipmaps(for: texture)
            blit.endEncoding()
            commands.commit()
        }
        return texture
    }
}
