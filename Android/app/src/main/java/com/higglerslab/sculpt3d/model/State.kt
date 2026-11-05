package com.higglerslab.sculpt3d.model

import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonElement

/**
 * Immutable mirror of AppCore's state snapshot (`sculpt_doc_state`, AppCore/API.md "State").
 * Every field has a default so a missing or null value never fails the decode.
 */
val CoreJson = Json {
    ignoreUnknownKeys = true
    coerceInputValues = true
    explicitNulls = false
    isLenient = true
}

@Serializable
data class AppState(
    val revision: Long = 0,
    val viewRevision: Long = 0,
    val document: DocumentInfo = DocumentInfo(),
    val mode: String = "modeling",
    val modeTitle: String = "Modeling",
    val idle: Boolean = true,
    val toast: String? = null,
    val uiRequest: UiRequest = UiRequest(),
    val gl: GlInfo = GlInfo(),
    val units: Units = Units(),
    val features: List<FeatureRow> = emptyList(),
    val history: HistoryInfo = HistoryInfo(),
    val selection: Selection = Selection(),
    val selectionBar: List<BarButton> = emptyList(),
    val bodies: List<BodyRow> = emptyList(),
    val items: List<ItemRow> = emptyList(),
    val project: ProjectInfo = ProjectInfo(),
    val draft: Draft? = null,
    val sketch: Sketch? = null,
    val settings: Settings = Settings(),
    val view: ViewState = ViewState(),
    val measure: Measure = Measure(),
    val pointPick: PointPick? = null,
    val tools: List<ToolCategory> = emptyList(),
    val actions: Map<String, Boolean> = emptyMap(),
    val visualize: Visualize = Visualize(),
    val export: ExportInfo = ExportInfo(),
    val displayFormat: DisplayFormat = DisplayFormat(),
) {
    fun enabled(action: String) = actions[action] == true
    val sketching get() = sketch != null
    val measuring get() = view.measuring
    val visualizing get() = view.visualizing
}

@Serializable data class DocumentInfo(val name: String = "Untitled", val path: String = "", val dirty: Boolean = false)
@Serializable data class UiRequest(val serial: Long = 0, val action: String = "")
@Serializable data class GlInfo(val ready: Boolean = false, val error: String = "")
@Serializable data class Units(
    val id: String = "mm", val symbol: String = "mm", val title: String = "Millimeters",
    val scale: Double = 1.0, val imperial: Boolean = false,
)

@Serializable data class FeatureRow(
    val id: String = "", val index: Int = 0, val number: Int = 0, val title: String = "", val symbol: String = "",
    val case: String = "", val suppressed: Boolean = false, val error: String? = null,
    val hasParameters: Boolean = false, val hasSketch: Boolean = false, val editable: Boolean = false,
    val editing: Boolean = false, val isReference: Boolean = false,
)

@Serializable data class HistoryInfo(
    val canUndo: Boolean = false, val canRedo: Boolean = false, val sketchUndo: Boolean = false,
    val emptyTitle: String = "Workspace is empty", val emptySubtitle: String = "Your modeling steps will show up here",
)

@Serializable data class Pick(val kind: String = "body", val body: Int = 0, val index: Int? = null)

@Serializable data class Selection(
    val picks: List<Pick> = emptyList(),
    val bodies: List<Int> = emptyList(),
    val bodyCount: Int = 0, val faceCount: Int = 0, val edgeCount: Int = 0,
    val summary: String = "",
    val items: List<String> = emptyList(),
    val touchedBodies: List<Int> = emptyList(),
    val selectedBody: Int? = null,
    val hover: Pick? = null,
    val hintTouch: String = "", val hintPointer: String = "",
) {
    val isEmpty get() = picks.isEmpty() && items.isEmpty()
}

@Serializable data class BarButton(val id: String = "", val title: String = "", val symbol: String = "", val destructive: Boolean = false)

@Serializable data class Appearance(
    val color: List<Float> = listOf(0.72f, 0.75f, 0.8f),
    val metallic: Float = 0f, val roughness: Float = 0.45f, val opacity: Float = 1f,
    val preset: String? = null, val finish: String = "", val name: String = "",
)

@Serializable data class Properties(
    val size: List<Double> = emptyList(), val volume: Double = 0.0, val area: Double = 0.0,
    val faceCount: Int = 0, val edgeCount: Int = 0,
    val sizeText: String = "", val volumeText: String = "", val areaText: String = "", val facesEdgesText: String = "",
    val captionLines: List<String> = emptyList(),
)

