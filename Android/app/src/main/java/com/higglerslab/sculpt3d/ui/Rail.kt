package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.boundsInRoot
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.model.CameraInfo
import com.higglerslab.sculpt3d.session.DocumentSession

// MARK: - Main rail

/** Left rail. Its contents follow the mode: modeling, sketching, measuring or visualizing. */
@Composable
fun ToolRail(session: DocumentSession, ui: UiState, wide: Boolean) {
    val st = session.state
    Column(Modifier.fillMaxHeight(), verticalArrangement = Arrangement.SpaceBetween) {
        RailGroup(
            listOf(
                RailItem(
                    "project", "Project", "sculpt3d.project", subtitle = st.modeTitle, active = ui.showProject,
                    anchor = {
                        DropdownMenu(ui.showProject, { ui.showProject = false }) { ProjectPanel(session, Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) }
                    },
                ) { ui.showProject = !ui.showProject },
                RailItem("items", "Items", "square.3.layers.3d", active = ui.showItems) { ui.showItems = !ui.showItems },
            ),
            showLabels = wide,
        )
        Box(Modifier.weight(1f, fill = false).padding(vertical = 8.dp).verticalScroll(rememberScrollState())) { Middle(session, ui, wide) }
        if (!st.view.visualizing) {
            RailGroup(
                listOf(
                    RailItem(
                        "section", "Section View", "square.split.bottomrightquarter",
                        subtitle = if (st.view.section.enabled) "On" else "Off", active = st.view.section.enabled,
                    ) { session.command("view.toggleSection") },
                    RailItem("measure", "Measure", "ruler", active = st.view.measuring) { session.command("view.toggleMeasuring") },
                ),
                showLabels = wide,
            )
        }
    }
}

@Composable
private fun Middle(session: DocumentSession, ui: UiState, wide: Boolean) {
    val st = session.state
    val search = RailItem("search", "Search", "magnifyingglass", keys = "⌘F", active = ui.showSearch) { ui.showSearch = true }
    when {
        st.view.visualizing -> RailGroup(
            listOf(search, RailItem("exitViz", "Exit Visualization", "xmark") { session.action("visualize.exit") }),
            showLabels = wide,
        )
        st.sketch != null -> SketchRail(session, ui, wide)
        st.view.measuring -> RailGroup(
            listOf(
                RailItem("exitMeasure", "Exit Measure", "xmark") { session.command("view.setMeasuring", "on" to false) },
                RailItem("measureMode", "Measure mode", "ruler", subtitle = "Object Measurements", active = true) {},
            ),
            showLabels = wide,
        )
        else -> Box(Modifier.onGloballyPositioned { ui.railMenuBounds = it.boundsInRoot() }) {
            val cats = st.tools.filter { it.onRail }
            RailGroup(
                listOf(search) + cats.map { c ->
                    RailItem(
                        c.id, c.title, if (c.id == "visualize") "sculpt3d.visualize" else c.symbol,
                        active = ui.openMenu == c.id, dot = c.dot,
                    ) {
                        if (c.id == "visualize") {
                            ui.openMenu = null
                            session.action("visualize.enter")
                        } else {
                            ui.openMenu = if (ui.openMenu == c.id) null else c.id
                        }
                    }
                },
                showLabels = wide && ui.openMenu == null,
            )
        }
    }
}

/** The open category's tools, beside the rail's icons (drawn over the viewport, Swift ToolFlyout). */
@Composable
fun ToolFlyout(session: DocumentSession, ui: UiState) {
    val open = ui.openMenu ?: return
    val st = session.state
    if (st.sketch != null || st.view.measuring || st.view.visualizing) return
    val cat = st.tools.firstOrNull { it.id == open } ?: return
    val b = ui.railMenuBounds ?: return
    val density = androidx.compose.ui.platform.LocalDensity.current
    BoxWithConstraints(Modifier.fillMaxSize()) {
        val maxH = constraints.maxHeight
        Box(
            Modifier
                .offset { androidx.compose.ui.unit.IntOffset((b.right + with(density) { 8.dp.toPx() }).toInt(), 0) }
                .padding(top = with(density) { b.top.toDp() }.coerceAtMost(with(density) { (maxH * 0.35f).toDp() }))
                .heightIn(max = with(density) { maxH.toDp() } - 16.dp)
                .verticalScroll(rememberScrollState()),
        ) {
            RailGroup(
                cat.tools.map { t ->
                    RailItem(t.id, t.title, t.symbol, keys = t.shortcut, enabled = t.enabled) {
                        ui.openMenu = null
                        session.runTool(t.id)
                    }
                },
                showLabels = true,
            )
        }
    }
}

