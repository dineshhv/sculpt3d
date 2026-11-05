package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.Remove
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.composed
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.higglerslab.sculpt3d.R

/** Height of one rail row; icons and their label pills line up on it. */
val RailRow = 40.dp

/** Gives the 3D view keyboard focus back after a field commits. */
object ViewportFocus {
    var request: (() -> Unit)? = null
}

/** How many text fields have focus: hardware keys go to the viewport only when none has. */
object TextFocus {
    var focused by mutableIntStateOf(0)
}

fun Modifier.trackTextFocus(): Modifier = composed {
    var had by remember { mutableStateOf(false) }
    DisposableEffect(Unit) { onDispose { if (had) TextFocus.focused-- } }
    onFocusChanged {
        if (it.isFocused != had) {
            had = it.isFocused
            TextFocus.focused += if (it.isFocused) 1 else -1
        }
    }
}

/** The translucent panel look (Swift `.thickMaterial` in a rounded rectangle). */
@Composable
fun Modifier.panel(radius: Dp = 16.dp, alpha: Float = 0.94f): Modifier {
    val c = MaterialTheme.colorScheme
    return this
        .shadow(6.dp, RoundedCornerShape(radius), clip = false, ambientColor = Color.Black.copy(alpha = 0.15f))
        .clip(RoundedCornerShape(radius))
        .background(c.surface.copy(alpha = alpha))
        .border(BorderStroke(0.5.dp, c.outline.copy(alpha = 0.35f)), RoundedCornerShape(radius))
}

@Composable
fun Modifier.capsule(alpha: Float = 0.94f): Modifier = panel(radius = 50.dp, alpha = alpha)

@Composable
fun SymbolIcon(symbol: String, modifier: Modifier = Modifier, tint: Color = LocalContentColor.current, size: Dp = 20.dp) {
    Icon(SymbolIcons.of(symbol), contentDescription = null, modifier = modifier.size(size), tint = tint)
}

/** Swift ToolButtonStyle: a rounded button with a material background, prominent in the accent colour. */
@Composable
fun ToolButton(
    title: String? = null,
    symbol: String? = null,
    prominent: Boolean = false,
    enabled: Boolean = true,
    destructive: Boolean = false,
    description: String? = title,
    onClick: () -> Unit,
) {
    val c = MaterialTheme.colorScheme
    val k = if (enabled) 1f else 0.45f
    val fg = when {
        prominent -> c.onPrimary
        destructive -> c.error
        else -> c.onSurface
    }.let { it.copy(alpha = it.alpha * k) }
    val bg = if (prominent) c.primary.copy(alpha = if (enabled) 1f else 0.45f) else c.surfaceVariant.copy(alpha = 0.85f)
    Row(
        Modifier
            .heightIn(min = 38.dp)
            .widthIn(min = 38.dp)
            .clip(RoundedCornerShape(10.dp))
            .background(bg)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 9.dp, vertical = 6.dp)
        .semantics { if (description != null) contentDescription = description },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center,
    ) {
        if (symbol != null) SymbolIcon(symbol, tint = fg, size = 18.dp)
        if (symbol != null && title != null) Spacer(Modifier.width(6.dp))
        if (title != null) Text(title, color = fg, fontSize = 14.sp, fontWeight = FontWeight.Medium, maxLines = 1)
    }
}

/** A tool icon: a dark glyph on a soft tile, highlighted when active. */
@Composable
fun RailIcon(symbol: String, active: Boolean = false, enabled: Boolean = true) {
    val c = MaterialTheme.colorScheme
    Box(
        Modifier
            .size(34.dp)
            .clip(RoundedCornerShape(9.dp))
            .background(if (active) c.onSurface.copy(alpha = 0.78f) else Color.Transparent),
        contentAlignment = Alignment.Center,
    ) {
        val tint = if (active) c.surface else c.onSurface.copy(alpha = if (enabled) 0.85f else 0.32f)
        when (symbol) {
            "sculpt3d.project" -> Image(painterResource(R.drawable.brand_mark), null, Modifier.size(26.dp))
            "sculpt3d.visualize" -> VisualizeGlyph()
            "sculpt3d.magnet" -> MagnetGlyph(tint)
            else -> SymbolIcon(symbol, tint = tint, size = 20.dp)
        }
    }
}

