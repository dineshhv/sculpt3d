package com.higglerslab.sculpt3d.ui

import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.os.Build
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.higglerslab.sculpt3d.model.CoreJson
import com.higglerslab.sculpt3d.model.DrawingReply
import com.higglerslab.sculpt3d.session.DocumentSession
import com.higglerslab.sculpt3d.session.Documents
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive

/** Sheets and dialogs of a document window (Swift MenuSheets and the compact Items/History sheets). */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun WorkspaceSheets(session: DocumentSession, ui: UiState) {
    val wide = LocalWide.current
    val st = session.state
    if (!wide && ui.showItems) Sheet({ ui.showItems = false }) { Text("Items", Modifier.padding(horizontal = 16.dp), fontWeight = FontWeight.SemiBold, fontSize = 18.sp); ItemsPanel(session, Modifier.fillMaxWidth().heightIn(max = 520.dp)) }
    if (!wide && ui.showHistory) Sheet({ ui.showHistory = false }) { HistoryPanel(session, Modifier.fillMaxWidth().heightIn(max = 520.dp)) }
    if (!wide && ui.showProperties) Sheet({ ui.showProperties = false }) { PropertiesPanel(session, Modifier.fillMaxWidth()) }
    if (ui.showVariables) Sheet({ ui.showVariables = false }) { VariablesPanel(session) { ui.showVariables = false } }
    if (ui.showExport) Sheet({ ui.showExport = false }) { ExportSheet(session, ui) }
    if (ui.showFieldOfView) Sheet({ ui.showFieldOfView = false }) { FieldOfViewSheet(session) { ui.showFieldOfView = false } }
    ui.sketchTransform?.let { kind -> Sheet({ ui.sketchTransform = null }) { SketchTransformSheet(session, kind) { ui.sketchTransform = null } } }
    if (ui.showSketchText) Sheet({ ui.showSketchText = false }) { SketchTextSheet(session) { ui.showSketchText = false } }
    if (ui.showMcp) Sheet({ ui.showMcp = false }) { McpSheet() }
    if (ui.showDocuments) Sheet({ ui.showDocuments = false }) { DocumentsSheet(session) { ui.showDocuments = false } }
    if (ui.showDrawing) DrawingWindow(session) { ui.showDrawing = false }
    // Leaving a mode closes what belongs to it.
    LaunchedEffect(st.sketch == null) { if (st.sketch == null) ui.editingDimension = null }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Sheet(onDismiss: () -> Unit, content: @Composable ColumnScope.() -> Unit) {
    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)) {
        Column(Modifier.fillMaxWidth().navigationBarsPadding().imePadding().padding(bottom = 12.dp), content = content)
    }
}

// MARK: - Export

