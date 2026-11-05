package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowBack
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.outlined.Check
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.session.DocumentSession
import com.higglerslab.sculpt3d.session.Documents

/** A menu entry: a command, a submenu, a toggle or a separator. */
private sealed interface Entry
private data class Cmd(val title: String, val enabled: Boolean = true, val keys: String? = null, val checked: Boolean? = null, val run: () -> Unit) : Entry
private data class Sub(val title: String, val entries: () -> List<Entry>) : Entry
private data object Sep : Entry
private data class Header(val title: String) : Entry

/**
 * The app menu (the Mac menu bar / iPad menus, Swift ModelCommands): File, Edit, Item,
 * Sketch, Insert, Construct, Transform, Tools and View, as nested pages of one dropdown.
 */
@Composable
fun MainMenu(session: DocumentSession, ui: UiState) {
    val stack = remember { mutableStateListOf<Sub>() }
    val close = { ui.showMenu = false; stack.clear() }
    DropdownMenu(ui.showMenu, close, Modifier.widthIn(min = 260.dp).heightIn(max = 560.dp)) {
        val page: List<Entry> = stack.lastOrNull()?.entries?.invoke() ?: rootEntries(session, ui)
        stack.lastOrNull()?.let { sub ->
            DropdownMenuItem(
                text = { Text(sub.title, fontWeight = FontWeight.SemiBold) },
                leadingIcon = { Icon(Icons.AutoMirrored.Outlined.ArrowBack, "Back") },
                onClick = { stack.removeAt(stack.size - 1) },
            )
            HorizontalDivider()
        }
        page.forEach { e ->
            when (e) {
                is Sep -> HorizontalDivider()
                is Header -> Text(e.title, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp))
                is Sub -> DropdownMenuItem(
                    text = { Text(e.title) },
                    trailingIcon = { Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, null) },
                    onClick = { stack.add(e) },
                )
                is Cmd -> DropdownMenuItem(
                    text = { Text(e.title) },
                    enabled = e.enabled,
                    leadingIcon = if (e.checked != null) ({ if (e.checked) Icon(Icons.Outlined.Check, null) else Spacer(Modifier.size(24.dp)) }) else null,
                    trailingIcon = e.keys?.let { k -> { KeyCaps(k) } },
                    onClick = { close(); e.run() },
                )
            }
        }
    }
}

private fun rootEntries(s: DocumentSession, ui: UiState): List<Entry> = listOf(
    Sub("File") { fileMenu(s, ui) },
    Sub("Edit") { editMenu(s, ui) },
    Sub("Item") { itemMenu(s, ui) },
    Sub("Sketch") { sketchMenu(s, ui) },
    Sub("Insert") { toolMenu(s, "insert") + listOf(Sep, Header("Terrain"), act(s, "Rolling Hills", "model.rollingHills"), act(s, "From Height Map…", "file.importHeightMap")) },
    Sub("Construct") { constructMenu(s) },
    Sub("Transform") { toolMenu(s, "transform") },
    Sub("Tools") { toolsMenu(s, ui) },
    Sub("View") { viewMenu(s, ui) },
    Sep,
    Cmd("MCP Server…") { ui.showMcp = true },
)

private fun act(s: DocumentSession, title: String, id: String, keys: String? = null) =
    Cmd(title, s.state.enabled(id), keys) { s.action(id) }

private fun tool(s: DocumentSession, id: String, title: String? = null): Entry {
    val t = s.state.tools.flatMap { it.tools }.firstOrNull { it.id == id }
    return Cmd(title ?: t?.title ?: id, t?.enabled ?: false, t?.shortcut) { s.runTool(id) }
}

private fun toolMenu(s: DocumentSession, category: String): List<Entry> =
    s.state.tools.firstOrNull { it.id == category }?.tools?.map { t -> Cmd(t.title, t.enabled, t.shortcut) { s.runTool(t.id) } } ?: emptyList()

