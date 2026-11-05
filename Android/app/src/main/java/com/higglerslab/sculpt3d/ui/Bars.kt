package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.Remove
import androidx.compose.material.icons.outlined.PushPin
import androidx.compose.material.icons.outlined.Verified
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.model.PointPick
import com.higglerslab.sculpt3d.session.DocumentSession
import kotlinx.coroutines.delay

/** Context actions for the current selection (Swift SelectionBar). */
@Composable
fun SelectionBar(session: DocumentSession) {
    val st = session.state
    val sel = st.selection
    if (sel.picks.isEmpty()) {
        Text(
            sel.hintTouch, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, textAlign = TextAlign.Center,
            maxLines = 2, modifier = Modifier.capsule(alpha = 0.85f).padding(horizontal = 14.dp, vertical = 9.dp),
        )
        return
    }
    FlowRow(
        Modifier.panel(radius = 14.dp).padding(6.dp),
        horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Text(sel.summary, fontSize = 14.sp, fontWeight = FontWeight.Medium, modifier = Modifier.padding(horizontal = 8.dp))
        st.selectionBar.forEach { b ->
            val iconOnly = b.id == "selection.clear" || b.destructive
            ToolButton(
                title = if (iconOnly) null else b.title, symbol = b.symbol, destructive = b.destructive, description = b.title,
            ) { session.command(b.id) }
        }
    }
}