@Serializable data class BodyRow(
    val id: Int = 0, val name: String = "", val hidden: Boolean = false, val selected: Boolean = false,
    val touched: Boolean = false, val appearance: Appearance = Appearance(), val properties: Properties? = null,
)

@Serializable data class ItemRow(
    val id: String = "", val name: String = "", val kind: String = "sketch", val symbol: String = "",
    val hidden: Boolean = false, val selected: Boolean = false, val order: Int? = null, val editTitle: String = "Edit",
)

@Serializable data class ProjectInfo(
    val bodyCount: Int = 0, val hiddenBodyCount: Int = 0, val featureCount: Int = 0,
    val summary: String = "", val isolating: Boolean = false,
)

@Serializable data class Parameter(
    val id: Int = 0, val label: String = "", val value: Double = 0.0, val unit: String = "",
    val isLength: Boolean = false, val displayValue: Double = 0.0, val displayUnit: String = "",
    val text: String = "", val step: Double = 1.0, val binding: String? = null,
)

@Serializable data class Choice(val id: Int = 0, val label: String = "", val options: List<String> = emptyList(), val selected: Int = 0)
@Serializable data class IdTitle(val id: String = "", val title: String = "")
@Serializable data class CombineMode(val selected: String = "newBody", val options: List<IdTitle> = emptyList())
@Serializable data class AxisOption(val aroundV: Boolean = true, val title: String = "")
@Serializable data class RevolveAxis(val aroundV: Boolean = true, val options: List<AxisOption> = emptyList())

@Serializable data class Draft(
    val title: String = "", val featureTitle: String = "", val symbol: String = "", val case: String = "",
    val replacing: String? = null,
    val parameters: List<Parameter> = emptyList(),
    val choices: List<Choice> = emptyList(),
    val combineMode: CombineMode? = null,
    val revolveAxis: RevolveAxis? = null,
    val canEditSketch: Boolean = false,
    val error: String? = null,
    val canApply: Boolean = true,
)

@Serializable data class SketchToolInfo(
    val id: String = "", val title: String = "", val symbol: String = "", val key: String? = null,
    val subtitle: String? = null, val drags: Boolean = false, val active: Boolean = false,
)

@Serializable data class SketchPlane(
    val name: String = "", val origin: List<Double> = emptyList(), val normal: List<Double> = emptyList(),
    val xDir: List<Double> = emptyList(),
)

@Serializable data class SketchRef(val kind: String = "", val index: Int = 0)
@Serializable data class Counts(
    val points: Int = 0, val lines: Int = 0, val circles: Int = 0, val splines: Int = 0,
    val ellipses: Int = 0, val constraints: Int = 0,
)
@Serializable data class SketchStatus(val degreesOfFreedom: Int = 0, val fullyConstrained: Boolean = false)
@Serializable data class SelectedDimension(
    val index: Int = 0, val kind: String = "", val label: String = "", val value: Double = 0.0, val unit: String = "",
    val displayValue: Double = 0.0, val displayUnit: String = "", val text: String = "", val step: Double = 1.0,
)
@Serializable data class AvailableConstraint(val kind: String = "", val title: String = "", val symbol: String = "")
@Serializable data class ConstraintBarInfo(
    val visible: Boolean = false, val noneFits: Boolean = false, val showOffset: Boolean = false,
    val showFillet: Boolean = false, val showDisconnect: Boolean = false, val showMakeConstruction: Boolean = false,
)
@Serializable data class ColumnItem(
    val id: String = "", val title: String = "", val symbol: String = "", val keys: String? = null, val enabled: Boolean = false,
)
@Serializable data class ConstraintInfo(
    val index: Int = 0, val kind: String = "", val title: String = "", val symbol: String = "", val glyph: String = "",
    val isDimension: Boolean = false, val value: Double = 0.0, val unit: String = "", val label: String = "",
    val selected: Boolean = false,
)

@Serializable data class Sketch(
    val tool: String = "select",
    val tools: List<SketchToolInfo> = emptyList(),
    val plane: SketchPlane = SketchPlane(),
    val replacing: String? = null,
    val target: Int? = null,
    val constructionMode: Boolean = false,
    val polygonSides: Int = 6,
    val polygonCircumscribed: Boolean = false,
    val offsetDistance: Double = 5.0, val offsetDistanceText: String = "5",
    val filletRadius: Double = 5.0, val filletRadiusText: String = "5",
    val selection: List<SketchRef> = emptyList(),
    val hover: JsonElement? = null,
    val selectedCounts: Counts = Counts(),
    val counts: Counts = Counts(),
    val isEmpty: Boolean = true,
    val profiles: Int = 0,
    val profilesText: String = "",
    val status: SketchStatus = SketchStatus(),
    val statusText: String = "",
    val escapeTitle: String = "Cancel",
    val canUndo: Boolean = false, val canRedo: Boolean = false, val canTransform: Boolean = false,
    val finishOnReturn: Boolean = true,
    val showConstraints: Boolean = true,
    val chainActive: Boolean = false,
    val selectedDimension: SelectedDimension? = null,
    val availableConstraints: List<AvailableConstraint> = emptyList(),
    val constraintBar: ConstraintBarInfo = ConstraintBarInfo(),
    val constraintColumn: List<ColumnItem> = emptyList(),
    val constraints: List<ConstraintInfo> = emptyList(),
)

