package com.higglerslab.sculpt3d

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.util.Log
import android.view.KeyEvent
import androidx.activity.ComponentActivity
import androidx.activity.OnBackPressedCallback
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContract
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.platform.LocalConfiguration
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import com.higglerslab.sculpt3d.core.SculptNative
import com.higglerslab.sculpt3d.session.DocumentSession
import com.higglerslab.sculpt3d.session.Documents
import com.higglerslab.sculpt3d.session.UiActionHandler
import com.higglerslab.sculpt3d.ui.Host
import com.higglerslab.sculpt3d.ui.HostRef
import com.higglerslab.sculpt3d.ui.LocalHost
import com.higglerslab.sculpt3d.ui.LocalWide
import com.higglerslab.sculpt3d.ui.SculptTheme
import com.higglerslab.sculpt3d.ui.TextFocus
import com.higglerslab.sculpt3d.ui.UiStates
import com.higglerslab.sculpt3d.ui.WelcomeScreen
import com.higglerslab.sculpt3d.ui.WelcomeState
import com.higglerslab.sculpt3d.ui.Workspace
import com.higglerslab.sculpt3d.viewport.SculptGLView
import java.io.File

private const val TAG = "Sculpt3D"

/** SAF "create document" with the MIME type chosen per call: input is (mime, file name). */
private class CreateDocumentAs : ActivityResultContract<Pair<String, String>, Uri?>() {
    override fun createIntent(context: Context, input: Pair<String, String>) =
        Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType(input.first)
            .putExtra(Intent.EXTRA_TITLE, input.second)
    override fun parseResult(resultCode: Int, intent: Intent?): Uri? = if (resultCode == android.app.Activity.RESULT_OK) intent?.data else null
}

class MainActivity : ComponentActivity(), Host {
    private var pendingCreate: ((Uri) -> Unit)? = null
    private var pendingOpen: ((Uri) -> Unit)? = null
    private var pendingTree: (() -> Unit)? = null