@Composable
fun VisualizeGlyph(size: Dp = 22.dp) {
    Canvas(Modifier.size(size)) {
        drawCircle(
            Brush.sweepGradient(listOf(Color.Red, Color(0xFFFF9500), Color.Yellow, Color.Green, Color.Cyan, Color.Blue,
                Color(0xFFAF52DE), Color(0xFFFF2D55), Color.Red)),
        )
        drawCircle(Brush.radialGradient(listOf(Color.White.copy(alpha = 0.8f), Color.Transparent), radius = this.size.minDimension / 2))
    }
}

/** A horseshoe magnet with red tips, for snapping. */
@Composable
fun MagnetGlyph(tint: Color) {
    Canvas(Modifier.size(18.dp)) {
        val w = size.width
        val h = size.height
        drawArc(tint, 0f, 180f, false, topLeft = Offset(w * 0.2f, h * 0.55f - w * 0.3f), size = Size(w * 0.6f, w * 0.6f),
            style = Stroke(width = w * 0.18f))
        for (x in listOf(w * 0.2f, w * 0.8f)) {
            drawRect(tint, Offset(x - w * 0.09f, h * 0.12f), Size(w * 0.18f, h * 0.43f))
            drawRect(Color.Red, Offset(x - w * 0.09f, h * 0.12f), Size(w * 0.18f, h * 0.16f))
        }
    }
}

/** Keyboard shortcut drawn as little key caps, e.g. "⌘U" → [Ctrl][U] on Android. */
@Composable
fun KeyCaps(keys: String) {
    val caps = remember(keys) {
        keys.map { ch ->
            when (ch) {
                '⌘' -> "Ctrl"
                '⌥' -> "Alt"
                '⇧' -> "⇧"
                '⌫' -> "⌫"
                else -> ch.toString()
            }
        }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(2.dp)) {
        caps.forEach {
            Box(
                Modifier
                    .heightIn(min = 14.dp)
                    .widthIn(min = 13.dp)
                    .clip(RoundedCornerShape(3.dp))
                    .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f))
                    .padding(horizontal = 2.dp),
                contentAlignment = Alignment.Center,
            ) {
                Text(it, fontSize = 9.sp, fontWeight = FontWeight.SemiBold, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

/** The label next to a rail icon: a separate rounded pill. */
@Composable
fun LabelPill(title: String, subtitle: String? = null, keys: String? = null, enabled: Boolean = true, dot: Boolean = false) {
    Row(
        Modifier
            .panel(radius = 9.dp, alpha = 0.95f)
            .padding(horizontal = 9.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        Column {
            Text(title, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = if (enabled) 1f else 0.45f))
            if (subtitle != null) Text(subtitle, fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1)
        }
        if (keys != null) KeyCaps(keys)
        if (dot) Box(Modifier.size(6.dp).clip(CircleShape).background(MaterialTheme.colorScheme.primary))
    }
}

/** One entry of a rail: icon and (optionally) its label. */
data class RailItem(
    val id: String,
    val title: String,
    val symbol: String,
    val subtitle: String? = null,
    val keys: String? = null,
    val active: Boolean = false,
    val enabled: Boolean = true,
    val dot: Boolean = false,
    /** A popup (DropdownMenu) anchored to the icon. */
    val anchor: (@Composable () -> Unit)? = null,
    val action: () -> Unit,
)

/**
 * A group of rail items: icons share one rounded column, labels float beside
 * them as separate pills (on the left of the icons when `trailing`).
 */
@Composable
fun RailGroup(items: List<RailItem>, showLabels: Boolean = true, trailing: Boolean = false) {
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.Top) {
        if (trailing && showLabels) Labels(items, Alignment.End)
        Column(
            Modifier.panel(radius = 12.dp).padding(horizontal = 3.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            items.forEach { item ->
                Box(
                    Modifier
                        .height(RailRow)
                        .clip(RoundedCornerShape(9.dp))
                        .clickable(enabled = item.enabled, onClick = item.action)
                        .semantics { contentDescription = item.title },
                    contentAlignment = Alignment.Center,
                ) {
                    RailIcon(item.symbol, item.active, item.enabled)
                    item.anchor?.invoke()
                }
            }
        }
        if (!trailing && showLabels) Labels(items, Alignment.Start)
    }
}

@Composable
private fun Labels(items: List<RailItem>, alignment: Alignment.Horizontal) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp), horizontalAlignment = alignment) {
        items.forEach { item ->
            Box(
                Modifier
                    .height(RailRow)
                    .clickable(
                        enabled = item.enabled,
                        interactionSource = remember { MutableInteractionSource() },
                        indication = null,
                        onClick = item.action,
                    ),
                contentAlignment = if (alignment == Alignment.Start) Alignment.CenterStart else Alignment.CenterEnd,
            ) {
                LabelPill(item.title, item.subtitle, item.keys, item.enabled, item.dot)
            }
        }
    }
}

