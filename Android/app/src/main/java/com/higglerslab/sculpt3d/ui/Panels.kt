package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Redo
import androidx.compose.material.icons.automirrored.outlined.Undo
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.model.BodyRow
import com.higglerslab.sculpt3d.model.CoreJson
import com.higglerslab.sculpt3d.model.FeatureRow
import com.higglerslab.sculpt3d.model.ItemRow
import com.higglerslab.sculpt3d.model.SearchReply
import com.higglerslab.sculpt3d.model.SearchResult
import com.higglerslab.sculpt3d.session.DocumentSession

// MARK: - Items

/** Bodies, sketches and planes: select, show/hide, rename, and the item actions (Swift ItemsPanel). */
@Composable
fun ItemsPanel(session: DocumentSession, modifier: Modifier = Modifier) {
    val st = session.state
    var renaming by remember { mutableStateOf<Pair<String, String>?>(null) } // (key, current name)
    LazyColumn(modifier, contentPadding = PaddingValues(12.dp), verticalArrangement = Arrangement.spacedBy(2.dp)) {
        item { PanelTitle("Bodies") }
        if (st.bodies.isEmpty()) item { Hint("No bodies yet") }
        items(st.bodies, key = { "b${it.id}" }) { b -> BodyRowView(session, b) { renaming = "body:${b.id}" to b.name } }
        item { Spacer(Modifier.height(8.dp)); PanelTitle("Sketches & Planes") }
        if (st.items.isEmpty()) item { Hint("Sketches kept with Done and construction planes appear here") }
        items(st.items, key = { "i${it.id}" }) { i -> ItemRowView(session, i) { renaming = "item:${i.id}" to i.name } }
    }
    renaming?.let { (key, name) ->
        RenameDialog(name, onDismiss = { renaming = null }) { newName ->
            val (kind, id) = key.split(":", limit = 2)
            if (kind == "body") session.command("body.rename", "id" to id.toInt(), "name" to newName)
            else session.command("item.rename", "id" to id, "name" to newName)
            renaming = null
        }
    }
}

@Composable
fun PanelTitle(text: String, modifier: Modifier = Modifier) =
    Text(text, fontWeight = FontWeight.SemiBold, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = modifier.padding(vertical = 4.dp))

