package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.KeyboardArrowRight
import androidx.compose.material.icons.outlined.Search
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.model.Appearance
import com.higglerslab.sculpt3d.model.CoreJson
import com.higglerslab.sculpt3d.model.LibraryReply
import com.higglerslab.sculpt3d.model.MaterialFamily
import com.higglerslab.sculpt3d.session.DocumentSession

/** Visualize mode's panel: material library, environment and camera (Swift VisualizationPanel). */
@Composable
fun VisualizationPanel(session: DocumentSession, ui: UiState, modifier: Modifier = Modifier) {
    Column(modifier.panel().padding(12.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Segmented(listOf("Material", "Environment", "Camera"), ui.visualizeTab) { ui.visualizeTab = it }
        Column(Modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            when (ui.visualizeTab) {
                0 -> MaterialTab(session)
                1 -> EnvironmentTab(session)
                else -> CameraTab(session)
            }
            Spacer(Modifier.height(4.dp))
        }
    }
}

@Composable
private fun MaterialTab(session: DocumentSession) {
    val st = session.state
    val viz = st.visualize
    val targets = viz.targets
    var query by remember { mutableStateOf("") }
    val collapsed = remember { mutableStateListOf<String>() }
    val library: List<MaterialFamily> = remember(query) {
        val r = session.command("materials.library", "query" to query.trim())
        try { CoreJson.decodeFromJsonElement(LibraryReply.serializer(), r.json).families } catch (_: Throwable) { emptyList() }
    }
    Row(
        Modifier.clip(RoundedCornerShape(8.dp)).background(MaterialTheme.colorScheme.surfaceVariant).padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(Icons.Outlined.Search, null, Modifier.size(18.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
        CommitField("", {}, Modifier.weight(1f), placeholder = "Search materials", numeric = false, align = TextAlign.Start,
            commitOnBlur = false, onChange = { query = it })
    }
    Text(viz.targetText, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)

    if (query.isBlank() && st.bodies.isNotEmpty()) {
        SectionHeader("Used materials")
        viz.usedMaterials.forEach { used ->
            Row(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(8.dp))
                    .clickable { session.command("selection.selectBodies", "ids" to used.bodies) }
                    .padding(horizontal = 6.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                MaterialSphere(used.appearance, 28.dp)
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(used.appearance.finish, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(used.appearance.name, fontSize = 14.sp)
                }
                Text("${used.count}", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, null, Modifier.size(16.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }

    library.forEach { fam ->
        val open = fam.family !in collapsed || query.isNotBlank()
        Row(
            Modifier.fillMaxWidth().clickable(enabled = query.isBlank()) {
                if (fam.family in collapsed) collapsed.remove(fam.family) else collapsed.add(fam.family)
            }.padding(top = 6.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(fam.family, fontWeight = FontWeight.SemiBold, fontSize = 14.sp, modifier = Modifier.weight(1f))
            if (query.isBlank()) Icon(Icons.AutoMirrored.Outlined.KeyboardArrowRight, null, Modifier.size(18.dp).rotate(if (open) 90f else 0f))
        }
        if (open) {
            fam.presets.chunked(2).forEach { row ->
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    row.forEach { p ->
                        val active = viz.current.preset == p.id
                        Column(
                            Modifier.weight(1f).clip(RoundedCornerShape(12.dp))
                                .clickable(enabled = targets.isNotEmpty()) { session.command("appearance.set", "preset" to p.id) }
                                .padding(4.dp),
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            Box(
                                Modifier.clip(RoundedCornerShape(12.dp))
                                    .background(if (active) MaterialTheme.colorScheme.primary.copy(alpha = 0.15f) else Color.Transparent)
                                    .border(2.dp, if (active) MaterialTheme.colorScheme.primary else Color.Transparent, RoundedCornerShape(12.dp))
                                    .padding(6.dp),
                            ) { MaterialSphere(p.appearance, 60.dp) }
                            Text(p.finish, fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            Text(p.name, fontSize = 12.sp, maxLines = 1)
                        }
                    }
                    if (row.size == 1) Spacer(Modifier.weight(1f))
                }
            }
        }
    }
    if (library.isEmpty()) Hint("No material matches “$query”")

    SectionHeader("Custom")
    val a = viz.current
    Row(verticalAlignment = Alignment.CenterVertically) {
        MaterialSphere(a, 40.dp)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            ColorRow(viz.palette, a.color, enabled = targets.isNotEmpty()) { session.command("appearance.set", "color" to it) }
        }
    }
    Column(Modifier.alpha(if (targets.isEmpty()) 0.5f else 1f)) {
        val metal = rememberThrottle { session.command("appearance.set", "metallic" to it) }
        val rough = rememberThrottle { session.command("appearance.set", "roughness" to it) }
        val opac = rememberThrottle { session.command("appearance.set", "opacity" to it) }
        SliderRow("Metallic", a.metallic, 0f..1f, { "%.2f".format(it) }, targets.isNotEmpty()) { metal(it) }
        SliderRow("Roughness", a.roughness, 0f..1f, { "%.2f".format(it) }, targets.isNotEmpty()) { rough(it) }
        SliderRow("Opacity", a.opacity, 0.05f..1f, { "%.2f".format(it) }, targets.isNotEmpty()) { opac(it) }
    }
    TextButton(onClick = { session.command("appearance.set", "reset" to true) }, enabled = targets.isNotEmpty()) { Text("Reset to default") }
}

/** Palette swatches plus a hex field for any colour. */
@Composable
fun ColorRow(palette: List<List<Float>>, current: List<Float>, enabled: Boolean = true, onPick: (Any) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        palette.forEach { c ->
            Swatch(rgb(c), selected = c.zip(current).all { (x, y) -> kotlin.math.abs(x - y) < 0.01f }, size = 22.dp) { if (enabled) onPick(c) }
        }
    }
    Spacer(Modifier.height(6.dp))
    val hex = "#%02x%02x%02x".format(
        (current.getOrElse(0) { 0f } * 255).toInt().coerceIn(0, 255),
        (current.getOrElse(1) { 0f } * 255).toInt().coerceIn(0, 255),
        (current.getOrElse(2) { 0f } * 255).toInt().coerceIn(0, 255),
    )
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("Hex", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Spacer(Modifier.width(8.dp))
        CommitField(hex, { t ->
            val v = t.trim().let { if (it.startsWith("#")) it else "#$it" }
            if (Regex("#[0-9a-fA-F]{6}").matches(v) && enabled) onPick(v)
        }, Modifier.width(100.dp), numeric = false, align = TextAlign.Start)
    }
}

@Composable
private fun EnvironmentTab(session: DocumentSession) {
    val env = session.state.settings.environment
    fun set(k: String, v: Any) = session.command("settings.setEnvironment", k to v)
    SectionHeader("Background")
    ColorRow(session.state.visualize.backgrounds, env.background) { set("background", it) }
    SwitchRow("Gradient", env.gradient) { set("gradient", it) }
    SwitchRow("Show grid", env.showGrid) { set("showGrid", it) }
    SectionHeader("Lighting")
    val intensity = rememberThrottle { set("lightIntensity", it) }
    val az = rememberThrottle { set("lightAzimuth", it) }
    val el = rememberThrottle { set("lightElevation", it) }
    SliderRow("Intensity", env.lightIntensity, 0f..2f, { "%.1f×".format(it) }) { intensity(it) }
    SliderRow("Direction", env.lightAzimuth, -180f..180f, { "%.0f°".format(it) }) { az(it) }
    SliderRow("Height", env.lightElevation, 10f..85f, { "%.0f°".format(it) }) { el(it) }
    SwitchRow("Ground shadow", env.groundShadow) { set("groundShadow", it) }
    SectionHeader("Rendering")
    SwitchRow("Cast shadows", env.castShadows) { set("castShadows", it) }
    if (env.castShadows) {
        val soft = rememberThrottle { set("shadowSoftness", it) }
        SliderRow("Shadow softness", env.shadowSoftness, 0f..1f, { "%.2f".format(it) }) { soft(it) }
    }
    SwitchRow("Ambient occlusion", env.ambientOcclusion) { set("ambientOcclusion", it) }
    if (env.ambientOcclusion) {
        val occ = rememberThrottle { set("occlusionStrength", it) }
        SliderRow("Occlusion", env.occlusionStrength, 0f..1f, { "%.2f".format(it) }) { occ(it) }
    }
    SwitchRow("Smooth shading", env.smoothShading) { set("smoothShading", it) }
    TextButton(onClick = { session.command("settings.setEnvironment", "reset" to true) }) { Text("Reset environment") }
}

@Composable
private fun CameraTab(session: DocumentSession) {
    val cam = session.state.settings.camera
    val host = LocalHost.current
    var capturing by remember { mutableStateOf(false) }
    SectionHeader("Projection")
    Segmented(listOf("Perspective", "Orthographic"), if (cam.orthographic) 1 else 0) {
        session.command("settings.setCamera", "orthographic" to (it == 1))
    }
    val fov = rememberThrottle { session.command("settings.setCamera", "fieldOfView" to it) }
    SliderRow("Field of view", cam.fieldOfView, 20f..90f, { "%.0f°".format(it) }, !cam.orthographic) { fov(it) }
    SectionHeader("Capture")
    Hint("Renders the current view at 4× resolution with the environment and materials.")
    Button(
        onClick = {
            capturing = true
            host.screenshot(session, 4f, "Sculpt3D Render.png")
            capturing = false
        },
        enabled = !capturing, modifier = Modifier.fillMaxWidth(),
    ) {
        SymbolIcon("camera.aperture", size = 18.dp, tint = MaterialTheme.colorScheme.onPrimary)
        Spacer(Modifier.width(6.dp))
        Text(if (capturing) "Rendering…" else "Capture")
    }
}

/** A material ball drawn with gradients: shaded body, metal bands, a specular highlight sized by roughness. */
@Composable
fun MaterialSphere(a: Appearance, size: Dp) {
    val base = rgb(a.color)
    val rough = a.roughness.coerceIn(0.02f, 1f)
    val metal = a.metallic.coerceIn(0f, 1f)
    val alpha = a.opacity.coerceIn(0.05f, 1f)
    Canvas(Modifier.size(size).clip(CircleShape)) {
        val r = this.size.minDimension / 2
        val c = Offset(r, r)
        fun lift(col: Color, k: Float) = Color(col.red + (1 - col.red) * k, col.green + (1 - col.green) * k, col.blue + (1 - col.blue) * k)
        fun mul(col: Color, k: Float) = Color((col.red * k).coerceIn(0f, 1f), (col.green * k).coerceIn(0f, 1f), (col.blue * k).coerceIn(0f, 1f))
        if (alpha < 1f) {
            drawCircle(base.copy(alpha = 0.18f + alpha * 0.5f))
            drawCircle(Brush.radialGradient(listOf(Color.Transparent, mul(base, 0.6f).copy(alpha = 0.5f)), c, r))
        } else {
            drawCircle(
                Brush.radialGradient(
                    listOf(lift(base, 0.25f * (1 - metal)), base, mul(base, 0.35f + 0.25f * (1 - metal))),
                    Offset(r * 0.7f, r * 0.6f), r * 1.5f,
                ),
            )
            if (metal > 0.01f) {
                fun band(k: Float) = mul(base, k + (0.62f - k) * (rough * 1.4f).coerceAtMost(1f))
                drawCircle(
                    Brush.verticalGradient(
                        0f to band(1.15f), 0.3f to band(0.95f), 0.5f to band(0.75f), 0.56f to band(0.18f), 0.72f to band(0.32f), 1f to band(0.55f),
                    ),
                    alpha = metal,
                )
            }
        }
        val hs = r * 2 * (0.12f + 0.35f * rough)
        drawOval(
            Brush.radialGradient(listOf(Color.White.copy(alpha = (1 - rough) * 0.85f + 0.08f), Color.Transparent),
                Offset(c.x - r * 0.34f, c.y - r * 0.4f), hs * 0.8f),
            topLeft = Offset(c.x - r * 0.34f - hs, c.y - r * 0.4f - hs), size = Size(hs * 2, hs * 2),
        )
        drawCircle(Color.Black.copy(alpha = 0.12f), style = Stroke(width = 1f))
    }
}