// MARK: - Sketch mode

/** Sketching tools down the left side. */
@Composable
fun SketchRail(session: DocumentSession, ui: UiState, wide: Boolean) {
    val s = session.state.sketch ?: return
    val hasSelection = s.selection.isNotEmpty()
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        RailGroup(
            listOf(
                RailItem("search", "Search", "magnifyingglass", keys = "⌘F") { ui.showSearch = true },
                RailItem("exit", "Exit Sketching", "xmark") { session.action("sketch.cancel") },
                RailItem("plane", s.plane.name.ifEmpty { "No active plane" }, "square.grid.3x3") { session.action("sketch.lookAt") },
            ),
            showLabels = wide,
        )
        RailGroup(
            s.tools.filter { it.id != "select" && it.id != "trim" }.map { t ->
                RailItem(t.id, t.title, t.symbol, subtitle = t.subtitle, keys = t.key, active = t.active) {
                    session.command("sketch.setTool", "tool" to t.id)
                }
            } + listOf(
                RailItem("project", "Project", "rectangle.portrait.on.rectangle.portrait.angled") { session.action("sketch.project") },
                RailItem("offset", "Offset Edge", "square.on.square", keys = "O", enabled = hasSelection) { session.action("sketch.offset") },
                RailItem("fillet", "Fillet", "arrow.turn.up.right", enabled = hasSelection) { session.action("sketch.fillet") },
            ),
            showLabels = wide,
        )
        RailGroup(
            listOf(
                RailItem("select", "Select", "cursorarrow", keys = "S", active = s.tool == "select") { session.command("sketch.setTool", "tool" to "select") },
                RailItem("trim", "Trim", "scissors", keys = "T", active = s.tool == "trim") { session.command("sketch.setTool", "tool" to "trim") },
                RailItem("delete", "Delete", "trash", enabled = hasSelection) { session.action("sketch.deleteSelection") },
            ),
            showLabels = wide,
        )
        RailGroup(
            listOf(
                RailItem(
                    "construction", "Construction", "square.dashed", subtitle = if (s.constructionMode) "On" else "Off",
                    active = s.constructionMode,
                ) { session.action("sketch.toggleConstructionMode") },
            ),
            showLabels = wide,
        )
    }
}

/** Constraints down the right side, enabled by what the selection allows. */
@Composable
fun ConstraintColumn(session: DocumentSession, wide: Boolean) {
    val s = session.state.sketch ?: return
    RailGroup(
        s.constraintColumn.map { c ->
            RailItem(c.id, c.title, c.symbol, keys = c.keys, enabled = c.enabled) {
                session.command("sketch.constraintColumn", "id" to c.id)
            }
        },
        showLabels = wide,
        trailing = true,
    )
}

// MARK: - Right-hand view controls

@Composable
fun ViewControlsColumn(session: DocumentSession, ui: UiState, wide: Boolean, cubeLarge: Boolean = wide) {
    val st = session.state
    val host = LocalHost.current
    Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.Top) {
            ViewCube(session, if (cubeLarge) 100.dp else 78.dp)
            Column(Modifier.panel(radius = 12.dp).padding(3.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Box(Modifier.clip(RoundedCornerShape(9.dp)).clickable { ui.showSnap = true }) {
                    RailIcon("sculpt3d.magnet", active = st.settings.snap.grid)
                    DropdownMenu(ui.showSnap, { ui.showSnap = false }) { SnapSettingsPanel(session, Modifier.padding(horizontal = 16.dp, vertical = 6.dp)) }
                }
                Box(
                    Modifier.size(34.dp).clip(RoundedCornerShape(9.dp)).clickable { ui.showGrid = true },
                    contentAlignment = Alignment.Center,
                ) {
                    Text(
                        st.view.gridLabel, fontSize = 10.sp, lineHeight = 11.sp, fontWeight = FontWeight.Bold,
                        textAlign = TextAlign.Center,
                    )
                    DropdownMenu(ui.showGrid, { ui.showGrid = false }) { GridSettingsPanel(session) { ui.showGrid = false } }
                }
                Box(Modifier.clip(RoundedCornerShape(9.dp)).clickable {
                    session.command("settings.setEnvironment", "showGrid" to !st.settings.environment.showGrid)
                }) { RailIcon("move.3d", active = !st.settings.environment.showGrid) }
            }
        }
        RailGroup(
            listOf(
                RailItem("display", "Display Modes", st.view.displayModeSymbol, subtitle = st.view.displayModeTitle) {
                    session.command("view.cycleDisplayMode")
                },
                RailItem("shot", "Screenshot", "camera") { host.screenshot(session, 2f, "Sculpt3D Screenshot.png") },
                RailItem("export", "Export STEP", "square.and.arrow.up") { host.export(session, "step") },
                RailItem("history", "History", "clock.arrow.circlepath", keys = "⌥⌘P", active = ui.showHistory) {
                    ui.showHistory = !ui.showHistory
                },
            ),
            showLabels = wide,
            trailing = true,
        )
    }
}