/** Floating card that edits the parameters of the operation being drafted (Swift OperationPanel). */
@Composable
fun OperationPanel(session: DocumentSession, wide: Boolean) {
    val d = session.state.draft ?: return
    Column(
        Modifier
            .then(if (wide) Modifier.width(330.dp) else Modifier.fillMaxWidth().widthIn(max = 420.dp))
            .panel()
            .padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SymbolIcon(d.symbol, size = 20.dp)
            Spacer(Modifier.width(8.dp))
            Text(d.title, fontWeight = FontWeight.SemiBold, fontSize = 16.sp, modifier = Modifier.weight(1f))
            if (d.canEditSketch) {
                TextButton(onClick = { session.action("draft.editSketch") }) {
                    SymbolIcon("pencil.and.outline", size = 16.dp)
                    Spacer(Modifier.width(4.dp))
                    Text("Edit Sketch", fontSize = 13.sp)
                }
            }
        }
        d.parameters.forEach { p ->
            ParameterField(
                p.label, p.text, p.displayUnit.ifEmpty { p.unit }, bound = p.binding != null,
                onStep = { session.command("draft.stepParameter", "id" to p.id, "direction" to it) },
                onCommit = { session.command("draft.setParameter", "id" to p.id, "text" to it) },
            )
        }
        d.choices.forEach { c ->
            if (c.label.isNotEmpty()) Text(c.label, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Segmented(c.options, c.selected) { session.command("draft.setChoice", "id" to c.id, "index" to it) }
        }
        d.combineMode?.let { m ->
            Text("Result", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Segmented(m.options.map { it.title }, m.options.indexOfFirst { it.id == m.selected }) {
                session.command("draft.setCombineMode", "mode" to m.options[it].id)
            }
        }
        d.revolveAxis?.let { a ->
            Text("Axis", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Segmented(a.options.map { it.title }, a.options.indexOfFirst { it.aroundV == a.aroundV }) {
                session.command("draft.setRevolveAxis", "aroundV" to a.options[it].aroundV)
            }
        }
        d.error?.let {
            Row(verticalAlignment = Alignment.Top) {
                Icon(Icons.Outlined.Warning, null, tint = Brand.orange, modifier = Modifier.size(16.dp))
                Spacer(Modifier.width(6.dp))
                Text(it, fontSize = 12.sp, color = Brand.orange)
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = { session.action("draft.cancel") }) { Text("Cancel") }
            Spacer(Modifier.weight(1f))
            Button(onClick = { session.action("draft.apply") }, enabled = d.canApply) { Text("Apply") }
        }
    }
}

/** Sketch mode controls: status and ways out; constraints or the selected dimension above (Swift SketchBar). */
@Composable
fun SketchBar(session: DocumentSession, wide: Boolean) {
    val s = session.state.sketch ?: return
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (s.selection.isNotEmpty()) ConstraintBar(session)
        if (s.tool == "polygon") {
            Row(
                Modifier.panel(radius = 14.dp).padding(horizontal = 10.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text("${s.polygonSides} sides", fontSize = 13.sp)
                StepButton(Icons.Outlined.Remove) {
                    session.command("sketch.setPolygonSides", "sides" to (s.polygonSides - 1).coerceAtLeast(3))
                }
                StepButton(Icons.Outlined.Add) {
                    session.command("sketch.setPolygonSides", "sides" to (s.polygonSides + 1).coerceAtMost(24))
                }
                Segmented(listOf("Inscribed", "Circumscribed"), if (s.polygonCircumscribed) 1 else 0, Modifier.width(220.dp)) {
                    session.command("sketch.setPolygonCircumscribed", "on" to (it == 1))
                }
            }
        }
        FlowRow(
            Modifier.panel(radius = 14.dp).padding(6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.CenterHorizontally),
            verticalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            Column(Modifier.padding(horizontal = 6.dp)) {
                if (!s.isEmpty && s.status.fullyConstrained) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Outlined.Verified, null, tint = Brand.green, modifier = Modifier.size(14.dp))
                        Spacer(Modifier.width(3.dp))
                        Text(s.statusText, fontSize = 12.sp, color = Brand.green)
                    }
                } else {
                    Text(s.statusText, fontSize = 12.sp)
                }
                Text(s.profilesText, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            ToolButton(s.escapeTitle) { session.action("sketch.escape") }
            if (s.replacing != null) {
                ToolButton("Done", "checkmark", prominent = true) { session.action("sketch.finish") }
            } else {
                ToolButton("Done", "checkmark", enabled = !s.isEmpty) { session.action("sketch.finishAsItem") }
                ToolButton("Revolve", "arrow.triangle.2.circlepath", enabled = !s.isEmpty && session.state.enabled("sketch.revolve")) {
                    session.action("sketch.revolve")
                }
                ToolButton("Extrude", "square.stack.3d.up", prominent = true, enabled = !s.isEmpty) { session.action("sketch.finish") }
            }
        }
    }
}

/** Constraints that fit the selection, or the value of a selected dimension (Swift ConstraintBar). */
@Composable
private fun ConstraintBar(session: DocumentSession) {
    val s = session.state.sketch ?: return
    val bar = s.constraintBar
    Row(
        Modifier.panel(radius = 14.dp).horizontalScroll(rememberScrollState()).padding(6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        val dim = s.selectedDimension
        if (dim != null) {
            Box(Modifier.width(320.dp)) {
                ParameterField(
                    dim.label, dim.text, dim.displayUnit.ifEmpty { dim.unit },
                    onStep = { dir ->
                        session.command("sketch.setDimension", "index" to dim.index, "text" to fmt(dim.displayValue + dir * dim.step))
                    },
                    onCommit = { session.command("sketch.setDimension", "index" to dim.index, "text" to it) },
                )
            }
        } else {
            s.availableConstraints.forEach { c ->
                ToolButton(c.title, c.symbol) { session.command("sketch.constrain", "kind" to c.kind) }
            }
            if (bar.noneFits && s.availableConstraints.isEmpty()) {
                Text("No constraint fits this selection", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(horizontal = 6.dp))
            }
            if (bar.showOffset) {
                ToolButton("Offset", "square.on.square") { session.action("sketch.offset") }
                CommitField(s.offsetDistanceText, { session.command("sketch.setOffsetDistance", "text" to it) }, Modifier.width(64.dp))
            }
            if (bar.showFillet) {
                ToolButton("Fillet", "arrow.turn.up.right") { session.action("sketch.fillet") }
                CommitField(s.filletRadiusText, { session.command("sketch.setFilletRadius", "text" to it) }, Modifier.width(64.dp))
            }
            if (bar.showDisconnect) ToolButton("Disconnect", "link.badge.plus") { session.action("sketch.disconnect") }
            if (bar.showMakeConstruction) ToolButton("Make Construction", "square.dashed") { session.action("sketch.toggleConstruction") }
        }
        VerticalDivider(Modifier.height(24.dp))
        ToolButton(symbol = "trash", destructive = true, description = "Delete selection") { session.action("sketch.deleteSelection") }
    }
}

@Composable
fun SectionPanel(session: DocumentSession) {
    val sec = session.state.view.section
    Column(
        Modifier.width(310.dp).panel().padding(14.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SymbolIcon("square.split.bottomrightquarter", size = 18.dp)
            Spacer(Modifier.width(6.dp))
            Text("Section View", fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            IconButton(onClick = { session.command("view.section", "enabled" to false) }, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Outlined.Close, "Close", Modifier.size(18.dp))
            }
        }
        Segmented(listOf("X", "Y", "Z"), sec.axis) { session.command("view.section", "axis" to it) }
        val lo = sec.range.getOrElse(0) { 0.0 }.toFloat()
        val hi = sec.range.getOrElse(1) { 1.0 }.toFloat().coerceAtLeast(lo + 0.001f)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Offset", fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            var v by remember(sec.offset) { mutableFloatStateOf(sec.offset.toFloat()) }
            Slider(
                v.coerceIn(lo, hi), { v = it; session.command("view.section", "offset" to it.toDouble()) }, valueRange = lo..hi,
                modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
            )
            Text(sec.offsetText, fontSize = 12.sp, fontFamily = FontFamily.Monospace, modifier = Modifier.width(44.dp))
        }
        SwitchRow("Flip side", sec.flipped) { session.command("view.section", "flipped" to it) }
    }
}

/** Measure mode on phones: the current measurement (Swift MeasurePanel). */
@Composable
fun MeasurePanel(session: DocumentSession) {
    val m = session.state.measure
    Column(Modifier.fillMaxWidth().widthIn(max = 420.dp).panel().padding(14.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            SymbolIcon("ruler", size = 18.dp)
            Spacer(Modifier.width(6.dp))
            Text("Measure", fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
            IconButton(onClick = { session.command("view.setMeasuring", "on" to false) }, modifier = Modifier.size(32.dp)) {
                Icon(Icons.Outlined.Close, "Close", Modifier.size(18.dp))
            }
        }
        if (m.compactRows.isEmpty()) {
            Text(m.kernelError.ifEmpty { m.message }, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        m.compactRows.forEach { r ->
            Row {
                Text(r.label, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.weight(1f))
                Text(r.text, fontWeight = FontWeight.Medium)
                Spacer(Modifier.width(6.dp))
                IconButton(onClick = { session.command("measure.pin", "label" to r.label, "value" to r.text) }, modifier = Modifier.size(22.dp)) {
                    Icon(Icons.Outlined.PushPin, "Pin", Modifier.size(14.dp))
                }
            }
        }
    }
}

/** The prompt shown while picking points, with a way out. */
@Composable
fun PointPickBanner(session: DocumentSession, pick: PointPick) {
    Row(
        Modifier.capsule().padding(start = 14.dp, end = 6.dp, top = 4.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        SymbolIcon("scope", tint = MaterialTheme.colorScheme.primary, size = 18.dp)
        Column(Modifier.weight(1f, fill = false)) {
            Text(pick.title, fontWeight = FontWeight.SemiBold, fontSize = 13.sp)
            Text(pick.text, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        TextButton(onClick = { session.action("pick.cancel") }) { Text("Cancel") }
    }
}

/** Pinned measurements, kept on screen outside Measure mode. */
@Composable
fun PinnedMeasurementsCard(session: DocumentSession) {
    Column(Modifier.panel(radius = 12.dp).padding(horizontal = 12.dp, vertical = 6.dp)) {
        session.state.measure.pinned.forEachIndexed { i, text ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Outlined.PushPin, null, Modifier.size(12.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                Spacer(Modifier.width(6.dp))
                Text(text, fontSize = 12.sp, fontFamily = FontFamily.Monospace)
                IconButton(onClick = { session.command("measure.unpin", "index" to i) }, modifier = Modifier.size(24.dp)) {
                    Icon(Icons.Outlined.Close, "Unpin", Modifier.size(12.dp))
                }
            }
        }
    }
}

/** Throttles a stream of slider values to ~30 per second (API.md "Threading"). */
@Composable
fun rememberThrottle(intervalMs: Long = 33, send: (Float) -> Unit): (Float) -> Unit {
    var last by remember { mutableLongStateOf(0L) }
    var pending by remember { mutableStateOf<Float?>(null) }
    LaunchedEffect(pending) {
        val p = pending ?: return@LaunchedEffect
        delay(intervalMs)
        if (pending == p) {
            send(p)
            pending = null
        }
    }
    return { v ->
        val now = System.currentTimeMillis()
        if (now - last >= intervalMs) {
            last = now
            send(v)
        } else {
            pending = v
        }
    }
}

/** Swift `formatted()`: up to three decimals, no trailing zeros. */
fun fmt(v: Double): String {
    val r = Math.round(v * 1000.0) / 1000.0
    return if (r == Math.floor(r) && kotlin.math.abs(r) < 1e15) r.toLong().toString() else r.toString()
}
