import CoreGraphics
import simd

/// Z-up orbit camera.
struct Camera {
    var target = SIMD3<Float>(0, 0, 10)
    var distance: Float = 220
    var yaw: Float = -.pi / 4      // around Z, 0 looks from +X
    var pitch: Float = 0.55        // elevation above the XY plane
    var fovY: Float = 40 * .pi / 180
    /// Parallel projection. The visible height at the target is the same as
    /// in perspective (2·distance·tan(fovY/2)), so switching keeps the scale.
    var orthographic = false

    /// The persisted camera options (projection and field of view).
    var settings: CameraSettings {
        get { CameraSettings(orthographic: orthographic, fieldOfView: fovY * 180 / .pi) }
        set {
            orthographic = newValue.orthographic
            fovY = min(max(newValue.fieldOfView, 5), 120) * .pi / 180
        }
    }

    /// Whether the camera already uses `s` (compare before assigning, so a
    /// @Published camera isn't republished on every store change).
    func matches(_ s: CameraSettings) -> Bool {
        orthographic == s.orthographic && abs(fovY * 180 / .pi - min(max(s.fieldOfView, 5), 120)) < 0.01
    }

    /// Half the visible height at the target distance.
    var halfHeight: Float { distance * tan(fovY / 2) }

    var forward: SIMD3<Float> {
        -SIMD3(cos(pitch) * cos(yaw), cos(pitch) * sin(yaw), sin(pitch))
    }

    var eye: SIMD3<Float> { target - forward * distance }

    var right: SIMD3<Float> { simd_normalize(simd_cross(forward, [0, 0, 1])) }
    var up: SIMD3<Float> { simd_cross(right, forward) }

    var viewMatrix: simd_float4x4 {
        let f = forward, r = right, u = up, e = eye
        return simd_float4x4(rows: [
            SIMD4(r.x, r.y, r.z, -simd_dot(r, e)),
            SIMD4(u.x, u.y, u.z, -simd_dot(u, e)),
            SIMD4(-f.x, -f.y, -f.z, simd_dot(f, e)),
            SIMD4(0, 0, 0, 1),
        ])
    }

    func projection(aspect: Float) -> simd_float4x4 {
        if orthographic {
            // Depth covers geometry behind the eye too, since zooming an
            // orthographic view moves the eye without changing what is seen.
            let near = -(distance * 20 + 2000), far = distance * 50 + 2000
            let y = 1 / halfHeight, x = y / aspect
            let z = -1 / (far - near)
            return simd_float4x4(rows: [
                SIMD4(x, 0, 0, 0),
                SIMD4(0, y, 0, 0),
                SIMD4(0, 0, z, -near / (far - near)),
                SIMD4(0, 0, 0, 1),
            ])
        }
        let near = max(distance * 0.005, 0.01), far = distance * 50 + 2000
        let y = 1 / tan(fovY / 2), x = y / aspect
        let z = far / (near - far)
        return simd_float4x4(rows: [
            SIMD4(x, 0, 0, 0),
            SIMD4(0, y, 0, 0),
            SIMD4(0, 0, z, z * near),
            SIMD4(0, 0, -1, 0),
        ])
    }

    func viewProjection(size: CGSize) -> simd_float4x4 {
        projection(aspect: Float(max(size.width, 1) / max(size.height, 1))) * viewMatrix
    }

    /// World-space ray through a point in view coordinates (origin top-left).
    func ray(at point: CGPoint, size: CGSize) -> (origin: SIMD3<Float>, direction: SIMD3<Float>) {
        let ndcX = Float(point.x / size.width) * 2 - 1
        let ndcY = 1 - Float(point.y / size.height) * 2
        let aspect = Float(size.width / max(size.height, 1))
        if orthographic {
            let h = halfHeight
            // Start well behind the eye so geometry behind it is still hit.
            let origin = eye + right * (ndcX * h * aspect) + up * (ndcY * h) - forward * (distance * 20 + 2000)
            return (origin, forward)
        }
        let t = tan(fovY / 2)
        let dir = simd_normalize(forward + right * (ndcX * t * aspect) + up * (ndcY * t))
        return (eye, dir)
    }