private fun fileMenu(s: DocumentSession, ui: UiState): List<Entry> {
    val host = HostRef.host ?: return emptyList()
    val st = s.state
    val exportable = st.export.kinds.filter { it.available }
    return listOf(
        Cmd("New Project", keys = "⌘N") { host.newDocument() },
        Cmd("Open…", keys = "⌘O") { host.openDocument() },
        Cmd("Save", keys = "⌘S") { host.save(s) },
        Cmd("Save As…", keys = "⇧⌘S") { host.saveAs(s) },
        Cmd("Close") { host.closeDocument(s) },
        Sep,
        Cmd("New Drawing") { ui.showDrawing = true },
        Sep,
        act(s, "Import File…", "file.importFile", "⇧⌘I"),
        act(s, "Import Project…", "file.importProject", "⇧⌘P"),
        act(s, "Import Material…", "file.importMaterial"),
        act(s, "Import Image as Decal…", "file.importDecal"),
        Sep,
        Cmd("View in Browser") { host.viewInBrowser(s) },
        Sub("Export to") {
            listOf(
                Cmd("Export…", keys = "⇧⌘E") { ui.exportToSlicer = false; ui.showExport = true },
                Cmd("Export to Slicer…", st.export.bodyCount > 0) { ui.exportToSlicer = true; ui.showExport = true },
                Cmd("Quick Export to Favorite Formats") {
                    val favs = (Documents.prefString("favorites", "threeMF,step") ?: "").split(",").filter { it.isNotEmpty() }
                    host.quickExport(s, favs)
                },
                Cmd("Open Screenshot Tool…") { host.screenshot(s, 2f, "Sculpt3D Screenshot.png") },
                Cmd("Open Visualization…", st.enabled("visualize.enter")) { s.action("visualize.enter") },
                Sep,
            ) + exportable.map { k ->
                Cmd("${k.title}…", k.enabled) { if (k.fromDrawing) ui.showDrawing = true else host.export(s, k.id) }
            }
        },
    )
}

private fun editMenu(s: DocumentSession, ui: UiState): List<Entry> = listOf(
    act(s, "Undo", "edit.undo", "⌘Z"),
    act(s, "Redo", "edit.redo", "⇧⌘Z"),
    Sep,
    act(s, "Delete", "edit.delete", "⌫"),
    act(s, "Duplicate", "edit.duplicate", "⌘D"),
    Sep,
    act(s, "Select All Bodies", "edit.selectAll", "⌘A"),
    act(s, "Deselect All", "edit.deselectAll", "⇧⌘A"),
    Sep,
    Cmd("Search Tools", keys = "⌘F") { ui.showSearch = true },
    Cmd("Variables…", keys = "⇧⌘V") { ui.showVariables = true },
)

private fun itemMenu(s: DocumentSession, ui: UiState): List<Entry> = listOf(
    act(s, "Show/Hide", "item.toggleVisibilityOfSelection", "⇧⌘H"),
    act(s, "Isolate", "item.isolate"),
    act(s, "Exit Isolation", "item.exitIsolation"),
    Sep,
    act(s, "Zoom to", "view.zoomToSelection"),
    Cmd("Reveal in Items Sidebar") { ui.showItems = true; ui.openMenu = null },
    Cmd("Properties", keys = "⌥⌘P", checked = ui.showProperties) { ui.showProperties = !ui.showProperties },
    Sep,
    act(s, "Show Hidden Bodies", "item.showAll", "⌥H"),
    act(s, "Invert Body Visibility", "item.invertVisibility", "⌥⇧H"),
)

