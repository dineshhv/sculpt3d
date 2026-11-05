package com.higglerslab.sculpt3d.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

/** Brand colours sampled from the Sculpt3D mark (Design/, App/Resources/Assets.xcassets/BrandMark). */
object Brand {
    val navy = Color(0xFF0429A6)
    val blue = Color(0xFF1F45E6)
    val teal = Color(0xFF00D3AB)
    val ink = Color(0xFF051A66)
    val orange = Color(0xFFFF9500)
    val green = Color(0xFF34C759)
}

private val Light = lightColorScheme(
    primary = Brand.blue,
    onPrimary = Color.White,
    primaryContainer = Color(0xFFDCE3FF),
    onPrimaryContainer = Brand.ink,
    secondary = Brand.teal,
    onSecondary = Color.White,
    tertiary = Brand.navy,
    surface = Color(0xFFF7F7F9),
    surfaceVariant = Color(0xFFE9EAEE),
    surfaceContainer = Color(0xFFF2F2F5),
    surfaceContainerHigh = Color(0xFFECECF0),
    background = Color(0xFFF2F3F7),
    onSurface = Color(0xFF16171B),
    onSurfaceVariant = Color(0xFF5D6068),
    outline = Color(0xFFBFC2CA),
    outlineVariant = Color(0xFFDADCE2),
    error = Color(0xFFD93025),
)

private val Dark = darkColorScheme(
    primary = Color(0xFF8AA2FF),
    onPrimary = Color(0xFF001257),
    primaryContainer = Color(0xFF213C9E),
    onPrimaryContainer = Color(0xFFDCE3FF),
    secondary = Brand.teal,
    onSecondary = Color(0xFF00382C),
    tertiary = Color(0xFFB7C4FF),
    surface = Color(0xFF1E1F23),
    surfaceVariant = Color(0xFF2C2E33),
    surfaceContainer = Color(0xFF25262B),
    surfaceContainerHigh = Color(0xFF2E3035),
    background = Color(0xFF141518),
    onSurface = Color(0xFFE6E7EB),
    onSurfaceVariant = Color(0xFFB0B3BB),
    outline = Color(0xFF52555D),
    outlineVariant = Color(0xFF3A3C42),
    error = Color(0xFFFF6B5E),
)

/** True on wide (tablet / landscape) layouts, where panels sit beside the viewport. */
val LocalWide = staticCompositionLocalOf { true }

/** True when there is room for label pills beside side panels (>= 1100 dp). */
val LocalRoomy = staticCompositionLocalOf { true }

@Composable
fun SculptTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) Dark else Light, content = content)
}

fun rgb(c: List<Float>): Color =
    Color(c.getOrElse(0) { 0.7f }.coerceIn(0f, 1f), c.getOrElse(1) { 0.7f }.coerceIn(0f, 1f), c.getOrElse(2) { 0.7f }.coerceIn(0f, 1f))
