package com.higglerslab.sculpt3d.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.Redo
import androidx.compose.material.icons.automirrored.outlined.Undo
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.ExpandMore
import androidx.compose.material.icons.outlined.Menu
import androidx.compose.material.icons.outlined.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.higglerslab.sculpt3d.session.DocumentSession
import com.higglerslab.sculpt3d.session.Documents
import com.higglerslab.sculpt3d.viewport.SculptGLView
import kotlinx.coroutines.delay

/** One document's window: the viewport with the tool rail, panels and bars over it (Swift ContentView). */
@Composable
fun Workspace(session: DocumentSession) {
    val ui = UiStates.of(session)
    val st = session.state
    val wide = LocalWide.current
    Box(Modifier.fillMaxSize()) {
        key(session) {
            AndroidView(
                factory = { ctx -> SculptGLView(ctx, session).also { v -> v.onTouchDown = { ui.closeTransient() } } },
                modifier = Modifier.fillMaxSize(),
            )
        }
        if (st.sketch != null) SketchAnnotationsOverlay(session, ui)
        if (st.view.showPinnedMeasurements && session.annotations.measurements.isNotEmpty()) MeasurementLabelsOverlay(session)

        Box(Modifier.fillMaxSize().safeDrawingPadding().padding(8.dp)) {
            Overlays(session, ui, wide)
        }

        ToolFlyout(session, ui)
        if (ui.showSearch) SearchPalette(session, ui)
        ui.editingDimension?.let { DimensionEditor(session, ui, it) }
    }
    WorkspaceSheets(session, ui)
}

@Composable
private fun Overlays(session: DocumentSession, ui: UiState, wide: Boolean) {
    val st = session.state
    Column(Modifier.fillMaxSize()) {
        Row(Modifier.weight(1f).fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val roomy = LocalRoomy.current
            val panelsOpen = ui.showItems || ui.showHistory || ui.showProperties || st.view.visualizing || st.view.measuring
            // Narrow tablets drop the label pills while side panels are open, so the middle keeps its room.
            val labels = wide && (roomy || !panelsOpen)
            ToolRail(session, ui, labels)
            if (wide && ui.showItems && ui.openMenu == null && st.sketch == null) {
                ItemsPanel(session, Modifier.width(270.dp).fillMaxHeight().panel())
            }
            Column(
                Modifier.weight(1f).fillMaxHeight(),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                TitleBar(session, ui, wide)
                st.pointPick?.let { PointPickBanner(session, it) }
                if (st.view.showPinnedMeasurements && !st.view.measuring && st.measure.pinned.isNotEmpty()) PinnedMeasurementsCard(session)
                if (st.view.measuring) {
                    Text(
                        "Select objects to list available measurements.", fontSize = 13.sp,
                        modifier = Modifier.capsule().padding(horizontal = 14.dp, vertical = 8.dp),
                    )
                }
                if (!st.gl.ready && st.gl.error.isNotEmpty()) ToastView("Graphics: ${st.gl.error}") {}
                val toast = st.toast ?: ui.localToast
                AnimatedVisibility(toast != null, enter = slideInVertically() + fadeIn(), exit = slideOutVertically() + fadeOut()) {
                    if (toast != null) {
                        ToastView(toast, warning = st.toast != null) {
                            if (st.toast != null) session.command("toast.dismiss") else ui.localToast = null
                        }
                    }
                }
                Spacer(Modifier.weight(1f))
                if (wide) BottomBar(session, ui, wide)
            }
            Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                ViewControlsColumn(session, ui, labels, cubeLarge = wide)
                if (st.sketch != null && !st.view.visualizing) {
                    Box(Modifier.weight(1f, fill = false).verticalScroll(rememberScrollState())) { ConstraintColumn(session, labels) }
                }
            }
            if (wide) RightPanel(session, ui)
        }
        if (!wide) {
            Box(Modifier.fillMaxWidth().padding(top = 6.dp), contentAlignment = Alignment.BottomCenter) {
                BottomBar(session, ui, wide)
            }
        }
    }
}

