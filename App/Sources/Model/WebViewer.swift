import Foundation
import SwiftUI
import UniformTypeIdentifiers

struct WebPageFile: FileDocument {
    static var readableContentTypes: [UTType] { [.html] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

/// File ▸ View in Browser / Publish Current Version: a single HTML page that
/// shows the model in 3D with its materials. The geometry is embedded, so the
/// page works on its own and can be shared or hosted anywhere.
extension ModelStore {
    func webPage(title: String) -> Data? {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("Sculpt3D-web-\(UUID().uuidString)")
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        var parts: [String] = []
        for id in visibleMeshes.keys.sorted() {
            let url = dir.appendingPathComponent("\(id).stl")
            guard kernel.exportFile(url.path, bodies: [NSNumber(value: id)]), let stl = try? Data(contentsOf: url) else { continue }
            let look = settings.appearance[id] ?? Appearance()
            let color = String(format: "#%02x%02x%02x", Int(look.color.x * 255), Int(look.color.y * 255), Int(look.color.z * 255))
            parts.append("""
            {"name":\(jsonString(settings.name(of: id))),"color":"\(color)","metalness":\(look.metallic),"roughness":\(look.roughness),"opacity":\(look.opacity),"stl":"\(stl.base64EncodedString())"}
            """)
        }
        guard !parts.isEmpty else {
            toast = "Nothing to show: the model has no visible bodies"
            return nil
        }
        let bg = settings.environment.background
        let background = String(format: "#%02x%02x%02x", Int(bg.x * 255), Int(bg.y * 255), Int(bg.z * 255))
        let html = """
        <!doctype html>
        <html lang="en">
        <head>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <title>\(escapeHTML(title)) — Sculpt3D</title>
        <style>
          html, body { margin: 0; height: 100%; background: \(background); font: 14px -apple-system, system-ui, sans-serif; color: #1c1f26; }
          canvas { display: block; width: 100%; height: 100%; }
          header { position: fixed; top: 16px; left: 16px; right: 16px; display: flex; justify-content: space-between; pointer-events: none; }
          header b { font-size: 16px; }
          header span { opacity: 0.6; }
        </style>
        <script type="importmap">{"imports":{"three":"https://cdn.jsdelivr.net/npm/three@0.160.0/build/three.module.js","three/addons/":"https://cdn.jsdelivr.net/npm/three@0.160.0/examples/jsm/"}}</script>
        </head>
        <body>
        <header><b>\(escapeHTML(title))</b><span>Made with Sculpt3D · drag to orbit, scroll to zoom</span></header>
        <canvas id="c"></canvas>
        <script type="module">
        import * as THREE from "three";
        import { STLLoader } from "three/addons/loaders/STLLoader.js";
        import { OrbitControls } from "three/addons/controls/OrbitControls.js";
        import { RoomEnvironment } from "three/addons/environments/RoomEnvironment.js";
        const parts = [\(parts.joined(separator: ","))];
        const canvas = document.getElementById("c");
        const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
        renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
        renderer.toneMapping = THREE.ACESFilmicToneMapping;
        const scene = new THREE.Scene();
        scene.background = new THREE.Color("\(background)");
        scene.environment = new THREE.PMREMGenerator(renderer).fromScene(new RoomEnvironment(), 0.04).texture;
        const camera = new THREE.PerspectiveCamera(35, 1, 0.1, 1e6);
        camera.up.set(0, 0, 1);
        const controls = new OrbitControls(camera, canvas);
        controls.enableDamping = true;
        const group = new THREE.Group();
        const loader = new STLLoader();
        for (const p of parts) {
          const bytes = Uint8Array.from(atob(p.stl), (c) => c.charCodeAt(0));
          const geometry = loader.parse(bytes.buffer);
          geometry.computeVertexNormals();
          const material = new THREE.MeshPhysicalMaterial({ color: p.color, metalness: p.metalness, roughness: p.roughness,
            transparent: p.opacity < 1, opacity: p.opacity, clearcoat: p.metalness > 0.5 ? 0 : 0.2 });
          const mesh = new THREE.Mesh(geometry, material);
          mesh.name = p.name;
          group.add(mesh);
        }
        scene.add(group);
        const box = new THREE.Box3().setFromObject(group), size = box.getSize(new THREE.Vector3()), center = box.getCenter(new THREE.Vector3());
        const r = size.length() / 2 || 1;
        controls.target.copy(center);
        camera.position.copy(center).add(new THREE.Vector3(1, -1.2, 0.8).normalize().multiplyScalar(r * 3));
        camera.near = r / 100; camera.far = r * 100; camera.updateProjectionMatrix();
        function resize() {
          const w = canvas.clientWidth, h = canvas.clientHeight;
          renderer.setSize(w, h, false);
          camera.aspect = w / h;
          camera.updateProjectionMatrix();
        }
        new ResizeObserver(resize).observe(canvas);
        renderer.setAnimationLoop(() => { controls.update(); renderer.render(scene, camera); });
        </script>
        </body>
        </html>
        """
        return Data(html.utf8)
    }

    /// Writes the page to a temporary file (for View in Browser).
    func webPageURL(title: String) -> URL? {
        guard let data = webPage(title: title) else { return nil }
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("Sculpt3D Viewer", isDirectory: true)
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        let url = dir.appendingPathComponent("\(title.replacingOccurrences(of: "/", with: "-")).html")
        do { try data.write(to: url) } catch { toast = "Couldn't write the web page"; return nil }
        return url
    }
}

private func jsonString(_ s: String) -> String {
    let data = (try? JSONSerialization.data(withJSONObject: [s])) ?? Data("[\"\"]".utf8)
    return String(decoding: data, as: UTF8.self).dropFirst().dropLast().description
}

private func escapeHTML(_ s: String) -> String {
    s.replacingOccurrences(of: "&", with: "&amp;").replacingOccurrences(of: "<", with: "&lt;").replacingOccurrences(of: ">", with: "&gt;")
}
