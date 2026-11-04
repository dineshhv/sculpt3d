import SwiftUI

/// The brand mark and name used on the first page on both platforms.
struct BrandHeader: View {
    var markSize: CGFloat = 132

    var body: some View {
        VStack(spacing: 14) {
            Image("BrandMark")
                .resizable()
                .scaledToFit()
                .frame(width: markSize, height: markSize)
                .shadow(color: Color(red: 0.04, green: 0.1, blue: 0.36).opacity(0.22), radius: 14, y: 8)
            Text("Sculpt3D")
                .font(.system(size: markSize * 0.36, weight: .bold, design: .rounded))
                .foregroundStyle(Color(red: 0.02, green: 0.10, blue: 0.40))
        }
    }
}

/// Soft white-to-blue backdrop behind the first page.
struct BrandBackground: View {
    var body: some View {
        LinearGradient(colors: [.white, Color(red: 0.91, green: 0.93, blue: 0.97)], startPoint: .top, endPoint: .bottom)
    }
}

#if os(macOS)
import AppKit

/// The Mac's first page: the logo with New and Open on the left, recent
/// documents on the right. Shown at launch and whenever no document is open.
struct WelcomeView: View {
    @Environment(\.newDocument) private var newDocument
    @Environment(\.openDocument) private var openDocument
    @Environment(\.dismissWindow) private var dismissWindow
    @State private var recents: [URL] = []

    var body: some View {
        HStack(spacing: 0) {
            VStack(spacing: 28) {
                Spacer()
                BrandHeader()
                Text("Version \(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "")")
                    .font(.callout)
                    .foregroundStyle(.gray)
                VStack(spacing: 10) {
                    Button {
                        newDocument(Sculpt3DFile())
                    } label: {
                        Label("New Document", systemImage: "plus")
                            .frame(width: 220)
                    }
                    .buttonStyle(.borderedProminent)
                    .controlSize(.large)
                    .keyboardShortcut("n", modifiers: .command)

                    Button {
                        NSDocumentController.shared.openDocument(nil)
                    } label: {
                        Label("Open…", systemImage: "folder")
                            .frame(width: 220)
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                    .keyboardShortcut("o", modifiers: .command)
                }
                Spacer()
            }
            .frame(maxWidth: .infinity)
            .background(BrandBackground())

            recentList
                .frame(width: 280)
        }
        .frame(width: 780, height: 480)
        .onAppear { recents = NSDocumentController.shared.recentDocumentURLs }
    }

    private var recentList: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Recent")
                .font(.headline)
                .padding(.horizontal, 16)
                .padding(.top, 40)
                .padding(.bottom, 8)
            if recents.isEmpty {
                Text("Documents you open will appear here.")
                    .font(.callout)
                    .foregroundStyle(.gray)
                    .padding(.horizontal, 16)
                Spacer()
            } else {
                List(recents, id: \.self) { url in
                    Button {
                        Task { try? await openDocument(at: url) }
                    } label: {
                        HStack(spacing: 10) {
                            Image(nsImage: NSWorkspace.shared.icon(forFile: url.path))
                                .resizable()
                                .frame(width: 32, height: 32)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(url.deletingPathExtension().lastPathComponent)
                                    .lineLimit(1)
                                Text(url.deletingLastPathComponent().path.replacingOccurrences(of: NSHomeDirectory(), with: "~"))
                                    .font(.caption)
                                    .foregroundStyle(.gray)
                                    .lineLimit(1)
                                    .truncationMode(.middle)
                            }
                        }
                        .contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .padding(.vertical, 2)
                }
                .listStyle(.sidebar)
                .scrollContentBackground(.hidden)
            }
        }
        .frame(maxHeight: .infinity, alignment: .top)
        .background(.background)
    }
}
#endif