    private val createDoc = registerForActivityResult(CreateDocumentAs()) { uri ->
        val cb = pendingCreate
        pendingCreate = null
        if (uri != null) cb?.invoke(uri)
    }
    private val openDoc = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val cb = pendingOpen
        pendingOpen = null
        if (uri != null) cb?.invoke(uri)
    }
    private val openTree = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            Documents.setPrefString("quickExportFolder", uri.toString())
            pendingTree?.invoke()
        }
        pendingTree = null
    }

    private val uiHandler = UiActionHandler { session, action -> handleUi(session, action) }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        HostRef.host = this
        Documents.init(this)
        Documents.onSessionCreated = { s ->
            s.uiHandler = uiHandler
            WelcomeState.visible = false
        }
        Documents.sessions.forEach { it.uiHandler = uiHandler }
        handleIntent(intent)
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (!back()) {
                    isEnabled = false
                    onBackPressedDispatcher.onBackPressed()
                    isEnabled = true
                }
            }
        })
        setContent {
            SculptTheme {
                val widthDp = LocalConfiguration.current.screenWidthDp
                val wide = widthDp >= 700
                CompositionLocalProvider(LocalHost provides this, LocalWide provides wide, com.higglerslab.sculpt3d.ui.LocalRoomy provides (widthDp >= 1100)) {
                    androidx.compose.material3.Surface(color = androidx.compose.material3.MaterialTheme.colorScheme.background) {
                    val active = Documents.active
                    if (WelcomeState.visible || active == null) {
                        WelcomeScreen { r ->
                            Documents.open(r.uri)?.let { err ->
                                android.widget.Toast.makeText(this, err, android.widget.Toast.LENGTH_LONG).show()
                                Documents.removeRecent(r)
                            }
                        }
                    } else if (active != null) {
                        Workspace(active)
                    }
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    private fun handleIntent(intent: Intent?) {
        if (intent?.action == Intent.ACTION_VIEW) intent.data?.let { uri -> Documents.open(uri)?.let { Log.w(TAG, it) } }
    }

    private val glView get() = Documents.active?.renderTarget as? SculptGLView

    override fun onPause() {
        super.onPause()
        Documents.autosaveAll()
        glView?.onPause()
    }

    override fun onResume() {
        super.onResume()
        glView?.onResume()
    }

    /** Android Back: closes what is open, steps out of modes, then returns to the welcome page. */
    private fun back(): Boolean {
        if (WelcomeState.visible) return Documents.active != null && run { WelcomeState.visible = false; true }
        val s = Documents.active ?: return false
        val ui = UiStates.of(s)
        val st = s.state
        when {
            ui.showSearch -> ui.showSearch = false
            ui.editingDimension != null -> ui.editingDimension = null
            ui.openMenu != null || ui.showProject || ui.showSnap || ui.showGrid -> ui.closeTransient()
            st.pointPick != null -> s.action("pick.cancel")
            st.draft != null -> s.action("draft.cancel")
            st.sketch != null -> s.action("sketch.escape")
            st.view.measuring -> s.command("view.setMeasuring", "on" to false)
            st.view.visualizing -> s.action("visualize.exit")
            !st.selection.isEmpty -> s.action("edit.deselectAll")
            else -> {
                Documents.autosaveAll()
                WelcomeState.visible = true
            }
        }
        return true
    }

    // ---- Hardware keyboard ----

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        val s = Documents.active
        if (event.action == KeyEvent.ACTION_DOWN && s != null && !WelcomeState.visible && TextFocus.focused == 0) {
            if (hostShortcut(s, event)) return true
            val name = SculptGLView.keyName(event)
            if (name != null) {
                val ui = UiStates.of(s)
                if (name == "Escape" && (ui.showSearch || ui.editingDimension != null || ui.openMenu != null)) {
                    ui.showSearch = false
                    ui.editingDimension = null
                    ui.closeTransient()
                    return true
                }
                if (s.key(name, SculptGLView.modsFromMeta(event.metaState))) return true
            }
        }
        return super.dispatchKeyEvent(event)
    }

    /** Shortcuts the core leaves to the host (API.md "Keys ▸ Not handled"). */
    private fun hostShortcut(s: DocumentSession, e: KeyEvent): Boolean {
        val ctrl = e.isCtrlPressed || e.isMetaPressed
        val shift = e.isShiftPressed
        val alt = e.isAltPressed
        val ui = UiStates.of(s)
        when {
            ctrl && !shift && !alt && e.keyCode == KeyEvent.KEYCODE_F -> ui.showSearch = true
            ctrl && !shift && e.keyCode == KeyEvent.KEYCODE_S -> save(s)
            ctrl && shift && e.keyCode == KeyEvent.KEYCODE_S -> saveAs(s)
            ctrl && e.keyCode == KeyEvent.KEYCODE_O -> openDocument()
            ctrl && e.keyCode == KeyEvent.KEYCODE_N -> newDocument()
            ctrl && e.keyCode == KeyEvent.KEYCODE_W -> closeDocument(s)
            ctrl && shift && e.keyCode == KeyEvent.KEYCODE_E -> { ui.exportToSlicer = false; ui.showExport = true }
            ctrl && shift && e.keyCode == KeyEvent.KEYCODE_I -> s.action("file.importFile")
            ctrl && shift && e.keyCode == KeyEvent.KEYCODE_P -> s.action("file.importProject")
            ctrl && shift && e.keyCode == KeyEvent.KEYCODE_V -> ui.showVariables = true
            ctrl && alt && e.keyCode == KeyEvent.KEYCODE_P -> ui.showProperties = !ui.showProperties
            alt && !ctrl && e.keyCode == KeyEvent.KEYCODE_L -> { ui.showItems = false; ui.showHistory = false; ui.showProperties = false }
            else -> return false
        }
        return true
    }

    // ---- UI actions from the core ----

    private fun handleUi(s: DocumentSession, action: String) {
        val ui = UiStates.of(s)
        when (action) {
            "ui.showVariables", "insert.variable" -> ui.showVariables = true
            "ui.importImage" -> importImage(s, 0)
            "ui.importDecal" -> importImage(s, 1)
            "ui.importHeightMap" -> importImage(s, 2)
            "ui.importFile" -> importFile(s)
            "ui.importProject" -> importFile(s, project = true)
            "ui.importMaterial" -> importMaterial(s)
            "ui.sketchText" -> ui.showSketchText = true
            "view.screenshot" -> screenshot(s, 2f, "Sculpt3D Screenshot.png")
            "ui.search" -> ui.showSearch = true
            "ui.export" -> ui.showExport = true
            else -> when {
                action.startsWith("export.") -> export(s, action.removePrefix("export."))
                else -> toast(s, "“$action” isn't available on Android yet")
            }
        }
    }

    // ---- Host ----

    override fun newDocument() {
        Documents.newDocument()
        WelcomeState.visible = false
    }

    override fun openDocument() {
        pendingOpen = { uri ->
            Documents.open(uri)?.let { err -> android.widget.Toast.makeText(this, err, android.widget.Toast.LENGTH_LONG).show() }
            WelcomeState.visible = Documents.active == null
        }
        openDoc.launch(arrayOf("application/json", "application/octet-stream", "*/*"))
    }

    override fun save(session: DocumentSession) {
        if (session.uri == null || session.uri?.scheme == "file") return saveAs(session)
        Documents.save(session)?.let { toast(session, it) } ?: toast(session, "Saved ${session.displayName}")
    }

    override fun saveAs(session: DocumentSession) {
        pendingCreate = { uri -> Documents.save(session, uri)?.let { toast(session, it) } ?: toast(session, "Saved ${session.displayName}") }
        createDoc.launch("application/octet-stream" to "${session.displayName}.sculpt3d")
    }

    override fun closeDocument(session: DocumentSession) {
        Documents.autosaveAll()
        Documents.close(session)
        if (Documents.active == null) WelcomeState.visible = true
    }

    override fun showWelcome() {
        Documents.autosaveAll()
        WelcomeState.visible = true
    }

    /** Copies a picked document into the cache under its own name, so the core sees its extension. */
    private fun copyToCache(uri: Uri): File? = try {
        val name = Documents.displayName(uri).replace('/', '_')
        val dir = File(cacheDir, "import").apply { mkdirs() }
        val f = File(dir, name)
        contentResolver.openInputStream(uri)?.use { input -> f.outputStream().use { input.copyTo(it) } }
        f
    } catch (t: Throwable) {
        Log.e(TAG, "Copy failed", t)
        null
    }

    override fun importFile(session: DocumentSession, project: Boolean) {
        pendingOpen = { uri ->
            val f = copyToCache(uri)
            if (f == null) {
                toast(session, "Couldn't read the file")
            } else {
                val ext = f.extension.lowercase()
                val ok = if (project) ext in listOf("sculpt3d", "o3d") else ext in listOf("step", "stp", "iges", "igs", "stl", "brep", "brp", "sculpt3d", "o3d")
                if (!ok) {
                    toast(session, "Choose a ${if (project) ".sculpt3d project" else "STEP, IGES, STL or BREP file"}")
                } else {
                    val err = SculptNative.docImportPath(session.handle, f.absolutePath)
                    session.refresh()
                    toast(session, err ?: "Imported ${f.name}")
                }
            }
        }
        openDoc.launch(arrayOf("*/*"))
    }

    override fun importImage(session: DocumentSession, mode: Int) {
        pendingOpen = { uri ->
            val bytes = try { contentResolver.openInputStream(uri)?.use { it.readBytes() } } catch (t: Throwable) { null }
            if (bytes == null) {
                toast(session, "Couldn't read the image")
            } else {
                val err = SculptNative.docImportImageAs(session.handle, bytes, Documents.displayName(uri), mode)
                session.refresh()
                err?.let { toast(session, it) }
            }
        }
        openDoc.launch(arrayOf("image/*"))
    }

    override fun importMaterial(session: DocumentSession) {
        pendingOpen = { uri ->
            val text = try { contentResolver.openInputStream(uri)?.use { it.readBytes().toString(Charsets.UTF_8) } } catch (t: Throwable) { null }
            if (text == null) toast(session, "Couldn't read the material")
            else session.command("import.material", "json" to text, "fileName" to Documents.displayName(uri)).error?.let { toast(session, it) }
        }
        openDoc.launch(arrayOf("application/json", "*/*"))
    }

    private fun kindInfo(session: DocumentSession, kindId: String): Pair<String, String> {
        val k = session.state.export.kinds.firstOrNull { it.id == kindId || it.extension == kindId }
        return (k?.mimeType ?: "application/octet-stream") to (k?.extension ?: kindId)
    }

    private fun exportBytes(session: DocumentSession, kindId: String, options: String?): ByteArray? = try {
        SculptNative.docExport(session.handle, kindId, options)
    } catch (t: Throwable) {
        toast(session, t.message ?: "Export failed")
        null
    }

    override fun export(session: DocumentSession, kindId: String, options: String?, fileName: String?) {
        val bytes = exportBytes(session, kindId, options) ?: return
        val (mime, ext) = kindInfo(session, kindId)
        val name = fileName ?: "${session.displayName}.$ext"
        pendingCreate = { uri ->
            try {
                contentResolver.openOutputStream(uri, "wt")?.use { it.write(bytes) }
                toast(session, "Exported ${Documents.displayName(uri)}")
            } catch (t: Throwable) {
                toast(session, "Couldn't write the file: ${t.message}")
            }
        }
        createDoc.launch(mime to name)
    }

    override fun quickExportFolderName(): String? {
        val u = Documents.prefString("quickExportFolder") ?: return null
        return DocumentFile.fromTreeUri(this, Uri.parse(u))?.name
    }

    override fun chooseQuickExportFolder(then: (() -> Unit)?) {
        pendingTree = then
        openTree.launch(null)
    }

    override fun quickExport(session: DocumentSession, kinds: List<String>) {
        if (kinds.isEmpty()) return toast(session, "Pick favourite formats in File ▸ Export… first")
        val folder = Documents.prefString("quickExportFolder")?.let { DocumentFile.fromTreeUri(this, Uri.parse(it)) }
        if (folder == null || !folder.canWrite()) return chooseQuickExportFolder { quickExport(session, kinds) }
        val written = mutableListOf<String>()
        for (k in kinds) {
            val bytes = exportBytes(session, k, null) ?: continue
            val (mime, ext) = kindInfo(session, k)
            val name = "${session.displayName}.$ext"
            folder.findFile(name)?.delete()
            val f = folder.createFile(mime, name) ?: continue
            contentResolver.openOutputStream(f.uri, "wt")?.use { it.write(bytes) }
            written += ext
        }
        if (written.isNotEmpty()) toast(session, "Exported ${written.joinToString(", ")} to ${folder.name}")
    }

    private fun shareFile(name: String, bytes: ByteArray): Uri {
        val dir = File(cacheDir, "share").apply { mkdirs() }
        val f = File(dir, name)
        f.writeBytes(bytes)
        return FileProvider.getUriForFile(this, "$packageName.files", f)
    }

    override fun openInApp(session: DocumentSession, kindId: String) {
        val bytes = exportBytes(session, kindId, null) ?: return
        val (mime, ext) = kindInfo(session, kindId)
        val uri = shareFile("${session.displayName}.$ext", bytes)
        val view = Intent(Intent.ACTION_VIEW).setDataAndType(uri, mime).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        try {
            startActivity(Intent.createChooser(view, "Open in"))
        } catch (t: Throwable) {
            toast(session, "No app on this device opens .$ext files")
        }
    }

    override fun screenshot(session: DocumentSession, scale: Float, fileName: String) {
        val v = session.renderTarget as? SculptGLView ?: return toast(session, "The viewport isn't ready")
        v.snapshotPng(scale) { png ->
            if (png == null) return@snapshotPng toast(session, "Couldn't capture the view")
            pendingCreate = { uri ->
                contentResolver.openOutputStream(uri, "wt")?.use { it.write(png) }
                toast(session, "Saved ${Documents.displayName(uri)}")
            }
            createDoc.launch("image/png" to fileName)
        }
    }

    override fun viewInBrowser(session: DocumentSession) {
        val r = session.command("export.webPage", "title" to session.displayName)
        val html = r.string("html") ?: return toast(session, r.error ?: "Couldn't make the page")
        val name = (r.string("defaultName") ?: session.displayName).let { if (it.endsWith(".html")) it else "$it.html" }
        val uri = shareFile(name, html.toByteArray())
        try {
            startActivity(Intent(Intent.ACTION_VIEW).setDataAndType(uri, "text/html").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION))
        } catch (t: Throwable) {
            toast(session, "No browser can open the page")
        }
    }

    override fun copyText(text: String, label: String) {
        (getSystemService(CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText(label, text))
        Documents.active?.let { toast(it, "Copied") }
    }

    override fun toast(session: DocumentSession, text: String) {
        UiStates.of(session).localToast = text
    }
}