/** Orientation cube: shows the camera's attitude; tap a face to look at it (Swift ViewCube). */
@Composable
fun ViewCube(session: DocumentSession, side: Dp) {
    val cam = session.annotations.camera
    val measurer = rememberTextMeasurer()
    val labelColor = MaterialTheme.colorScheme.onSurfaceVariant
    val dark = MaterialTheme.colorScheme.surface.luminance() < 0.5f
    Canvas(
        Modifier
            .size(side)
            .pointerInput(cam) {
                detectTapGestures { p ->
                    val c = Offset(size.width / 2f, size.height / 2f)
                    val faces = visibleFaces(cam, size.width * 0.38f)
                    for ((face, pts, _) in faces.reversed()) {
                        if (contains(pts.map { it + c }, p)) {
                            session.command("view.setOrientation", "face" to face.lowercase())
                            return@detectTapGestures
                        }
                    }
                }
            },
    ) {
        val c = Offset(size.width / 2f, size.height / 2f)
        val scale = size.width * 0.38f
        for ((name, pts, shade) in visibleFaces(cam, scale)) {
            val path = Path().apply {
                moveTo(c.x + pts[0].x, c.y + pts[0].y)
                for (p in pts.drop(1)) lineTo(c.x + p.x, c.y + p.y)
                close()
            }
            val base = if (dark) 0.32f else 0.97f
            val g = (base - shade * 0.12f).coerceIn(0f, 1f)
            drawPath(path, Color(g, g, g, 0.95f))
            drawPath(path, Color.Black.copy(alpha = 0.25f), style = Stroke(width = 1.dp.toPx()))
            val mid = pts.fold(Offset.Zero) { a, b -> a + b / 4f }
            drawLabel(measurer, name, c + mid, labelColor)
        }
        val o = project(doubleArrayOf(-0.5, -0.5, -0.5), cam, scale)
        listOf(
            doubleArrayOf(1.0, 0.0, 0.0) to Color.Red, doubleArrayOf(0.0, 1.0, 0.0) to Color(0xFF2EB82E),
            doubleArrayOf(0.0, 0.0, 1.0) to Color.Blue,
        ).forEach { (axis, color) ->
            val e = project(doubleArrayOf(-0.5 + axis[0] * 1.35, -0.5 + axis[1] * 1.35, -0.5 + axis[2] * 1.35), cam, scale)
            drawLine(color.copy(alpha = 0.8f), c + o, c + e, strokeWidth = 1.5.dp.toPx())
        }
    }
}

private fun Color.luminance() = 0.2126f * red + 0.7152f * green + 0.0722f * blue

private fun androidx.compose.ui.graphics.drawscope.DrawScope.drawLabel(m: TextMeasurer, text: String, at: Offset, color: Color) {
    val layout = m.measure(text, TextStyle(fontSize = 8.sp, fontWeight = FontWeight.SemiBold, color = color))
    drawText(layout, topLeft = Offset(at.x - layout.size.width / 2f, at.y - layout.size.height / 2f))
}

private data class CubeFace(val name: String, val normal: DoubleArray)

private val cubeFaces = listOf(
    CubeFace("Top", doubleArrayOf(0.0, 0.0, 1.0)), CubeFace("Bottom", doubleArrayOf(0.0, 0.0, -1.0)),
    CubeFace("Front", doubleArrayOf(0.0, -1.0, 0.0)), CubeFace("Back", doubleArrayOf(0.0, 1.0, 0.0)),
    CubeFace("Right", doubleArrayOf(1.0, 0.0, 0.0)), CubeFace("Left", doubleArrayOf(-1.0, 0.0, 0.0)),
)

private fun dot(a: DoubleArray, b: List<Double>) = a[0] * b.getOrElse(0) { 0.0 } + a[1] * b.getOrElse(1) { 0.0 } + a[2] * b.getOrElse(2) { 0.0 }