/** A small rounded text field that commits on Done or when focus leaves. */
@Composable
fun CommitField(
    value: String,
    onCommit: (String) -> Unit,
    modifier: Modifier = Modifier,
    placeholder: String = "",
    numeric: Boolean = true,
    align: TextAlign = TextAlign.End,
    commitOnBlur: Boolean = true,
    autoFocus: Boolean = false,
    onChange: ((String) -> Unit)? = null,
) {
    var field by remember(value) { mutableStateOf(androidx.compose.ui.text.input.TextFieldValue(value)) }
    val text = field.text
    var focused by remember { mutableStateOf(false) }
    val focusManager = LocalFocusManager.current
    val c = MaterialTheme.colorScheme
    val requester = remember { androidx.compose.ui.focus.FocusRequester() }
    if (autoFocus) LaunchedEffect(Unit) { runCatching { requester.requestFocus() } }
    BasicTextField(
        value = field,
        onValueChange = { field = it; if (it.text != text) onChange?.invoke(it.text) },
        singleLine = true,
        textStyle = TextStyle(color = c.onSurface, fontSize = 15.sp, textAlign = align, fontFamily = FontFamily.Default),
        cursorBrush = SolidColor(c.primary),
        keyboardOptions = KeyboardOptions(
            keyboardType = if (numeric) KeyboardType.Text else KeyboardType.Text,
            imeAction = ImeAction.Done,
        ),
        keyboardActions = KeyboardActions(onDone = {
            onCommit(text)
            focusManager.clearFocus()
            ViewportFocus.request?.invoke()
        }),
        modifier = modifier
            .focusRequester(requester)
            .trackTextFocus()
            .onFocusChanged {
                if (focused && !it.isFocused && commitOnBlur && text != value) onCommit(text)
                // Select everything on focus, so typing replaces the value.
                if (!focused && it.isFocused) field = field.copy(selection = androidx.compose.ui.text.TextRange(0, field.text.length))
                focused = it.isFocused
            },
        decorationBox = { inner ->
            Box(
                Modifier
                    .clip(RoundedCornerShape(8.dp))
                    .background(c.surfaceVariant)
                    .border(1.dp, if (focused) c.primary else Color.Transparent, RoundedCornerShape(8.dp))
                    .padding(horizontal = 8.dp, vertical = 7.dp),
                contentAlignment = if (align == TextAlign.End) Alignment.CenterEnd else Alignment.CenterStart,
            ) {
                if (text.isEmpty() && placeholder.isNotEmpty()) Text(placeholder, color = c.onSurfaceVariant, fontSize = 15.sp)
                inner()
            }
        },
    )
}