@Composable
fun Hint(text: String) = Text(text, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(vertical = 4.dp))

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun BodyRowView(session: DocumentSession, b: BodyRow, rename: () -> Unit) {
    var menu by remember { mutableStateOf(false) }
    val c = MaterialTheme.colorScheme
    Column(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(if (b.selected) c.primary.copy(alpha = 0.10f) else c.surface.copy(alpha = 0f))
            .combinedClickable(onClick = { session.command("selection.toggleBody", "id" to b.id) }, onLongClick = { menu = true })
            .padding(horizontal = 6.dp, vertical = 6.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(10.dp).clip(CircleShape).background(rgb(b.appearance.color)))
            Spacer(Modifier.width(8.dp))
            Text(
                b.name, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis,
                color = if (b.selected) c.primary else if (b.hidden) c.onSurfaceVariant else c.onSurface,
            )
            IconButton(onClick = { session.command("body.toggleHidden", "id" to b.id) }, modifier = Modifier.size(30.dp)) {
                Icon(if (b.hidden) Icons.Outlined.VisibilityOff else Icons.Outlined.Visibility, if (b.hidden) "Show" else "Hide",
                    Modifier.size(18.dp), tint = c.onSurfaceVariant)
            }
            Box {
                IconButton(onClick = { menu = true }, modifier = Modifier.size(30.dp)) { Icon(Icons.Outlined.MoreVert, "More", Modifier.size(18.dp)) }
                DropdownMenu(menu, { menu = false }) {
                    DropdownMenuItem({ Text("Rename") }, { menu = false; rename() })
                    DropdownMenuItem({ Text(if (b.hidden) "Show" else "Hide") }, { menu = false; session.command("body.toggleHidden", "id" to b.id) })
                    DropdownMenuItem({ Text("Zoom to") }, {
                        menu = false
                        session.command("selection.selectBodies", "ids" to listOf(b.id))
                        session.action("view.zoomToSelection")
                    })
                    DropdownMenuItem({ Text("Delete", color = c.error) }, {
                        menu = false
                        session.command("selection.selectBodies", "ids" to listOf(b.id))
                        session.action("model.delete")
                    })
                }
            }
        }
        if (b.selected) {
            b.properties?.captionLines?.forEach { Text(it, fontSize = 11.sp, color = c.onSurfaceVariant, modifier = Modifier.padding(start = 18.dp)) }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun ItemRowView(session: DocumentSession, i: ItemRow, rename: () -> Unit) {
    var menu by remember { mutableStateOf(false) }
    val c = MaterialTheme.colorScheme
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(if (i.selected) c.primary.copy(alpha = 0.10f) else c.surface.copy(alpha = 0f))
            .combinedClickable(onClick = { session.command("item.toggleSelection", "id" to i.id) }, onLongClick = { menu = true })
            .padding(horizontal = 6.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        SymbolIcon(i.symbol.ifEmpty { if (i.kind == "sketch") "pencil.and.outline" else "square.on.square.dashed" }, size = 16.dp, tint = c.onSurfaceVariant)
        Spacer(Modifier.width(8.dp))
        Text(i.name, Modifier.weight(1f, fill = false), maxLines = 1, overflow = TextOverflow.Ellipsis,
            color = if (i.selected) c.primary else if (i.hidden) c.onSurfaceVariant else c.onSurface)
        i.order?.let {
            Spacer(Modifier.width(6.dp))
            Text("$it", fontSize = 10.sp, modifier = Modifier.clip(CircleShape).background(c.primary.copy(alpha = 0.2f)).padding(horizontal = 5.dp))
        }
        Spacer(Modifier.weight(1f))
        IconButton(onClick = { session.command("item.toggleHidden", "id" to i.id) }, modifier = Modifier.size(30.dp)) {
            Icon(if (i.hidden) Icons.Outlined.VisibilityOff else Icons.Outlined.Visibility, null, Modifier.size(18.dp), tint = c.onSurfaceVariant)
        }
        Box {
            IconButton(onClick = { menu = true }, modifier = Modifier.size(30.dp)) { Icon(Icons.Outlined.MoreVert, "More", Modifier.size(18.dp)) }
            DropdownMenu(menu, { menu = false }) {
                DropdownMenuItem({ Text(i.editTitle) }, { menu = false; session.command("item.edit", "id" to i.id) })
                if (i.kind == "sketch") {
                    DropdownMenuItem({ Text("Extrude") }, { menu = false; session.command("item.extrude", "id" to i.id) })
                    DropdownMenuItem({ Text("Revolve") }, { menu = false; session.command("item.revolve", "id" to i.id) })
                } else if (i.kind == "plane") {
                    DropdownMenuItem({ Text("Sketch on Plane") }, { menu = false; session.command("sketch.startOnItem", "id" to i.id) })
                }
                DropdownMenuItem({ Text("Rename") }, { menu = false; rename() })
                DropdownMenuItem({ Text(if (i.hidden) "Show" else "Hide") }, { menu = false; session.command("item.toggleHidden", "id" to i.id) })
                DropdownMenuItem({ Text("Delete", color = c.error) }, { menu = false; session.command("item.delete", "id" to i.id) })
            }
        }
    }
}

@Composable
fun RenameDialog(current: String, title: String = "Rename", onDismiss: () -> Unit, onDone: (String) -> Unit) {
    var text by remember { mutableStateOf(current) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { OutlinedTextField(text, { text = it }, singleLine = true, modifier = Modifier.trackTextFocus()) },
        confirmButton = { TextButton(onClick = { onDone(text.trim()) }, enabled = text.isNotBlank()) { Text("Done") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } },
    )
}

// MARK: - History

@Composable
fun HistoryPanel(session: DocumentSession, modifier: Modifier = Modifier) {
    val st = session.state
    Column(modifier) {
        Row(Modifier.padding(start = 14.dp, end = 6.dp, top = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("History", fontWeight = FontWeight.SemiBold, fontSize = 16.sp, modifier = Modifier.weight(1f))
            IconButton(onClick = { session.action("edit.undo") }, enabled = st.enabled("edit.undo")) {
                Icon(Icons.AutoMirrored.Outlined.Undo, "Undo")
            }
            IconButton(onClick = { session.action("edit.redo") }, enabled = st.enabled("edit.redo")) {
                Icon(Icons.AutoMirrored.Outlined.Redo, "Redo")
            }
        }
        if (st.features.isEmpty()) {
            Column(Modifier.fillMaxWidth().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                Text(st.history.emptyTitle, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                Text(st.history.emptySubtitle, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center)
            }
        } else {
            LazyColumn(Modifier.fillMaxWidth(), contentPadding = PaddingValues(horizontal = 8.dp, vertical = 4.dp)) {
                items(st.features, key = { it.id }) { f -> FeatureRowView(session, f) }
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun FeatureRowView(session: DocumentSession, f: FeatureRow) {
    var menu by remember { mutableStateOf(false) }
    val c = MaterialTheme.colorScheme
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(8.dp))
            .background(if (f.editing) c.primary.copy(alpha = 0.10f) else c.surface.copy(alpha = 0f))
            .combinedClickable(onClick = { if (f.editable) session.command("feature.open", "id" to f.id) }, onLongClick = { menu = true })
            .padding(horizontal = 6.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text("${f.number}", fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = c.onSurfaceVariant.copy(alpha = 0.7f), modifier = Modifier.width(20.dp))
        if (f.error != null) Icon(Icons.Outlined.Warning, null, Modifier.size(18.dp), tint = Brand.orange)
        else SymbolIcon(f.symbol, size = 18.dp, tint = if (f.editing) c.primary else c.onSurfaceVariant)
        Spacer(Modifier.width(8.dp))
        Column(Modifier.weight(1f)) {
            Text(
                f.title, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                textDecoration = if (f.suppressed) TextDecoration.LineThrough else null,
                color = if (f.suppressed) c.onSurfaceVariant else c.onSurface,
            )
            f.error?.let { Text(it, fontSize = 11.sp, color = Brand.orange, maxLines = 2) }
        }
        Box {
            IconButton(onClick = { menu = true }, modifier = Modifier.size(30.dp)) { Icon(Icons.Outlined.MoreVert, "More", Modifier.size(18.dp)) }
            DropdownMenu(menu, { menu = false }) {
                if (f.hasParameters) DropdownMenuItem({ Text("Edit") }, { menu = false; session.command("feature.edit", "id" to f.id) })
                if (f.hasSketch) DropdownMenuItem({ Text("Edit Sketch") }, { menu = false; session.command("feature.editSketch", "id" to f.id) })
                DropdownMenuItem({ Text(if (f.suppressed) "Unsuppress" else "Suppress") }, { menu = false; session.command("feature.toggleSuppressed", "id" to f.id) })
                DropdownMenuItem({ Text("Delete", color = c.error) }, { menu = false; session.command("feature.delete", "id" to f.id) })
            }
        }
    }
}

// MARK: - Properties

@Composable
fun PropertiesPanel(session: DocumentSession, modifier: Modifier = Modifier) {
    val st = session.state
    val bodies = st.bodies.filter { it.id in st.selection.touchedBodies }
    Column(modifier.verticalScroll(rememberScrollState()).padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Properties", fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
        if (bodies.isEmpty()) {
            PropRow("Project", st.document.name)
            PropRow("Bodies", "${st.project.bodyCount} (${st.project.hiddenBodyCount} hidden)")
            PropRow("Features", "${st.project.featureCount}")
            val opts = st.settings.unitOptions
            Segmented(opts.map { it.symbol }, opts.indexOfFirst { it.id == st.settings.units }) {
                session.command("settings.setUnits", "units" to opts[it].id)
            }
            Hint("Select a body, face or edge to see its properties.")
        } else {
            bodies.forEach { b ->
                Text(b.name, fontWeight = FontWeight.SemiBold, fontSize = 14.sp)
                b.properties?.let { p ->
                    PropRow("Size", p.sizeText)
                    PropRow("Volume", p.volumeText)
                    PropRow("Surface area", p.areaText)
                    PropRow("Faces / edges", p.facesEdgesText)
                }
                Text("Color", fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    st.visualize.palette.forEach { col ->
                        Swatch(rgb(col), selected = col.zip(b.appearance.color).all { (a, c) -> kotlin.math.abs(a - c) < 0.01f }, size = 22.dp) {
                            session.command("appearance.set", "bodies" to listOf(b.id), "color" to col)
                        }
                    }
                }
                val throttled = rememberThrottle { v -> session.command("appearance.set", "bodies" to listOf(b.id), "opacity" to v) }
                SliderRow("Opacity", b.appearance.opacity, 0.05f..1f, { "%.2f".format(it) }) { throttled(it) }
                SwitchRow("Visible", !b.hidden) { session.command("body.setVisible", "id" to b.id, "visible" to it) }
                HorizontalDivider()
            }
        }
    }
}

@Composable
fun PropRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth()) {
        Text(label, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(8.dp).weight(1f))
        Text(value, fontSize = 13.sp, textAlign = TextAlign.End)
    }
}

// MARK: - Measure

/** Measure mode's side panel: the current measurement, deltas and pins. */
@Composable
fun MeasureSidePanel(session: DocumentSession, modifier: Modifier = Modifier) {
    val st = session.state
    val m = st.measure
    Column(modifier.verticalScroll(rememberScrollState()).padding(14.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Measure", fontWeight = FontWeight.SemiBold, fontSize = 16.sp, modifier = Modifier.weight(1f))
            IconButton(onClick = { session.command("view.setMeasuring", "on" to false) }, Modifier.size(30.dp)) { Icon(Icons.Outlined.Cancel, "Close") }
        }
        SwitchRow("Include X, Y, Z Deltas", st.view.measureDeltas) { session.command("view.set", "measureDeltas" to it) }
        if (m.rows.isEmpty()) Hint(m.message)
        m.rows.forEach { r ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(r.label, color = MaterialTheme.colorScheme.onSurfaceVariant, fontSize = 14.sp)
                Spacer(Modifier.weight(1f))
                Text(r.text, fontWeight = FontWeight.Medium, fontSize = 14.sp)
                IconButton(onClick = { session.command("measure.pin", "label" to r.label, "value" to r.text) }, Modifier.size(28.dp)) {
                    Icon(Icons.Outlined.PushPin, "Pin", Modifier.size(16.dp))
                }
            }
        }
        HorizontalDivider()
        Text("Pinned", fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (m.pinned.isEmpty()) Hint("Pin measurements to keep them visible.")
        m.pinned.forEachIndexed { i, t ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(t, fontSize = 13.sp, modifier = Modifier.weight(1f))
                IconButton(onClick = { session.command("measure.unpin", "index" to i) }, Modifier.size(28.dp)) {
                    Icon(Icons.Outlined.LocationOff, "Unpin", Modifier.size(16.dp))
                }
            }
        }
    }
}

// MARK: - Search

/** Ctrl+F palette that finds and runs any tool by name. */
@Composable
fun SearchPalette(session: DocumentSession, ui: UiState) {
    var query by remember { mutableStateOf("") }
    val results = remember(query, session.state.revision) {
        val r = session.command("tool.search", "query" to query.trim())
        try { CoreJson.decodeFromJsonElement(SearchReply.serializer(), r.json).results } catch (_: Throwable) { emptyList() }
    }
    fun run(r: SearchResult) {
        ui.showSearch = false
        session.runTool(r.id)
    }
    Box(Modifier.fillMaxSize()) {
        Scrim { ui.showSearch = false }
        Column(
            Modifier
                .align(Alignment.TopCenter)
                .safeDrawingPadding()
                .padding(top = 60.dp, start = 16.dp, end = 16.dp)
                .widthIn(max = 460.dp)
                .fillMaxWidth()
                .panel(radius = 18.dp, alpha = 0.98f)
                .padding(14.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Outlined.Search, null, tint = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.width(8.dp))
                CommitField(
                    "", { _ -> results.firstOrNull { it.enabled }?.let(::run) }, Modifier.weight(1f),
                    placeholder = "Search tools", numeric = false, align = TextAlign.Start, commitOnBlur = false, autoFocus = true,
                    onChange = { query = it },
                )
            }
            HorizontalDivider(Modifier.padding(vertical = 8.dp))
            LazyColumn(Modifier.heightIn(max = 380.dp)) {
                items(results, key = { it.id }) { r ->
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(8.dp))
                            .clickable(enabled = r.enabled) { run(r) }
                            .alpha(if (r.enabled) 1f else 0.45f)
                            .padding(horizontal = 6.dp, vertical = 6.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        SymbolIcon(r.symbol, size = 20.dp)
                        Spacer(Modifier.width(10.dp))
                        Column(Modifier.weight(1f)) {
                            Text(r.title, fontSize = 14.sp)
                            if (r.hint.isNotEmpty()) Text(r.hint, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        r.shortcut?.let { KeyCaps(it); Spacer(Modifier.width(6.dp)) }
                        Text(r.categoryTitle, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.7f))
                    }
                }
                if (results.isEmpty()) item { Hint("No tool matches “$query”") }
            }
        }
    }
}

// MARK: - Variables

@Composable
fun VariablesPanel(session: DocumentSession, done: () -> Unit) {
    val vars = session.state.settings.variables
    Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Variables", fontWeight = FontWeight.SemiBold, fontSize = 18.sp, modifier = Modifier.weight(1f))
            TextButton(onClick = { session.command("variable.add") }) { Icon(Icons.Outlined.Add, null); Text("Add") }
            TextButton(onClick = done) { Text("Done") }
        }
        Hint("Type a variable's name, or an expression like `width * 2`, into any length or angle field.")
        vars.forEach { v ->
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                CommitField(v.name, { session.command("variable.update", "id" to v.id, "name" to it) }, Modifier.width(100.dp),
                    placeholder = "name", numeric = false, align = TextAlign.Start)
                Text("=")
                CommitField(v.expression, { session.command("variable.update", "id" to v.id, "expression" to it) }, Modifier.weight(1f),
                    placeholder = "expression", numeric = false, align = TextAlign.Start)
                Text(
                    v.valueText, fontSize = 13.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.width(64.dp), textAlign = TextAlign.End,
                    color = if (v.value == null) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                )
                IconButton(onClick = { session.command("variable.remove", "id" to v.id) }, Modifier.size(30.dp)) {
                    Icon(Icons.Outlined.Delete, "Remove", Modifier.size(18.dp))
                }
            }
        }
        if (vars.isEmpty()) Hint("No variables yet.")
    }
}