    /// Projects a world point to view coordinates; nil when behind the camera
    /// (perspective only — orthographic views see everything in front of and behind the eye).
    func project(_ p: SIMD3<Float>, size: CGSize) -> CGPoint? {
        let clip = viewProjection(size: size) * SIMD4(p, 1)
        guard clip.w > 0 else { return nil }
        let ndc = SIMD2(clip.x, clip.y) / clip.w
        return CGPoint(x: CGFloat(ndc.x + 1) / 2 * size.width, y: CGFloat(1 - ndc.y) / 2 * size.height)
    }

    /// World units covered by one point at the target distance.
    func worldPerPoint(size: CGSize) -> Float {
        2 * distance * tan(fovY / 2) / Float(max(size.height, 1))
    }

    mutating func orbit(dx: Float, dy: Float) {
        yaw -= dx * 0.008
        pitch = min(max(pitch + dy * 0.008, -1.55), 1.55)
    }

    mutating func pan(dx: Float, dy: Float, size: CGSize) {
        let s = worldPerPoint(size: size)
        target += (-right * dx + up * dy) * s
    }

    mutating func zoom(_ factor: Float) {
        distance = min(max(distance * factor, 0.5), 20000)
    }

    /// Frames a bounding box; `aspect` is width / height, so tall (portrait)
    /// views fit to their narrower horizontal field of view.
    mutating func fit(min lo: SIMD3<Float>, max hi: SIMD3<Float>, aspect: Float = 1) {
        guard lo.x <= hi.x, simd_reduce_max(simd_abs(lo)).isFinite, simd_reduce_max(simd_abs(hi)).isFinite else { return }
        // A window that hasn't been laid out yet reports a zero size.
        let aspect = aspect.isFinite && aspect > 0.01 ? aspect : 1
        target = (lo + hi) / 2
        let radius = max(simd_length(hi - lo) / 2, 1)
        if orthographic {
            distance = radius * 1.15 / (tan(fovY / 2) * min(1, aspect))
            return
        }
        let halfX = atan(tan(fovY / 2) * aspect)
        distance = radius / sin(min(fovY / 2, halfX)) * 1.15
    }

    enum Preset: String, CaseIterable { case iso, front, back, top, bottom, right, left }

    mutating func look(_ preset: Preset) {
        switch preset {
        case .iso: yaw = -.pi / 4; pitch = 0.55
        case .top: yaw = -.pi / 2; pitch = 1.55
        case .bottom: yaw = -.pi / 2; pitch = -1.55
        case .front: yaw = -.pi / 2; pitch = 0
        case .back: yaw = .pi / 2; pitch = 0
        case .right: yaw = 0; pitch = 0
        case .left: yaw = .pi; pitch = 0
        }
    }

    /// Snaps to the closest of the six orthographic directions.
    mutating func snapToNearestOrtho() {
        if abs(pitch) > .pi / 4 {
            pitch = pitch > 0 ? 1.55 : -1.55
        } else {
            pitch = 0
        }
        yaw = (yaw / (.pi / 2)).rounded() * (.pi / 2)
    }

    /// Turns the view by `steps` quarter turns around the vertical (yaw) or sideways (pitch).
    mutating func rotate(yawSteps: Float = 0, pitchSteps: Float = 0) {
        yaw += yawSteps * .pi / 2
        pitch = min(max(pitch + pitchSteps * .pi / 2, -1.55), 1.55)
    }

    /// Orients the camera to look straight at a sketch plane. On a plane
    /// facing up or down, the camera also turns so the plane's X axis points
    /// right on screen.
    mutating func face(normal n: SIMD3<Float>, origin: SIMD3<Float>, xDir: SIMD3<Float>) {
        let horizontal = SIMD2(n.x, n.y)
        pitch = min(max(asin(max(min(n.z, 1), -1)), -1.55), 1.55)
        if abs(n.z) > 0.99 {
            // right = (-sin yaw, cos yaw, 0)
            yaw = atan2(-xDir.x, xDir.y)
        } else if simd_length(horizontal) > 1e-4 {
            yaw = atan2(n.y, n.x)
        }
        target = origin
    }
}