@Composable
fun ExportSheet(session: DocumentSession, ui: UiState) {
    val host = LocalHost.current
    val ex = session.state.export
    var favorites by remember { mutableStateOf((Documents.prefString("favorites", "threeMF,step") ?: "").split(",").filter { it.isNotEmpty() }.toSet()) }
    fun toggle(id: String) {
        favorites = if (id in favorites) favorites - id else favorites + id
        Documents.setPrefString("favorites", ex.kinds.map { it.id }.filter { it in favorites }.joinToString(","))
    }
    Column(Modifier.padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(if (ui.exportToSlicer) "Export to Slicer" else "Export", fontWeight = FontWeight.SemiBold, fontSize = 18.sp, modifier = Modifier.weight(1f))
            Text(ex.scope, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        if (ui.exportToSlicer) {
            var fmt by remember { mutableStateOf("threeMF") }
            listOf("threeMF" to "3MF (keeps colours and separate parts)", "stl" to "STL", "obj" to "OBJ").forEach { (id, title) ->
                Row(Modifier.fillMaxWidth().clickable { fmt = id }, verticalAlignment = Alignment.CenterVertically) {
                    RadioButton(fmt == id, { fmt = id })
                    Text(title)
                }
            }
            Hint("Opens the model in a slicer or another app on this device that accepts the file.")
            Button(onClick = { host.openInApp(session, fmt); ui.showExport = false }, enabled = ex.bodyCount > 0) { Text("Open in…") }
        } else {
            LazyColumn(Modifier.heightIn(max = 440.dp)) {
                items(ex.kinds, key = { it.id }) { k ->
                    Row(Modifier.fillMaxWidth().alpha(if (k.available) 1f else 0.5f).padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        IconButton(onClick = { toggle(k.id) }, enabled = k.available) {
                            Icon(if (k.id in favorites) Icons.Outlined.Star else Icons.Outlined.StarBorder, "Favourite",
                                tint = if (k.id in favorites) Brand.orange else MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        Column(Modifier.weight(1f)) {
                            Text("${k.title} (.${k.extension})", fontWeight = FontWeight.Medium, fontSize = 14.sp)
                            Text(k.detail, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        TextButton(onClick = {
                            if (k.fromDrawing) {
                                ui.showExport = false
                                ui.showDrawing = true
                            } else {
                                host.export(session, k.id)
                                ui.showExport = false
                            }
                        }, enabled = k.available && k.enabled) { Text("Export…") }
                    }
                }
            }
            HorizontalDivider()
            Text("Quick Export", fontWeight = FontWeight.SemiBold)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Outlined.Folder, null, Modifier.size(18.dp))
                Spacer(Modifier.width(6.dp))
                Text(host.quickExportFolderName() ?: "No folder chosen", Modifier.weight(1f), fontSize = 13.sp, maxLines = 1)
                TextButton(onClick = { host.chooseQuickExportFolder() }) { Text("Choose…") }
            }
            Row {
                Text("★ marks formats for Quick Export to Favorite Formats.", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
                Button(onClick = {
                    host.quickExport(session, ex.kinds.filter { it.id in favorites && it.available }.map { it.id })
                    ui.showExport = false
                }, enabled = favorites.isNotEmpty()) { Text("Export Favorites Now") }
            }
        }
    }
}

// MARK: - Field of view

@Composable
fun FieldOfViewSheet(session: DocumentSession, done: () -> Unit) {
    val cam = session.state.settings.camera
    Column(Modifier.padding(horizontal = 20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Field of View", fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
        SwitchRow("Orthographic (no perspective)", cam.orthographic) { session.command("settings.setCamera", "orthographic" to it) }
        val t = rememberThrottle { session.command("settings.setCamera", "fieldOfView" to it.toInt().toFloat()) }
        SliderRow("Field of view", cam.fieldOfView, 10f..100f, { "${it.toInt()}°" }, !cam.orthographic) { t(it) }
        Hint("Wide angles exaggerate perspective; narrow ones flatten it, like a telephoto lens.")
        Row {
            TextButton(onClick = { session.command("settings.setCamera", "reset" to true) }) { Text("Reset") }
            Spacer(Modifier.weight(1f))
            Button(onClick = done) { Text("Done") }
        }
    }
}

// MARK: - Sketch transform

@Composable
fun SketchTransformSheet(session: DocumentSession, kind: String, done: () -> Unit) {
    var factor by remember { mutableStateOf("2") }
    var angle by remember { mutableStateOf("90") }
    var keep by remember { mutableStateOf(false) }
    var count by remember { mutableIntStateOf(3) }
    var spacing by remember { mutableStateOf("10") }
    var direction by remember { mutableStateOf("0") }
    var total by remember { mutableStateOf("360") }
    val (title, hint) = when (kind) {
        "scale" -> "Scale Sketch" to "Scales the selected geometry about a selected point, or its centre."
        "rotate" -> "Rotate Sketch Around Axis" to "Turns the selected geometry about a selected point, or the sketch origin."
        "linearPattern" -> "Pattern - Linear" to "Repeats the selected geometry along a direction (0° is the sketch's X axis)."
        else -> "Pattern - Circular" to "Repeats the selected geometry around a selected point, or the sketch origin."
    }
    fun num(s: String) = s.replace(',', '.').toDoubleOrNull() ?: 0.0
    var error by remember { mutableStateOf<String?>(null) }
    Column(Modifier.padding(horizontal = 20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(title, fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
        Hint(hint)
        @Composable fun field(label: String, v: String, unit: String, set: (String) -> Unit) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(label, Modifier.weight(1f))
                CommitField(v, set, Modifier.width(100.dp), onChange = set)
                Text(unit, Modifier.width(34.dp).padding(start = 6.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        @Composable fun stepper(max: Int) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Instances: $count", Modifier.weight(1f))
                StepButton(Icons.Outlined.Remove) { count = (count - 1).coerceAtLeast(2) }
                Spacer(Modifier.width(6.dp))
                StepButton(Icons.Outlined.Add) { count = (count + 1).coerceAtMost(max) }
            }
        }
        when (kind) {
            "scale" -> { field("Scale factor", factor, "×") { factor = it }; SwitchRow("Keep original", keep) { keep = it } }
            "rotate" -> { field("Angle", angle, "°") { angle = it }; SwitchRow("Keep original", keep) { keep = it } }
            "linearPattern" -> { stepper(200); field("Spacing", spacing, "mm") { spacing = it }; field("Direction", direction, "°") { direction = it } }
            else -> { stepper(360); field("Total angle", total, "°") { total = it } }
        }
        error?.let { Text(it, color = Brand.orange, fontSize = 12.sp) }
        Row {
            TextButton(onClick = done) { Text("Cancel") }
            Spacer(Modifier.weight(1f))
            Button(onClick = {
                val r = when (kind) {
                    "scale" -> session.command("sketch.transform", "kind" to "scale", "factor" to num(factor), "keepOriginal" to keep)
                    "rotate" -> session.command("sketch.transform", "kind" to "rotate", "angle" to num(angle), "keepOriginal" to keep)
                    "linearPattern" -> session.command("sketch.transform", "kind" to "linearPattern", "count" to count,
                        "spacing" to num(spacing) * session.state.units.scale, "direction" to num(direction))
                    else -> session.command("sketch.transform", "kind" to "circularPattern", "count" to count, "totalAngle" to num(total))
                }
                if (r.ok) done() else error = r.error
            }) { Text("Apply") }
        }
    }
}

// MARK: - Sketch text

private val textFonts = listOf(
    "Sans Bold" to Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD),
    "Sans" to Typeface.SANS_SERIF,
    "Sans Condensed Bold" to Typeface.create("sans-serif-condensed", Typeface.BOLD),
    "Sans Black" to Typeface.create("sans-serif-black", Typeface.NORMAL),
    "Serif Bold" to Typeface.create(Typeface.SERIF, Typeface.BOLD),
    "Serif" to Typeface.SERIF,
    "Monospace Bold" to Typeface.create(Typeface.MONOSPACE, Typeface.BOLD),
    "Monospace" to Typeface.MONOSPACE,
)

@Composable
fun SketchTextSheet(session: DocumentSession, done: () -> Unit) {
    var text by remember { mutableStateOf("Sculpt3D") }
    var font by remember { mutableIntStateOf(0) }
    var height by remember { mutableStateOf("20") }
    var error by remember { mutableStateOf<String?>(null) }
    var fontMenu by remember { mutableStateOf(false) }
    Column(Modifier.padding(horizontal = 20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Text", fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
        Hint("Adds the text as closed outlines at the sketch origin, ready to extrude or emboss.")
        OutlinedTextField(text, { text = it }, label = { Text("Text") }, singleLine = true, modifier = Modifier.fillMaxWidth().trackTextFocus())
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Font", Modifier.weight(1f))
            Box {
                TextButton(onClick = { fontMenu = true }) { Text(textFonts[font].first) }
                DropdownMenu(fontMenu, { fontMenu = false }) {
                    textFonts.forEachIndexed { i, (name, _) -> DropdownMenuItem({ Text(name) }, { font = i; fontMenu = false }) }
                }
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Height", Modifier.weight(1f))
            CommitField(height, { height = it }, Modifier.width(90.dp), onChange = { height = it })
            Text(session.state.units.symbol, Modifier.padding(start = 6.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        error?.let { Text(it, color = Brand.orange, fontSize = 12.sp) }
        Row {
            TextButton(onClick = done) { Text("Cancel") }
            Spacer(Modifier.weight(1f))
            Button(onClick = {
                val h = (height.replace(',', '.').toDoubleOrNull() ?: 20.0).coerceAtLeast(0.1) * session.state.units.scale
                val path = textOutlines(text, textFonts[font].second, h)
                val r = session.command("sketch.addOutlines", "path" to path)
                if (r.ok) done() else error = r.error
            }, enabled = text.isNotBlank()) { Text("Add Text") }
        }
    }
}

/**
 * Lays out `text` with the platform font at `heightMm` (the font size) and returns the glyph
 * outlines as `sketch.addOutlines` path ops in mm, y up, starting at the sketch origin.
 */
fun textOutlines(text: String, typeface: Typeface, heightMm: Double): JsonArray {
    val ref = 100f // lay out large, then scale, so hinting doesn't distort small sizes
    val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        this.typeface = typeface
        textSize = ref
        isLinearText = true
        hinting = Paint.HINTING_OFF
    }
    val path = Path()
    paint.getTextPath(text, 0, text.length, 0f, 0f, path)
    val k = (heightMm / ref).toFloat()
    path.transform(Matrix().apply { setScale(k, -k) }) // mm, y up
    val ops = mutableListOf<JsonObject>()
    fun op(name: String, vararg pts: Float) {
        val points = pts.toList().chunked(2).map { JsonArray(listOf(JsonPrimitive(it[0].toDouble()), JsonPrimitive(it[1].toDouble()))) }
        ops.add(JsonObject(mapOf("op" to JsonPrimitive(name), "points" to JsonArray(points))))
    }
    if (Build.VERSION.SDK_INT >= 34) {
        val it = path.pathIterator
        val p = FloatArray(8)
        while (it.hasNext()) {
            when (it.next(p, 0)) {
                android.graphics.PathIterator.VERB_MOVE -> op("move", p[0], p[1])
                android.graphics.PathIterator.VERB_LINE -> op("line", p[2], p[3])
                android.graphics.PathIterator.VERB_QUAD -> op("quad", p[2], p[3], p[4], p[5])
                android.graphics.PathIterator.VERB_CONIC -> op("quad", p[2], p[3], p[4], p[5])
                android.graphics.PathIterator.VERB_CUBIC -> op("cubic", p[2], p[3], p[4], p[5], p[6], p[7])
                android.graphics.PathIterator.VERB_CLOSE -> op("close")
                else -> {}
            }
        }
    } else {
        // API < 34: flatten to polylines; equal fractions mark a jump to the next contour.
        val a = path.approximate((heightMm / 120.0).toFloat().coerceAtLeast(0.01f))
        var lastFrac = -1f
        var open = false
        var i = 0
        while (i + 2 < a.size) {
            val f = a[i]
            val x = a[i + 1]
            val y = a[i + 2]
            if (!open || f == lastFrac) {
                if (open) op("close")
                op("move", x, y)
                open = true
            } else {
                op("line", x, y)
            }
            lastFrac = f
            i += 3
        }
        if (open) op("close")
    }
    return JsonArray(ops)
}

// MARK: - MCP

@Composable
fun McpSheet() {
    val host = LocalHost.current
    val forward = "adb forward tcp:${Documents.MCP_PORT} tcp:${Documents.MCP_PORT}"
    val add = "claude mcp add --transport http sculpt3d http://127.0.0.1:${Documents.MCP_PORT}/mcp"
    Column(Modifier.padding(horizontal = 20.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("MCP Server", fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
        Hint("Lets AI assistants model in the open documents. It listens only on 127.0.0.1:${Documents.MCP_PORT} on this device; forward the port from your computer over USB.")
        SwitchRow(if (Documents.mcpRunning) "Running" else "Off", Documents.mcpRunning) { if (it) Documents.startMcp() else Documents.stopMcp() }
        Documents.mcpError?.let { Text(it, color = Brand.orange, fontSize = 12.sp) }
        Text("1. Forward the port", fontWeight = FontWeight.Medium)
        CommandBox(forward) { host.copyText(forward) }
        Text("2. Add it to Claude Code", fontWeight = FontWeight.Medium)
        CommandBox(add) { host.copyText(add) }
    }
}

@Composable
private fun CommandBox(text: String, copy: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp)).background(MaterialTheme.colorScheme.surfaceVariant).padding(start = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text, fontFamily = FontFamily.Monospace, fontSize = 12.sp, modifier = Modifier.weight(1f).padding(vertical = 8.dp))
        IconButton(onClick = copy) { Icon(Icons.Outlined.ContentCopy, "Copy", Modifier.size(18.dp)) }
    }
}

// MARK: - Documents

@Composable
fun DocumentsSheet(session: DocumentSession, done: () -> Unit) {
    val host = LocalHost.current
    Column(Modifier.padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("Documents", fontWeight = FontWeight.SemiBold, fontSize = 18.sp)
        Documents.sessions.forEach { s ->
            val active = s == Documents.active
            Row(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(10.dp))
                    .background(if (active) MaterialTheme.colorScheme.primary.copy(alpha = 0.1f) else Color.Transparent)
                    .clickable { Documents.activate(s); done() }
                    .padding(horizontal = 10.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(Icons.Outlined.Description, null, tint = if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(s.state.document.name + if (s.state.document.dirty) " •" else "", fontWeight = if (active) FontWeight.SemiBold else FontWeight.Normal)
                    Text(s.state.project.summary, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                IconButton(onClick = { host.closeDocument(s) }) { Icon(Icons.Outlined.Close, "Close document") }
            }
        }
        HorizontalDivider()
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { done(); host.newDocument() }) { Text("New") }
            OutlinedButton(onClick = { done(); host.openDocument() }) { Text("Open…") }
            OutlinedButton(onClick = { done(); host.save(session) }) { Text("Save") }
            OutlinedButton(onClick = { done(); host.saveAs(session) }) { Text("Save As…") }
            OutlinedButton(onClick = { done(); host.showWelcome() }) { Text("Home") }
        }
    }
}

// MARK: - Drawing

/** File ▸ New Drawing: orthographic views on a sheet, exported as PDF, SVG or DXF (Swift DrawingWindow). */
@Composable
fun DrawingWindow(session: DocumentSession, close: () -> Unit) {
    val host = LocalHost.current
    var paper by remember { mutableStateOf("a3") }
    var views by remember { mutableStateOf(setOf("front", "top", "right", "isometric")) }
    var hidden by remember { mutableStateOf(true) }
    var dims by remember { mutableStateOf(true) }
    var scale by remember { mutableStateOf<Double?>(null) }
    var title by remember { mutableStateOf(session.state.document.name) }
    val settings = mapOf(
        "paper" to paper, "views" to listOf("front", "top", "right", "isometric").filter { it in views },
        "showHidden" to hidden, "showDimensions" to dims, "scale" to scale, "title" to title,
    )
    val reply = remember(settings, session.state.revision) {
        val r = session.command("drawing.sheet", "drawing" to settings)
        try { CoreJson.decodeFromJsonElement(DrawingReply.serializer(), r.json) } catch (_: Throwable) { DrawingReply() }
    }
    val options = remember(settings) { JsonObject(mapOf("drawing" to com.higglerslab.sculpt3d.session.jsonOf(settings))).toString() }
    val measurer = rememberTextMeasurer()
    Dialog(onDismissRequest = close, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
            Column(Modifier.fillMaxSize().safeDrawingPadding().padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(onClick = close) { Icon(Icons.Outlined.Close, "Close") }
                    Text("Drawing", fontWeight = FontWeight.SemiBold, fontSize = 18.sp, modifier = Modifier.weight(1f))
                    val base = title.ifBlank { "Drawing" }
                    TextButton(onClick = { host.export(session, "pdf", options, "$base.pdf") }) { Text("PDF") }
                    TextButton(onClick = { host.export(session, "svg", options, "$base.svg") }) { Text("SVG") }
                    TextButton(onClick = { host.export(session, "dxfDrawing", options, "$base.dxf") }) { Text("DXF") }
                }
                // The sheet: paper mm, y down.
                val size = reply.sheet.size
                val pw = size.getOrElse(0) { 297f }
                val ph = size.getOrElse(1) { 210f }
                Box(Modifier.fillMaxWidth().weight(1f), contentAlignment = Alignment.Center) {
                    Canvas(
                        Modifier.fillMaxWidth().aspectRatio(pw / ph).background(Color.White).border(0.5.dp, Color.Gray),
                    ) {
                        val k = this.size.width / pw
                        val ink = Color(0xFF14171F)
                        for (s in reply.sheet.strokes) {
                            if (s.points.size < 2) continue
                            val p = androidx.compose.ui.graphics.Path().apply {
                                moveTo(s.points[0][0] * k, s.points[0][1] * k)
                                s.points.drop(1).forEach { lineTo(it[0] * k, it[1] * k) }
                            }
                            drawPath(p, ink, style = Stroke(
                                width = (s.width * k).coerceAtLeast(0.6f), cap = StrokeCap.Round,
                                pathEffect = if (s.dashed) PathEffect.dashPathEffect(floatArrayOf(1.6f * k, 1f * k)) else null,
                            ))
                        }
                        for (l in reply.sheet.labels) {
                            val layout = measurer.measure(l.text, TextStyle(color = ink, fontSize = (l.size * k).toSp(), fontWeight = if (l.bold) FontWeight.Bold else FontWeight.Normal))
                            drawText(layout, topLeft = Offset(l.at[0] * k - layout.size.width / 2f, l.at[1] * k - layout.size.height / 2f))
                        }
                    }
                }
                // Drawing properties.
                Column(Modifier.fillMaxWidth().heightIn(max = 260.dp).verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    val papers = reply.papers.ifEmpty { listOf(com.higglerslab.sculpt3d.model.Paper("a4", "A4"), com.higglerslab.sculpt3d.model.Paper("a3", "A3")) }
                    Segmented(papers.map { it.title }, papers.indexOfFirst { it.id == paper }) { paper = papers[it].id }
                    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        listOf("front" to "Front", "top" to "Top", "right" to "Right", "isometric" to "Isometric").forEach { (id, t) ->
                            FilterChip(id in views, { views = if (id in views) views - id else views + id }, { Text(t) })
                        }
                    }
                    SwitchRow("Hidden lines", hidden) { hidden = it }
                    SwitchRow("Dimensions", dims) { dims = it }
                    var scaleMenu by remember { mutableStateOf(false) }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Scale", Modifier.weight(1f))
                        Box {
                            TextButton(onClick = { scaleMenu = true }) { Text(if (scale == null) "Fit (${reply.sheet.scaleText})" else reply.sheet.scaleText) }
                            DropdownMenu(scaleMenu, { scaleMenu = false }) {
                                DropdownMenuItem({ Text("Fit to sheet") }, { scale = null; scaleMenu = false })
                                reply.scales.forEach { s -> DropdownMenuItem({ Text(s.title) }, { scale = s.value; scaleMenu = false }) }
                            }
                        }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("Title", Modifier.weight(1f))
                        CommitField(title, { title = it }, Modifier.width(200.dp), numeric = false, align = TextAlign.Start)
                    }
                    reply.error?.let { Text(it, color = Brand.orange, fontSize = 12.sp) }
                }
            }
        }
    }
}
