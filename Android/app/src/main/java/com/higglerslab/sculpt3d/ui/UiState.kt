package com.higglerslab.sculpt3d.ui

import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.higglerslab.sculpt3d.model.Annotation
import com.higglerslab.sculpt3d.session.DocumentSession

/** Per-window UI flags (Swift `UIState`). */
class UiState {
    var showProject by mutableStateOf(false)
    var showItems by mutableStateOf(false)
    var showHistory by mutableStateOf(false)
    var showProperties by mutableStateOf(false)
    var showSearch by mutableStateOf(false)
    var openMenu: String? by mutableStateOf(null)
    var showSnap by mutableStateOf(false)
    var showGrid by mutableStateOf(false)
    var showVariables by mutableStateOf(false)
    var showExport by mutableStateOf(false)
    var exportToSlicer by mutableStateOf(false)
    var showDrawing by mutableStateOf(false)
    var showFieldOfView by mutableStateOf(false)
    var sketchTransform: String? by mutableStateOf(null)
    var showSketchText by mutableStateOf(false)
    var visualizeTab by mutableStateOf(0)
    var showMenu by mutableStateOf(false)
    var showMcp by mutableStateOf(false)
    var showDocuments by mutableStateOf(false)
    var editingDimension: Annotation? by mutableStateOf(null)
    var showVisualizeSheet by mutableStateOf(false)
    /** A message from the front end itself ("Exported Model.step"), shown like the core's toast. */
    var localToast: String? by mutableStateOf(null)
    /** Where the rail's tool icons are (root px), so the flyout sits beside them. */
    var railMenuBounds: androidx.compose.ui.geometry.Rect? by mutableStateOf(null)

    fun closeTransient() {
        showProject = false
        openMenu = null
        showSnap = false
        showGrid = false
        showSearch = false
    }
}

/** One UiState per open document. */
object UiStates {
    private val map = java.util.WeakHashMap<DocumentSession, UiState>()
    fun of(session: DocumentSession): UiState = map.getOrPut(session) { UiState() }
}

/** What the activity does for the UI: file pickers, save dialogs, sharing. */
interface Host {
    fun newDocument()
    fun openDocument()
    fun save(session: DocumentSession)
    fun saveAs(session: DocumentSession)
    fun closeDocument(session: DocumentSession)
    fun showWelcome()
    fun importFile(session: DocumentSession, project: Boolean = false)
    /** 0 reference image, 1 decal, 2 height map terrain. */
    fun importImage(session: DocumentSession, mode: Int)
    fun importMaterial(session: DocumentSession)
    /** `sculpt_doc_export(kind)` then a save dialog. */
    fun export(session: DocumentSession, kindId: String, options: String? = null, fileName: String? = null)
    fun quickExport(session: DocumentSession, kinds: List<String>)
    fun chooseQuickExportFolder(then: (() -> Unit)? = null)
    fun quickExportFolderName(): String?
    fun openInApp(session: DocumentSession, kindId: String)
    fun screenshot(session: DocumentSession, scale: Float, fileName: String)
    fun viewInBrowser(session: DocumentSession)
    fun copyText(text: String, label: String = "Sculpt3D")
    fun toast(session: DocumentSession, text: String)
}

val LocalHost = compositionLocalOf<Host> { error("No host") }