/** Document name (tap: documents), undo/redo and the main menu. */
@Composable
private fun TitleBar(session: DocumentSession, ui: UiState, wide: Boolean) {
    val st = session.state
    Row(
        Modifier.capsule().padding(horizontal = 4.dp, vertical = 2.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            Modifier
                .clip(RoundedCornerShape(50))
                .clickable { ui.showDocuments = true }
                .padding(horizontal = 10.dp, vertical = 8.dp)
                .widthIn(max = if (wide) 260.dp else 84.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                st.document.name, fontWeight = FontWeight.SemiBold, fontSize = 14.sp, maxLines = 1,
                overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f, fill = false),
            )
            if (st.document.dirty) Text(" •", color = MaterialTheme.colorScheme.primary, fontSize = 14.sp)
            if (Documents.sessions.size > 1) Text(" ${Documents.sessions.size}", fontSize = 11.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Icon(Icons.Outlined.ExpandMore, null, Modifier.size(16.dp))
        }
        IconButton(onClick = { session.action("edit.undo") }, enabled = st.enabled("edit.undo"), modifier = Modifier.size(if (wide) 38.dp else 34.dp)) {
            Icon(Icons.AutoMirrored.Outlined.Undo, "Undo", Modifier.size(20.dp))
        }
        IconButton(onClick = { session.action("edit.redo") }, enabled = st.enabled("edit.redo"), modifier = Modifier.size(if (wide) 38.dp else 34.dp)) {
            Icon(Icons.AutoMirrored.Outlined.Redo, "Redo", Modifier.size(20.dp))
        }
        Box {
            IconButton(onClick = { ui.showMenu = true }, modifier = Modifier.size(if (wide) 38.dp else 34.dp)) { Icon(Icons.Outlined.Menu, "Menu", Modifier.size(20.dp)) }
            MainMenu(session, ui)
        }
    }
}

@Composable
private fun BottomBar(session: DocumentSession, ui: UiState, wide: Boolean) {
    val st = session.state
    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
        if (st.view.section.enabled) SectionPanel(session)
        when {
            st.draft != null -> OperationPanel(session, wide)
            st.sketch != null -> SketchBar(session, wide)
            st.view.measuring && !wide -> MeasurePanel(session)
            st.view.visualizing && !wide -> VisualizationPanel(session, ui, Modifier.fillMaxWidth().heightIn(max = 330.dp))
            !st.view.visualizing -> SelectionBar(session)
        }
    }
}

@Composable
private fun RightPanel(session: DocumentSession, ui: UiState) {
    val st = session.state
    when {
        st.view.visualizing -> VisualizationPanel(session, ui, Modifier.width(310.dp).fillMaxHeight())
        st.view.measuring -> MeasureSidePanel(session, Modifier.width(280.dp).fillMaxHeight().panel())
        (ui.showHistory || ui.showProperties) && st.sketch == null -> Column(
            Modifier.width(260.dp).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            if (ui.showProperties) {
                PropertiesPanel(session, Modifier.fillMaxWidth().then(if (ui.showHistory) Modifier.heightIn(max = 340.dp) else Modifier.weight(1f)).panel())
            }
            if (ui.showHistory) HistoryPanel(session, Modifier.fillMaxWidth().weight(1f).panel())
        }
    }
}

@Composable
fun ToastView(text: String, warning: Boolean = true, dismiss: () -> Unit) {
    LaunchedEffect(text) {
        delay(4000)
        dismiss()
    }
    Row(
        Modifier.capsule().padding(start = 14.dp, end = 4.dp, top = 4.dp, bottom = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (warning) Icon(Icons.Outlined.Warning, null, tint = Brand.orange, modifier = Modifier.size(18.dp))
        else Icon(Icons.Outlined.CheckCircle, null, tint = Brand.green, modifier = Modifier.size(18.dp))
        Text(text, fontSize = 13.sp, maxLines = 3, modifier = Modifier.weight(1f, fill = false))
        IconButton(onClick = dismiss, modifier = Modifier.size(32.dp)) { Icon(Icons.Outlined.Close, "Dismiss", Modifier.size(16.dp)) }
    }
}

/** Dims the viewport behind a modal overlay and closes it on tap. */
@Composable
fun Scrim(onDismiss: () -> Unit) {
    Box(
        Modifier
            .fillMaxSize()
            .background(Color.Black.copy(alpha = 0.12f))
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null, onClick = onDismiss),
    )
}
