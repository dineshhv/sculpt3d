package com.higglerslab.sculpt3d.ui

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.ArrowForward
import androidx.compose.material.icons.automirrored.outlined.Redo
import androidx.compose.material.icons.automirrored.outlined.ShowChart
import androidx.compose.material.icons.automirrored.outlined.Undo
import androidx.compose.material.icons.filled.ViewInAr
import androidx.compose.material.icons.outlined.*
import androidx.compose.ui.graphics.vector.ImageVector

/**
 * The single mapping from the SF Symbol names in the core's state (API.md
 * "Conventions") to Material icons. Unknown names fall back by keyword.
 */
object SymbolIcons {
    private val map: Map<String, ImageVector> = mapOf(
        // Rail and categories
        "magnifyingglass" to Icons.Outlined.Search,
        "square.3.layers.3d" to Icons.Outlined.Layers,
        "square.2.layers.3d.top.filled" to Icons.Outlined.Layers,
        "pencil.and.outline" to Icons.Outlined.Draw,
        "pencil" to Icons.Outlined.ModeEdit,
        "plus.square" to Icons.Outlined.AddBox,
        "square.on.square.dashed" to Icons.Outlined.FilterNone,
        "move.3d" to Icons.Outlined.OpenWith,
        "wrench.and.screwdriver" to Icons.Outlined.Handyman,
        "eye" to Icons.Outlined.Visibility,
        "eye.slash" to Icons.Outlined.VisibilityOff,
        "xmark" to Icons.Outlined.Close,
        "xmark.circle.fill" to Icons.Outlined.Cancel,
        "ruler" to Icons.Outlined.Straighten,
        "view.measure" to Icons.Outlined.Straighten,
        "square.split.bottomrightquarter" to Icons.Outlined.Splitscreen,
        "clock.arrow.circlepath" to Icons.Outlined.History,
        "camera" to Icons.Outlined.PhotoCamera,
        "camera.aperture" to Icons.Outlined.Camera,
        "square.and.arrow.up" to Icons.Outlined.IosShare,
        "square.and.arrow.down" to Icons.Outlined.Download,
        "gearshape" to Icons.Outlined.Settings,
        "trash" to Icons.Outlined.Delete,
        "arrow.uturn.backward" to Icons.AutoMirrored.Outlined.Undo,
        "arrow.uturn.forward" to Icons.AutoMirrored.Outlined.Redo,
        "checkmark" to Icons.Outlined.Check,
        "checkmark.circle" to Icons.Outlined.CheckCircle,
        "checkmark.seal.fill" to Icons.Outlined.Verified,
        "exclamationmark.triangle.fill" to Icons.Outlined.Warning,
        "lock" to Icons.Outlined.Lock,
        "function" to Icons.Outlined.Functions,
        "photo.badge.plus" to Icons.Outlined.AddPhotoAlternate,
        "doc.badge.plus" to Icons.Outlined.NoteAdd,
        "shippingbox" to Icons.Outlined.Inventory2,
        "globe" to Icons.Outlined.Public,
        "mountain.2" to Icons.Outlined.Terrain,
        "sparkles" to Icons.Outlined.AutoAwesome,
        "pin" to Icons.Outlined.PushPin,
        "pin.fill" to Icons.Outlined.PushPin,
        "pin.slash" to Icons.Outlined.LocationOff,
        "scope" to Icons.Outlined.CenterFocusStrong,
        "viewfinder" to Icons.Outlined.CenterFocusStrong,
        "folder" to Icons.Outlined.Folder,
        "plus" to Icons.Outlined.Add,
        "minus" to Icons.Outlined.Remove,
        "star" to Icons.Outlined.StarBorder,
        "star.fill" to Icons.Outlined.Star,
        "line.3.horizontal" to Icons.Outlined.Menu,
        "grid" to Icons.Outlined.GridOn,
        "text.bubble" to Icons.Outlined.ChatBubbleOutline,
        // Primitives and bodies
        "cube" to Icons.Outlined.ViewInAr,
        "cube.fill" to Icons.Filled.ViewInAr,
        "cube.transparent" to Icons.Outlined.ViewInAr,
        "cylinder" to Icons.Outlined.Storage,
        "cone" to Icons.Outlined.ChangeHistory,
        "circle" to Icons.Outlined.Circle,
        "globe.americas" to Icons.Outlined.Public,
        "torus" to Icons.Outlined.DonutLarge,
        "square.stack.3d.up" to Icons.Outlined.Layers,
        "square.stack" to Icons.Outlined.Layers,
        // Sketch tools
        "cursorarrow" to Icons.Outlined.NearMe,
        "line.diagonal" to Icons.Outlined.Straight,
        "point.topleft.down.to.point.bottomright.curvepath" to Icons.Outlined.Gesture,
        "point.topleft.down.to.point.bottomright.filled.curvepath" to Icons.Outlined.Polyline,
        "point.bottomleft.forward.to.point.topright.scurvepath" to Icons.Outlined.Route,
        "scribble" to Icons.Outlined.Gesture,
        "rectangle" to Icons.Outlined.Rectangle,
        "rectangle.center.inset.filled" to Icons.Outlined.CropFree,
        "square" to Icons.Outlined.CropSquare,
        "oval" to Icons.Outlined.Egg,
        "hexagon" to Icons.Outlined.Hexagon,
        "smallcircle.circle" to Icons.Outlined.RadioButtonChecked,
        "smallcircle.filled.circle" to Icons.Outlined.Adjust,
        "scissors" to Icons.Outlined.ContentCut,
        "eraser" to Icons.Outlined.AutoFixOff,
        "square.grid.3x3" to Icons.Outlined.Grid4x4,
        "rectangle.portrait.on.rectangle.portrait.angled" to Icons.Outlined.FlipToBack,
        "square.on.square" to Icons.Outlined.FilterNone,
        "arrow.turn.up.right" to Icons.Outlined.TurnRight,
        "square.dashed" to Icons.Outlined.CropFree,
        "line.diagonal.arrow" to Icons.Outlined.HorizontalRule,
        "textformat" to Icons.Outlined.TextFields,
        "character.textbox" to Icons.Outlined.TextFields,
        // Constraints
        "equal" to Icons.Outlined.DragHandle,
        "angle" to Icons.Outlined.Architecture,
        "circle.and.line.horizontal" to Icons.Outlined.Commit,
        "circle.circle" to Icons.Outlined.DonutLarge,
        "arrow.left.and.right" to Icons.Outlined.SwapHoriz,
        "arrow.up.and.down" to Icons.Outlined.SwapVert,
        "equal.circle" to Icons.Outlined.DragHandle,
        "arrow.left.and.line.vertical.and.arrow.right" to Icons.Outlined.AlignHorizontalCenter,
        "link.badge.plus" to Icons.Outlined.LinkOff,
        // Modeling tools
        "arrow.up.and.down.square" to Icons.Outlined.Height,
        "arrow.up.square" to Icons.Outlined.Upload,
        "arrow.right.square" to Icons.AutoMirrored.Outlined.ArrowForward,
        "arrow.left.and.right.square" to Icons.Outlined.SwapHoriz,
        "circle.bottomhalf.filled" to Icons.Outlined.RoundedCorner,
        "triangle.bottomhalf.filled" to Icons.Outlined.ChangeHistory,
        "square.on.square.squareshape.controlhandles" to Icons.Outlined.Transform,
        "arrow.triangle.2.circlepath" to Icons.Outlined.Sync,
        "arrow.clockwise.circle" to Icons.Outlined.Rotate90DegreesCw,
        "arrow.up.left.and.arrow.down.right" to Icons.Outlined.OpenInFull,
        "arrow.left.and.right.righttriangle.left.righttriangle.right" to Icons.Outlined.Flip,
        "plus.square.on.square" to Icons.Outlined.ContentCopy,
        "square.fill.on.square" to Icons.Outlined.FlipToFront,
        "square.on.square.intersection.dashed" to Icons.Outlined.JoinInner,
        "square.split.diagonal" to Icons.Outlined.CallSplit,
        "rectangle.split.2x1" to Icons.Outlined.VerticalSplit,
        "trapezoid.and.line.vertical" to Icons.Outlined.Landscape,
        "circle.dotted" to Icons.Outlined.Grain,
        "circle.dotted.and.circle" to Icons.Outlined.BlurOn,
        "circle.grid.cross" to Icons.Outlined.Apps,
        "align.horizontal.center" to Icons.Outlined.AlignHorizontalCenter,
        "square.grid.3x1.below.line.grid.1x2" to Icons.Outlined.ViewModule,
        "square.and.pencil" to Icons.Outlined.EditNote,
        "square.union" to Icons.Outlined.JoinFull,
        "square.subtract" to Icons.Outlined.Difference,
        "square.intersect" to Icons.Outlined.JoinInner,
        "sketch.edit" to Icons.Outlined.Draw,
        "sketch.face" to Icons.Outlined.Draw,
        "sketch.plane" to Icons.Outlined.Grid4x4,
        // Display modes
        "square.grid.3x3.square" to Icons.Outlined.GridOn,
        "cube.transparent.fill" to Icons.Outlined.ViewInAr,
        "paintpalette" to Icons.Outlined.Palette,
        "camera.filters" to Icons.Outlined.Gradient,
        "circle.lefthalf.striped.horizontal" to Icons.Outlined.Gradient,
        "waveform.path" to Icons.AutoMirrored.Outlined.ShowChart,
        "photo" to Icons.Outlined.Image,
        "paintbrush" to Icons.Outlined.Brush,
    )

    fun of(symbol: String): ImageVector {
        map[symbol]?.let { return it }
        val s = symbol.lowercase()
        return when {
            s.contains("trash") -> Icons.Outlined.Delete
            s.contains("eye") -> Icons.Outlined.Visibility
            s.contains("cube") -> Icons.Outlined.ViewInAr
            s.contains("circle") -> Icons.Outlined.Circle
            s.contains("rectangle") || s.contains("square") -> Icons.Outlined.CropSquare
            s.contains("arrow") -> Icons.Outlined.OpenWith
            s.contains("pencil") -> Icons.Outlined.Draw
            s.contains("plane") -> Icons.Outlined.Grid4x4
            s.contains("line") -> Icons.Outlined.Straight
            else -> Icons.Outlined.Category
        }
    }
}