private fun sketchMenu(s: DocumentSession, ui: UiState): List<Entry> {
    val st = s.state
    val canTool = st.draft == null
    val inSketch = st.sketch != null
    val canTransform = st.sketch?.canTransform == true
    fun t(title: String, tool: String, keys: String? = null, vararg extra: Pair<String, Any?>) =
        Cmd(title, canTool, keys) { s.command("sketch.setTool", "tool" to tool, *extra) }
    return listOf(
        t("Line", "line", "L"), t("Arc", "arc", "A"),
        Sub("Spline") { listOf(t("Fit Point Spline", "spline"), t("Control Point Spline", "controlSpline")) },
        Sub("Rectangle") { listOf(t("2-Point Rectangle", "rectangle", "R"), t("Center Rectangle", "centerRectangle")) },
        t("Circle", "circle", "C"), t("Ellipse", "ellipse"),
        Sub("Polygon") {
            listOf(
                t("Inscribed Polygon", "polygon", null, "circumscribed" to false),
                t("Circumscribed Polygon", "polygon", null, "circumscribed" to true),
                Sep,
            ) + listOf(3, 4, 5, 6, 8, 10, 12).map { n -> t("$n Sides", "polygon", null, "sides" to n) }
        },
        t("Point", "point"),
        Sep,
        act(s, "Project as Sketch", "sketch.project"),
        act(s, "Offset Edge", "sketch.offset", "O"),
        act(s, "Fillet Sketch", "sketch.fillet"),
        Cmd("Text…", inSketch) { ui.showSketchText = true },
        Sep,
        Cmd("Scale Sketch", canTransform) { ui.sketchTransform = "scale" },
        Cmd("Pattern - Linear", canTransform) { ui.sketchTransform = "linearPattern" },
        Cmd("Pattern - Circular", canTransform) { ui.sketchTransform = "circularPattern" },
        Cmd("Rotate Sketch Around Axis", canTransform) { ui.sketchTransform = "rotate" },
        act(s, "Mirror Sketch", "sketch.mirror"),
        Sep,
        t("Trim", "trim", "T"),
        Sep,
        act(s, "Finish Sketch", "sketch.finishAsItem"),
        act(s, "Cancel Sketch", "sketch.cancel"),
    )
}

private fun constructMenu(s: DocumentSession): List<Entry> = listOf(
    Header("Plane"),
    tool(s, "construct.offset", "Offset Plane"),
    act(s, "Midplane", "model.midplane"),
    act(s, "Plane Through 3 Points", "pick.planeThrough3Points"),
    act(s, "Plane on Curve at Point", "pick.planeOnCurveAtPoint"),
    act(s, "Plane Perpendicular to Edge at Point", "pick.planePerpendicularToEdgeAtPoint"),
    act(s, "Plane Through Edge at Angle", "model.planeThroughEdge"),
    act(s, "Plane Parallel to Face at Point", "pick.planeParallelToFaceAtPoint"),
    tool(s, "construct.angled", "Angled Plane"),
    Sep,
    Header("Axis"),
    act(s, "Axis Through 2 Points", "pick.axisThrough2Points"),
    act(s, "Axis Through 2 Planes", "model.axisThroughTwoPlanes"),
    act(s, "Axis of Cylinder/Cone", "model.axisOfRoundFace"),
    act(s, "Axis Along Edge", "model.axisAlongEdge"),
    act(s, "Axis Perpendicular to Face at Point", "pick.axisPerpendicularToFaceAtPoint"),
)

private fun toolsMenu(s: DocumentSession, ui: UiState): List<Entry> {
    val st = s.state
    val host = HostRef.host
    return toolMenu(s, "tools") + listOf(
        act(s, "Wrap & Emboss", "model.emboss"),
        Sep,
        if (st.view.visualizing) act(s, "Exit Visualize", "visualize.exit") else act(s, "Visualize", "visualize.enter"),
        Sep,
        Cmd(if (st.view.section.enabled) "Hide Section View" else "Section View", keys = "⌥⌘S") { s.command("view.toggleSection") },
        Cmd("Edit Section View", st.view.section.enabled) { s.command("view.section", "enabled" to true) },
        act(s, "2D Section View", "view.lookAtSection"),
        Sep,
        Sub("Measure") {
            listOf(
                act(s, "Object Measurements", "measure.objects"),
                act(s, "Point-to-Point Distance", "pick.measurePointToPoint"),
                act(s, "3-Point Angle", "pick.measureThreePointAngle"),
                Sep,
                Header("Recent (tap to copy)"),
            ) + st.measure.recent.map { r -> Cmd(r) { host?.copyText(r) } } + listOf(Sep, act(s, "Clear Measurements", "measure.clear"))
        },
        Sep,
        act(s, "Sketch", "model.sketch", "K"),
    )
}

