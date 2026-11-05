package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Cancel
import androidx.compose.material.icons.outlined.Check
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.model.Annotation
import com.higglerslab.sculpt3d.session.DocumentSession

/** Places each child centred on its own point (dp, viewport coordinates). */
@Composable
private fun CenteredAt(points: List<Pair<Float, Float>>, content: @Composable () -> Unit) {
    Layout(content, Modifier.fillMaxSize()) { measurables, constraints ->
        val placeables = measurables.map { it.measure(Constraints()) }
        layout(constraints.maxWidth, constraints.maxHeight) {
            placeables.forEachIndexed { i, p ->
                val (x, y) = points.getOrElse(i) { 0f to 0f }
                val px = (x.dp.roundToPx() - p.width / 2).coerceIn(0, (constraints.maxWidth - p.width).coerceAtLeast(0))
                val py = (y.dp.roundToPx() - p.height / 2).coerceIn(0, (constraints.maxHeight - p.height).coerceAtLeast(0))
                p.place(px, py)
            }
        }
    }
}

/** Constraint glyphs and dimension values pinned over the viewport (Swift SketchAnnotationsView). */
@Composable
fun SketchAnnotationsOverlay(session: DocumentSession, ui: UiState) {
    val list = session.annotations.annotations
    if (list.isEmpty()) return
    val c = MaterialTheme.colorScheme
    CenteredAt(list.map { it.x to it.y }) {
        list.forEach { a ->
            val dim = a.kind == "dimension"
            val tappable = a.kind != "hint" && a.constraint >= 0
            Text(
                a.text,
                fontSize = if (dim) 12.sp else 10.sp,
                fontWeight = if (dim) FontWeight.SemiBold else FontWeight.Bold,
                fontFamily = if (dim) FontFamily.Monospace else FontFamily.Default,
                color = if (a.selected) Color.White else c.onSurface,
                modifier = Modifier
                    .clip(RoundedCornerShape(5.dp))
                    .background(if (a.selected) Brand.orange else c.surface.copy(alpha = 0.9f))
                    .then(
                        if (tappable) Modifier.clickable {
                            if (dim) {
                                // Select it (the sketch bar shows its field) and edit in place.
                                if (!a.selected) session.command("sketch.toggleConstraintSelection", "index" to a.constraint)
                                ui.editingDimension = a
                            } else {
                                session.command("sketch.toggleConstraintSelection", "index" to a.constraint)
                            }
                        } else Modifier,
                    )
                    .padding(horizontal = if (dim) 6.dp else 4.dp, vertical = if (tappable) 4.dp else 2.dp),
            )
        }
    }
}

/** The in-place editor for a tapped dimension: a value or expression field at the label. */
@Composable
fun DimensionEditor(session: DocumentSession, ui: UiState, a: Annotation) {
    var error by remember { mutableStateOf<String?>(null) }
    // Follow the label as the camera moves.
    val live = session.annotations.annotations.firstOrNull { it.constraint == a.constraint && it.kind == "dimension" } ?: a
    if (session.state.sketch == null) {
        ui.editingDimension = null
        return
    }
    fun commit(text: String) {
        val r = session.command("sketch.setDimension", "index" to a.constraint, "text" to text)
        if (r.ok) ui.editingDimension = null else error = r.error ?: "Can't use that value"
    }
    Box(Modifier.fillMaxSize()) {
        Scrim { ui.editingDimension = null }
        CenteredAt(listOf(live.x to live.y - 4f)) {
            Column(Modifier.panel(radius = 12.dp, alpha = 0.98f).padding(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    var text by remember { mutableStateOf(a.editValue ?: a.text) }
                    CommitField(
                        a.editValue ?: a.text, { commit(it) }, Modifier.width(110.dp), autoFocus = true, commitOnBlur = false,
                        onChange = { text = it },
                    )
                    Spacer(Modifier.width(4.dp))
                    Text(a.unit ?: "", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    IconButton(onClick = { commit(text) }, Modifier.size(34.dp)) { Icon(Icons.Outlined.Check, "Apply", Modifier.size(18.dp)) }
                    IconButton(onClick = { ui.editingDimension = null }, Modifier.size(34.dp)) { Icon(Icons.Outlined.Close, "Cancel", Modifier.size(18.dp)) }
                }
                error?.let { Text(it, fontSize = 11.sp, color = Brand.orange, modifier = Modifier.widthIn(max = 220.dp)) }
            }
        }
    }
}

/** Point-measurement labels over the viewport, with an × each (Swift MeasurementLabels). */
@Composable
fun MeasurementLabelsOverlay(session: DocumentSession) {
    val list = session.annotations.measurements
    CenteredAt(list.map { it.x to it.y }) {
        list.forEach { m ->
            Row(
                Modifier.capsule().padding(start = 8.dp, end = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(m.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, fontFamily = FontFamily.Monospace, textAlign = TextAlign.Center)
                IconButton(onClick = { session.command("measure.removePoint", "id" to m.id) }, Modifier.size(26.dp)) {
                    Icon(Icons.Outlined.Cancel, "Remove", Modifier.size(14.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
    }
}