private fun project(p: DoubleArray, cam: CameraInfo, s: Float) = Offset((dot(p, cam.right) * s).toFloat(), (-dot(p, cam.up) * s).toFloat())

private fun visibleFaces(cam: CameraInfo, s: Float): List<Triple<String, List<Offset>, Float>> {
    val toEye = cam.forward.map { -it }
    val out = mutableListOf<Pair<Triple<String, List<Offset>, Float>, Double>>()
    for (f in cubeFaces) {
        val n = f.normal
        val facing = dot(n, toEye)
        if (facing <= 0.01) continue
        val u = if (kotlin.math.abs(n[2]) > 0.5) doubleArrayOf(1.0, 0.0, 0.0) else doubleArrayOf(0.0, 0.0, 1.0)
        val v = doubleArrayOf(n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
        val corners = listOf(-1 to -1, 1 to -1, 1 to 1, -1 to 1).map { (a, b) ->
            DoubleArray(3) { i -> n[i] * 0.5 + u[i] * 0.5 * a + v[i] * 0.5 * b }
        }
        out.add(Triple(f.name, corners.map { project(it, cam, s) }, (1 - facing).toFloat()) to facing)
    }
    return out.sortedBy { it.second }.map { it.first }
}

private fun contains(poly: List<Offset>, p: Offset): Boolean {
    var inside = false
    var j = poly.size - 1
    for (i in poly.indices) {
        val a = poly[i]
        val b = poly[j]
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside
        j = i
    }
    return inside
}

// MARK: - Popovers

@Composable
fun SnapSettingsPanel(session: DocumentSession, modifier: Modifier = Modifier) {
    val snap = session.state.settings.snap
    fun set(key: String, v: Boolean) = session.command("settings.setSnap", key to v)
    Column(modifier.width(280.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text("Snap to", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
        SwitchRow("Grid", snap.grid, "grid") { set("grid", it) }
        SwitchRow("Sketch Guide Lines", snap.guideLines, "square.grid.3x3") { set("guideLines", it) }
        SwitchRow("Sketch Guide Points", snap.guidePoints, "circle.dotted") { set("guidePoints", it) }
        SwitchRow("3D Guide Points", snap.points3D, "cube") { set("points3D", it) }
        SwitchRow("Distant Edges", snap.distantEdges, "cube.transparent") { set("distantEdges", it) }
        HorizontalDivider(Modifier.padding(vertical = 4.dp))
        Text("Show", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
        SwitchRow("Guide Points", snap.showGuidePoints, "line.diagonal") { set("showGuidePoints", it) }
        SwitchRow("Snapping Hints", snap.showHints, "text.bubble") { set("showHints", it) }
    }
}

@Composable
fun GridSettingsPanel(session: DocumentSession, close: () -> Unit) {
    val v = session.state.view
    Text("Grid", fontWeight = FontWeight.SemiBold, fontSize = 15.sp, modifier = Modifier.padding(horizontal = 16.dp, vertical = 6.dp))
    DropdownMenuItem(
        text = { Text("Automatic") },
        onClick = { session.command("view.setFixedGrid", "value" to null); close() },
        trailingIcon = { if (v.fixedGrid == null) SymbolIcon("checkmark", size = 18.dp) },
    )
    v.gridOptions.forEach { o ->
        DropdownMenuItem(
            text = { Text(o.title) },
            onClick = { session.command("view.setFixedGrid", "value" to o.value); close() },
            trailingIcon = { if (v.fixedGrid != null && kotlin.math.abs(v.fixedGrid - o.value) < 1e-9) SymbolIcon("checkmark", size = 18.dp) },
        )
    }
    HorizontalDivider()
    DropdownMenuItem(text = { Text(v.lockGridTitle) }, onClick = { session.command("view.setGridLock", "on" to true); close() })
}

@Composable
fun ProjectPanel(session: DocumentSession, modifier: Modifier = Modifier) {
    val st = session.state
    Column(modifier.width(280.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Project", fontWeight = FontWeight.SemiBold, fontSize = 15.sp)
        val opts = st.settings.unitOptions
        Segmented(opts.map { it.symbol }, opts.indexOfFirst { it.id == st.settings.units }) {
            session.command("settings.setUnits", "units" to opts[it].id)
        }
        SwitchRow("Snap to grid", st.settings.snap.grid) { session.command("settings.setSnap", "grid" to it) }
        Text(st.project.summary, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}