private fun viewMenu(s: DocumentSession, ui: UiState): List<Entry> {
    val st = s.state
    val v = st.view
    fun toggle(title: String, key: String, on: Boolean) = Cmd(title, checked = on) { s.command("view.set", key to !on) }
    return listOf(
        Sub("Properties Panels") {
            listOf(
                Cmd("Material Properties", v.visualizing) { ui.visualizeTab = 0 },
                Cmd("Environment Properties", v.visualizing) { ui.visualizeTab = 1 },
                Cmd("Camera Properties", v.visualizing) { ui.visualizeTab = 2 },
                Cmd("Drawing Properties") { ui.showDrawing = true },
                Cmd("Show Properties Sidebar", checked = ui.showProperties) { ui.showProperties = !ui.showProperties },
                Cmd("Collapse All", keys = "⌥L") { ui.showItems = false; ui.showHistory = false; ui.showProperties = false },
            )
        },
        Sub("Views") {
            listOf("default" to "Default (Isometric)", "front" to "Front", "back" to "Back", "top" to "Top", "bottom" to "Bottom",
                "right" to "Right", "left" to "Left").mapIndexed { i, (id, t) ->
                Cmd(t, keys = "⌘${i + 1}") { s.command("view.look", "preset" to id) }
            } + listOf(
                Cmd("Nearest Ortho View", keys = "⌘8") { s.command("view.snapToNearestOrtho") },
                Sep,
                Cmd("Rotate Left 90°") { s.command("view.rotate", "yaw" to -1) },
                Cmd("Rotate Right 90°") { s.command("view.rotate", "yaw" to 1) },
                Cmd("Rotate Up 90°") { s.command("view.rotate", "pitch" to 1) },
                Cmd("Rotate Down 90°") { s.command("view.rotate", "pitch" to -1) },
                Sep,
                Cmd("Zoom to Fit", keys = "⇧⌘F") { s.command("view.fit") },
            )
        },
        Sub("Display Mode") { v.displayModes.map { m -> Cmd(m.title, checked = m.id == v.displayMode, keys = "⌥${m.key.uppercase()}") { s.command("view.setDisplayMode", "mode" to m.id) } } },
        Sub("Show") {
            listOf(
                Cmd("Grid", checked = st.settings.environment.showGrid) { s.command("settings.setEnvironment", "showGrid" to !st.settings.environment.showGrid) },
                toggle("World Axes", "showWorldAxes", v.showWorldAxes),
                toggle("Edges", "showEdges", v.showEdges),
                toggle("Hidden Edges", "showHiddenEdges", v.showHiddenEdges),
                toggle("Sketches", "showSketches", v.showSketches),
                toggle("Decals", "showDecals", v.showDecals),
                toggle("Pinned Measurements", "showPinnedMeasurements", v.showPinnedMeasurements),
                toggle("Sketch Constraints", "showConstraints", v.showConstraints),
            )
        },
        Sub("Grid Position") {
            listOf(
                Cmd("At Origin", checked = !v.gridAtModelBottom) { s.command("view.set", "gridAtModelBottom" to false) },
                Cmd("At Model Bottom", checked = v.gridAtModelBottom) { s.command("view.set", "gridAtModelBottom" to true) },
            )
        },
        Cmd(v.lockGridTitle) { s.command("view.setGridLock", "on" to true) },
        Cmd("Field of View…") { ui.showFieldOfView = true },
        act(s, "Go to Modeling", "view.goToModeling"),
        Sep,
        Sub("Units") { st.settings.unitOptions.map { u -> Cmd(u.title, checked = u.id == st.settings.units) { s.command("settings.setUnits", "units" to u.id) } } },
        Sub("Foot & Inch Format") {
            st.displayFormat.footInchOptions.map { o ->
                Cmd(o.title, st.displayFormat.footInchEnabled, checked = o.id == st.displayFormat.footInch) {
                    s.command("format.set", "footInch" to o.id); Documents.setFormat(footInch = o.id)
                }
            }
        },
        Sub("Degree Format") {
            st.displayFormat.degreeOptions.map { o ->
                Cmd(o.title, checked = o.id == st.displayFormat.degrees) { s.command("format.set", "degrees" to o.id); Documents.setFormat(degrees = o.id) }
            }
        },
    )
}

/** The activity's [Host], for menu code outside composition. */
object HostRef {
    var host: Host? = null
}
