import MetalKit
import SwiftUI

#if os(iOS)
import UIKit

/// Touch, Apple Pencil and pointer input for iPhone and iPad.
final class TouchViewport: MTKView, UIGestureRecognizerDelegate {
    var controller: ViewportController?
    private var lastPan = CGPoint.zero
    private var drawing = false

    override init(frame: CGRect, device: MTLDevice?) {
        super.init(frame: frame, device: device)
        isMultipleTouchEnabled = true

        let doubleTap = UITapGestureRecognizer(target: self, action: #selector(onDoubleTap))
        doubleTap.numberOfTapsRequired = 2
        let tap = UITapGestureRecognizer(target: self, action: #selector(onTap))
        let drag = UIPanGestureRecognizer(target: self, action: #selector(onDrag))
        drag.maximumNumberOfTouches = 1
        let pan = UIPanGestureRecognizer(target: self, action: #selector(onPan))
        pan.minimumNumberOfTouches = 2
        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(onPinch))
        let hover = UIHoverGestureRecognizer(target: self, action: #selector(onHover))
        for g in [doubleTap, tap, drag, pan, pinch, hover] as [UIGestureRecognizer] {
            g.delegate = self
            addGestureRecognizer(g)
        }
    }

    required init(coder: NSCoder) { fatalError("init(coder:) is not supported") }

    override var canBecomeFirstResponder: Bool { true }

    override func didMoveToWindow() {
        super.didMoveToWindow()
        becomeFirstResponder()
    }

    /// Hardware keyboard shortcuts on iPad.
    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        var handled = false
        for press in presses {
            guard let key = press.key, key.modifierFlags.intersection([.command, .control, .alternate]).isEmpty else { continue }
            if controller?.handleKey(key.charactersIgnoringModifiers) == true { handled = true }
        }
        if !handled { super.pressesBegan(presses, with: event) }
    }

    func gestureRecognizer(_ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer) -> Bool {
        (g is UIPinchGestureRecognizer && other is UIPanGestureRecognizer) || (g is UIPanGestureRecognizer && other is UIPinchGestureRecognizer)
    }

    @objc private func onTap(_ g: UITapGestureRecognizer) {
        controller?.tap(at: g.location(in: self), additive: g.modifierFlags.contains(.shift))
    }

    @objc private func onDoubleTap(_ g: UITapGestureRecognizer) {
        controller?.doubleTap(at: g.location(in: self), additive: g.modifierFlags.contains(.shift))
    }

    @objc private func onDrag(_ g: UIPanGestureRecognizer) {
        guard let controller else { return }
        let p = g.location(in: self)
        switch g.state {
        case .began:
            lastPan = g.translation(in: self)
            let start = CGPoint(x: p.x - lastPan.x, y: p.y - lastPan.y)
            drawing = controller.dragDraws(at: start)
            if drawing { controller.drawBegan(at: start) }
        case .changed:
            if drawing {
                controller.drawChanged(at: p)
            } else {
                let t = g.translation(in: self)
                controller.orbit(dx: t.x - lastPan.x, dy: t.y - lastPan.y)
                lastPan = t
            }
        case .ended, .cancelled:
            if drawing { controller.drawEnded(at: p) }
            drawing = false
        default:
            break
        }
    }

    @objc private func onPan(_ g: UIPanGestureRecognizer) {
        let t = g.translation(in: self)
        controller?.pan(dx: t.x, dy: t.y)
        g.setTranslation(.zero, in: self)
    }

    @objc private func onPinch(_ g: UIPinchGestureRecognizer) {
        guard g.state == .changed, g.scale > 0 else { return }
        controller?.zoom(1 / g.scale, at: g.location(in: self))
        g.scale = 1
    }

    @objc private func onHover(_ g: UIHoverGestureRecognizer) {
        controller?.hover(at: g.state == .ended ? nil : g.location(in: self))
    }
}

struct ViewportView: UIViewRepresentable {
    let controller: ViewportController

    func makeUIView(context: Context) -> TouchViewport {
        let view = TouchViewport(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.controller = controller
        controller.attach(view)
        return view
    }

    func updateUIView(_ view: TouchViewport, context: Context) {
        controller.redraw()
    }
}

#else
import AppKit

/// Mouse, trackpad and keyboard input for the Mac.
final class MouseViewport: MTKView {
    var controller: ViewportController?
    private var downPoint = CGPoint.zero
    private var dragging = false
    private var drawing = false

    override var acceptsFirstResponder: Bool { true }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
                                       owner: self))
    }

    private func point(_ event: NSEvent) -> CGPoint {
        let p = convert(event.locationInWindow, from: nil)
        return CGPoint(x: p.x, y: bounds.height - p.y)
    }

    override func mouseMoved(with event: NSEvent) { controller?.hover(at: point(event)) }

    override func keyDown(with event: NSEvent) {
        let plain = event.modifierFlags.intersection([.command, .control, .option]).isEmpty
        if plain, let chars = event.charactersIgnoringModifiers, controller?.handleKey(chars) == true { return }
        super.keyDown(with: event)
    }
    override func mouseExited(with event: NSEvent) { controller?.hover(at: nil) }

    override func mouseDown(with event: NSEvent) {
        window?.makeFirstResponder(self)
        downPoint = point(event)
        dragging = false
        drawing = false
    }

    override func mouseDragged(with event: NSEvent) {
        guard let controller else { return }
        let p = point(event)
        if !dragging {
            guard hypot(p.x - downPoint.x, p.y - downPoint.y) > 3 else { return }
            dragging = true
            drawing = controller.dragDraws(at: downPoint)
            if drawing { controller.drawBegan(at: downPoint) }
        }
        if drawing {
            controller.drawChanged(at: p)
        } else if event.modifierFlags.contains(.option) || event.modifierFlags.contains(.shift) {
            controller.pan(dx: event.deltaX, dy: event.deltaY)
        } else {
            controller.orbit(dx: event.deltaX, dy: event.deltaY)
        }
    }

    override func mouseUp(with event: NSEvent) {
        guard let controller else { return }
        let additive = event.modifierFlags.contains(.shift) || event.modifierFlags.contains(.command)
        if drawing {
            controller.drawEnded(at: point(event))
        } else if !dragging {
            if event.clickCount == 2 {
                controller.doubleTap(at: point(event), additive: additive)
            } else {
                controller.tap(at: point(event), additive: additive)
            }
        }
        dragging = false
        drawing = false
    }

    override func rightMouseDragged(with event: NSEvent) {
        controller?.orbit(dx: event.deltaX, dy: event.deltaY)
    }

    override func otherMouseDragged(with event: NSEvent) {
        controller?.pan(dx: event.deltaX, dy: event.deltaY)
    }

    override func scrollWheel(with event: NSEvent) {
        guard let controller else { return }
        if event.hasPreciseScrollingDeltas {
            // Trackpad: two-finger scroll orbits, with shift it pans.
            if event.modifierFlags.contains(.shift) {
                controller.pan(dx: -event.scrollingDeltaX, dy: -event.scrollingDeltaY)
            } else {
                controller.orbit(dx: -event.scrollingDeltaX, dy: -event.scrollingDeltaY)
            }
        } else {
            controller.zoom(pow(1.1, -event.scrollingDeltaY / 3), at: point(event))
        }
    }

    override func magnify(with event: NSEvent) {
        controller?.zoom(1 / (1 + event.magnification), at: point(event))
    }
}

struct ViewportView: NSViewRepresentable {
    let controller: ViewportController

    func makeNSView(context: Context) -> MouseViewport {
        let view = MouseViewport(frame: .zero, device: MTLCreateSystemDefaultDevice())
        view.controller = controller
        controller.attach(view)
        return view
    }

    func updateNSView(_ view: MouseViewport, context: Context) {
        controller.redraw()
    }
}
#endif
