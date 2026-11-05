// Draws the DMG window background: a soft gradient with a "drag to
// Applications" arrow between the two icon slots.
// Usage: swift scripts/dmg_background.swift <out.png> [scale]
import AppKit

let args = CommandLine.arguments
let out = URL(fileURLWithPath: args.count > 1 ? args[1] : "background.png")
let scale = CGFloat(args.count > 2 ? Double(args[2]) ?? 1 : 1)
let size = CGSize(width: 600, height: 400)

let w = Int(size.width * scale), h = Int(size.height * scale)
let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: w, pixelsHigh: h, bitsPerSample: 8,
                           samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                           colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
rep.size = size
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: rep)

let gradient = NSGradient(colors: [NSColor(red: 0.97, green: 0.98, blue: 1.0, alpha: 1),
                                   NSColor(red: 0.89, green: 0.92, blue: 0.97, alpha: 1)])!
gradient.draw(in: NSRect(origin: .zero, size: size), angle: -90)

// Arrow between the app (x 150) and Applications (x 450) slots, at icon height.
let y: CGFloat = size.height - 190
let arrow = NSBezierPath()
arrow.move(to: NSPoint(x: 245, y: y))
arrow.line(to: NSPoint(x: 336, y: y))  // stops at the head so the translucent parts don't overlap
arrow.lineWidth = 6
arrow.lineCapStyle = .round
let tint = NSColor(red: 0.16, green: 0.32, blue: 0.75, alpha: 0.55)
tint.setStroke()
arrow.stroke()
let head = NSBezierPath()
head.move(to: NSPoint(x: 362, y: y))
head.line(to: NSPoint(x: 338, y: y + 15))
head.line(to: NSPoint(x: 338, y: y - 15))
head.close()
tint.setFill()
head.fill()

let style = NSMutableParagraphStyle()
style.alignment = .center
let text = NSAttributedString(string: "Drag Sculpt3D to Applications to install", attributes: [
    .font: NSFont.systemFont(ofSize: 15, weight: .medium),
    .foregroundColor: NSColor(red: 0.22, green: 0.26, blue: 0.36, alpha: 1),
    .paragraphStyle: style,
])
text.draw(in: NSRect(x: 0, y: 70, width: size.width, height: 24))

NSGraphicsContext.restoreGraphicsState()
try! rep.representation(using: .png, properties: [:])!.write(to: out)