@Serializable data class UnitOption(val id: String = "", val symbol: String = "", val title: String = "")
@Serializable data class Snap(
    val grid: Boolean = true, val guideLines: Boolean = true, val guidePoints: Boolean = true,
    val points3D: Boolean = true, val distantEdges: Boolean = true, val showGuidePoints: Boolean = true,
    val showHints: Boolean = true,
)
@Serializable data class Environment(
    val background: List<Float> = listOf(0.9f, 0.91f, 0.93f), val gradient: Boolean = true, val showGrid: Boolean = true,
    val lightIntensity: Float = 1f, val lightAzimuth: Float = 35f, val groundShadow: Boolean = true,
    val lightElevation: Float = 50f, val castShadows: Boolean = true, val shadowSoftness: Float = 0.45f,
    val ambientOcclusion: Boolean = true, val occlusionStrength: Float = 1f, val smoothShading: Boolean = true,
)
@Serializable data class CameraSettings(val orthographic: Boolean = false, val fieldOfView: Float = 40f)
@Serializable data class Variable(
    val id: String = "", val name: String = "", val expression: String = "", val value: Double? = null, val valueText: String = "—",
)
@Serializable data class Settings(
    val units: String = "mm",
    val unitOptions: List<UnitOption> = emptyList(),
    val snapping: Boolean = true,
    val snap: Snap = Snap(),
    val environment: Environment = Environment(),
    val camera: CameraSettings = CameraSettings(),
    val variables: List<Variable> = emptyList(),
)

@Serializable data class DisplayModeInfo(val id: String = "", val title: String = "", val symbol: String = "", val key: String = "")
@Serializable data class GridOption(val value: Double = 0.0, val title: String = "")
@Serializable data class Section(
    val enabled: Boolean = false, val axis: Int = 2, val offset: Double = 0.0, val offsetText: String = "",
    val flipped: Boolean = false, val range: List<Double> = listOf(0.0, 1.0),
)
@Serializable data class ViewState(
    val displayMode: String = "shaded", val displayModeTitle: String = "Shaded", val displayModeSymbol: String = "cube.fill",
    val displayModes: List<DisplayModeInfo> = emptyList(),
    val showEdges: Boolean = true, val showHiddenEdges: Boolean = false, val showSketches: Boolean = true,
    val showDecals: Boolean = true, val showPinnedMeasurements: Boolean = true, val showWorldAxes: Boolean = true,
    val showConstraints: Boolean = true, val gridAtModelBottom: Boolean = false,
    val fixedGrid: Double? = null,
    val gridStep: Double = 10.0, val gridLabel: String = "10\nmm",
    val gridOptions: List<GridOption> = emptyList(),
    val lockGridTitle: String = "Lock Grid Size",
    val section: Section = Section(),
    val measuring: Boolean = false, val measureDeltas: Boolean = false, val visualizing: Boolean = false,
)

@Serializable data class MeasureRow(val label: String = "", val text: String = "")
@Serializable data class CompactRow(val label: String = "", val value: Double = 0.0, val unit: String = "", val text: String = "")
@Serializable data class PointMeasurement(val id: String = "", val label: String = "")
@Serializable data class Measure(
    val rows: List<MeasureRow> = emptyList(),
    val compactRows: List<CompactRow> = emptyList(),
    val message: String = "",
    val kernelError: String = "",
    val pinned: List<String> = emptyList(),
    val recent: List<String> = emptyList(),
    val pointMeasurements: List<PointMeasurement> = emptyList(),
)

@Serializable data class PointPick(
    val purpose: String = "", val title: String = "", val prompt: String = "", val picked: Int = 0,
    val needed: Int = 0, val text: String = "",
)

@Serializable data class ToolInfo(
    val id: String = "", val title: String = "", val symbol: String = "", val hint: String = "",
    val enabled: Boolean = false, val shortcut: String? = null,
)
@Serializable data class ToolCategory(
    val id: String = "", val title: String = "", val symbol: String = "", val onRail: Boolean = true,
    val dot: Boolean = false, val tools: List<ToolInfo> = emptyList(),
)