/** A numeric field with ± steppers (Swift ParameterField): label, −, field, unit, +. */
@Composable
fun ParameterField(
    label: String,
    text: String,
    unit: String,
    bound: Boolean = false,
    labelWidth: Dp = 86.dp,
    onStep: (Int) -> Unit,
    onCommit: (String) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(label, fontSize = 14.sp, modifier = Modifier.width(labelWidth), maxLines = 1)
        StepButton(Icons.Outlined.Remove) { onStep(-1) }
        CommitField(
            text, onCommit, Modifier.weight(1f),
        )
        Text(
            if (bound) "ƒ" else unit, fontSize = 12.sp,
            color = if (bound) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.width(24.dp),
        )
        StepButton(Icons.Outlined.Add) { onStep(1) }
    }
}

@Composable
fun StepButton(icon: androidx.compose.ui.graphics.vector.ImageVector, onClick: () -> Unit) {
    Box(
        Modifier
            .size(34.dp)
            .clip(RoundedCornerShape(8.dp))
            .background(MaterialTheme.colorScheme.surfaceVariant)
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Icon(icon, null, Modifier.size(18.dp)) }
}

/** Segmented picker (Swift `.pickerStyle(.segmented)`). */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Segmented(options: List<String>, selected: Int, modifier: Modifier = Modifier, enabled: Boolean = true, onSelect: (Int) -> Unit) {
    SingleChoiceSegmentedButtonRow(modifier.fillMaxWidth()) {
        options.forEachIndexed { i, title ->
            SegmentedButton(
                selected = i == selected,
                onClick = { onSelect(i) },
                enabled = enabled,
                shape = SegmentedButtonDefaults.itemShape(i, options.size),
                icon = {},
                label = { Text(title, fontSize = if (options.size >= 3) 12.sp else 13.sp, maxLines = 1, softWrap = false, overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis) },
                modifier = Modifier.height(36.dp),
            )
        }
    }
}

@Composable
fun SwitchRow(title: String, checked: Boolean, symbol: String? = null, enabled: Boolean = true, onChange: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(enabled = enabled) { onChange(!checked) }
            .padding(vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (symbol != null) {
            SymbolIcon(symbol, size = 18.dp, tint = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.width(10.dp))
        }
        Text(title, Modifier.weight(1f), fontSize = 14.sp)
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled, modifier = Modifier.scale(0.8f))
    }
}


/** A slider with its title and value (Swift labeledSlider). Sends while dragging, throttled by the caller. */
@Composable
fun SliderRow(title: String, value: Float, range: ClosedFloatingPointRange<Float>, format: (Float) -> String, enabled: Boolean = true, onChange: (Float) -> Unit) {
    var local by remember(value) { mutableFloatStateOf(value) }
    Column(Modifier.fillMaxWidth().alpha(if (enabled) 1f else 0.5f)) {
        Row {
            Text(title, fontSize = 12.sp, modifier = Modifier.weight(1f))
            Text(format(local), fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Slider(
            value = local, onValueChange = { local = it; onChange(it) }, valueRange = range, enabled = enabled,
            modifier = Modifier.height(30.dp),
        )
    }
}

@Composable
fun SectionHeader(title: String, modifier: Modifier = Modifier) {
    Text(title, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, modifier = modifier.padding(top = 6.dp, bottom = 2.dp))
}

@Composable
fun Swatch(color: Color, selected: Boolean, size: Dp = 26.dp, onClick: () -> Unit) {
    Box(
        Modifier
            .size(size)
            .clip(CircleShape)
            .background(color)
            .border(if (selected) 3.dp else 1.dp, if (selected) MaterialTheme.colorScheme.primary else Color.Black.copy(alpha = 0.15f), CircleShape)
            .clickable(onClick = onClick),
    )
}