@Serializable data class UsedMaterial(val appearance: Appearance = Appearance(), val bodies: List<Int> = emptyList(), val count: Int = 0)
@Serializable data class Visualize(
    val targets: List<Int> = emptyList(),
    val targetText: String = "", val appearancePanelText: String = "",
    val current: Appearance = Appearance(),
    val finishIndex: Int = 1, val finishOptions: List<String> = emptyList(),
    val usedMaterials: List<UsedMaterial> = emptyList(),
    val palette: List<List<Float>> = emptyList(),
    val backgrounds: List<List<Float>> = emptyList(),
    val families: List<String> = emptyList(),
)

@Serializable data class ExportKind(
    val id: String = "", val title: String = "", val detail: String = "", val extension: String = "",
    val mimeType: String = "application/octet-stream", val available: Boolean = false, val enabled: Boolean = false,
    val forSlicer: Boolean = false, val fromDrawing: Boolean = false,
)
@Serializable data class ExportInfo(val kinds: List<ExportKind> = emptyList(), val scope: String = "", val bodyCount: Int = 0)

@Serializable data class DisplayFormat(
    val footInch: String = "decimal", val footInchOptions: List<IdTitle> = emptyList(), val footInchEnabled: Boolean = false,
    val degrees: String = "decimal", val degreeOptions: List<IdTitle> = emptyList(),
)

// ---- Annotations (sculpt_view_annotations) ----

@Serializable data class CameraInfo(
    val yaw: Double = 0.0, val pitch: Double = 0.0, val distance: Double = 0.0,
    val right: List<Double> = listOf(1.0, 0.0, 0.0), val up: List<Double> = listOf(0.0, 0.0, 1.0),
    val forward: List<Double> = listOf(0.0, 1.0, 0.0),
    val orthographic: Boolean = false, val fieldOfView: Double = 40.0, val worldPerPoint: Double = 1.0,
)
@Serializable data class Annotation(
    val id: String = "", val kind: String = "", val text: String = "", val x: Float = 0f, val y: Float = 0f,
    val selected: Boolean = false, val constraint: Int = -1, val constraintKind: String = "",
    val value: Double? = null, val editValue: String? = null, val unit: String? = null,
)
@Serializable data class MeasurementLabel(val id: String = "", val text: String = "", val x: Float = 0f, val y: Float = 0f)
@Serializable data class Annotations(
    val viewRevision: Long = 0,
    val size: List<Float> = emptyList(),
    val camera: CameraInfo = CameraInfo(),
    val gridStep: Double = 10.0, val gridLabel: String = "",
    val annotations: List<Annotation> = emptyList(),
    val measurements: List<MeasurementLabel> = emptyList(),
)

// ---- Command results ----

@Serializable data class SearchResult(
    val id: String = "", val title: String = "", val symbol: String = "", val hint: String = "",
    val category: String = "", val categoryTitle: String = "", val enabled: Boolean = false, val shortcut: String? = null,
)
@Serializable data class SearchReply(val ok: Boolean = false, val results: List<SearchResult> = emptyList())

@Serializable data class MaterialPreset(
    val id: String = "", val finish: String = "", val name: String = "", val title: String = "",
    val appearance: Appearance = Appearance(),
)
@Serializable data class MaterialFamily(val family: String = "", val presets: List<MaterialPreset> = emptyList())
@Serializable data class LibraryReply(val families: List<MaterialFamily> = emptyList())

@Serializable data class SheetStroke(val points: List<List<Float>> = emptyList(), val dashed: Boolean = false, val width: Float = 0.35f)
@Serializable data class SheetLabel(val text: String = "", val at: List<Float> = listOf(0f, 0f), val size: Float = 3.5f, val bold: Boolean = false)
@Serializable data class DrawingSheet(
    val size: List<Float> = listOf(297f, 210f), val scale: Double = 1.0, val scaleText: String = "",
    val strokes: List<SheetStroke> = emptyList(), val labels: List<SheetLabel> = emptyList(),
)
@Serializable data class Paper(val id: String = "", val title: String = "", val size: List<Float> = emptyList())
@Serializable data class ScaleOption(val title: String = "", val value: Double = 1.0)
@Serializable data class DrawingReply(
    val ok: Boolean = false, val error: String? = null, val sheet: DrawingSheet = DrawingSheet(),
    val papers: List<Paper> = emptyList(), val views: List<IdTitle> = emptyList(),
    val scales: List<ScaleOption> = emptyList(), val title: String = "",
)
