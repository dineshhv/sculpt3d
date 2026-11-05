// SF Symbol names (as the core and the Apple app use them) → Windows icons.
// Most map to Segoe Fluent Icons / Segoe MDL2 Assets glyphs (the system symbol
// font, "SymbolThemeFontFamily"); geometric ones that have no Fluent glyph use
// a Unicode shape drawn with Segoe UI Symbol. Custom glyphs: sculpt3d.project
// (brand mark image), sculpt3d.visualize (colour wheel), sculpt3d.magnet.
using System;
using System.Collections.Generic;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.UI.Xaml.Shapes;

namespace Sculpt3D.UI;

public static class Icons
{
    /// <summary>A Fluent glyph (code point in the symbol font) or a Unicode shape.</summary>
    private readonly record struct Icon(string Text, bool Fluent);

    private static Icon F(string glyph) => new(glyph, true);
    private static Icon U(string shape) => new(shape, false);

    private static readonly Dictionary<string, Icon> Map = new(StringComparer.Ordinal)
    {
        // Rail and chrome
        ["magnifyingglass"] = F(""),
        ["xmark"] = F(""),
        ["xmark.circle.fill"] = F(""),
        ["square.3.layers.3d"] = F(""),
        ["pencil.and.outline"] = F(""),
        ["pencil"] = F(""),
        ["square.and.pencil"] = F(""),
        ["plus.square"] = F(""),
        ["plus"] = F(""),
        ["minus"] = F(""),
        ["square.on.square.dashed"] = F(""),
        ["move.3d"] = F(""),
        ["wrench.and.screwdriver"] = F(""),
        ["paintpalette"] = F(""),
        ["eye"] = F(""),
        ["eye.slash"] = F(""),
        ["ruler"] = F(""),
        ["square.split.bottomrightquarter"] = F(""),
        ["camera"] = F(""),
        ["camera.aperture"] = F(""),
        ["square.and.arrow.up"] = F(""),
        ["square.and.arrow.down"] = F(""),
        ["clock.arrow.circlepath"] = F(""),
        ["gearshape"] = F(""),
        ["slider.horizontal.3"] = F(""),
        ["trash"] = F(""),
        ["arrow.uturn.backward"] = F(""),
        ["arrow.uturn.forward"] = F(""),
        ["checkmark"] = F(""),
        ["checkmark.seal.fill"] = F(""),
        ["checkmark.circle"] = F(""),
        ["exclamationmark.triangle.fill"] = F(""),
        ["pin"] = F(""),
        ["pin.fill"] = F(""),
        ["pin.slash"] = F(""),
        ["lock"] = F(""),
        ["function"] = F(""),
        ["photo"] = F(""),
        ["photo.badge.plus"] = F(""),
        ["doc.badge.plus"] = F(""),
        ["shippingbox"] = F(""),
        ["folder"] = F(""),
        ["globe"] = F(""),
        ["link.badge.plus"] = F(""),
        ["scissors"] = F(""),
        ["eraser"] = F(""),
        ["arrow.triangle.2.circlepath"] = F(""),
        ["arrow.clockwise.circle"] = F(""),
        ["arrow.up.left.and.arrow.down.right"] = F(""),
        ["plus.square.on.square"] = F(""),
        ["chevron.right"] = F(""),
        ["chevron.down"] = F(""),
        ["star"] = F(""),
        ["star.fill"] = F(""),
        ["info.circle"] = F(""),
        ["doc.on.doc"] = F(""),

        // Geometry, tools and constraints (Unicode shapes)
        ["cube"] = U("⬢"),
        ["cube.fill"] = U("⬢"),
        ["cube.transparent"] = U("⬡"),
        ["cylinder"] = U("▯"),
        ["circle"] = U("○"),
        ["oval"] = U("⬭"),
        ["cone"] = U("▲"),
        ["torus"] = U("◎"),
        ["hexagon"] = U("⬡"),
        ["rectangle"] = U("▭"),
        ["rectangle.center.inset.filled"] = U("▣"),
        ["line.diagonal"] = U("╱"),
        ["point.topleft.down.to.point.bottomright.curvepath"] = U("∿"),
        ["point.topleft.down.to.point.bottomright.filled.curvepath"] = U("∿"),
        ["point.bottomleft.forward.to.point.topright.scurvepath"] = U("∿"),
        ["scribble"] = U("∿"),
        ["circle.dotted"] = U("◌"),
        ["equal"] = U("∥"),
        ["angle"] = U("⊥"),
        ["circle.and.line.horizontal"] = U("⌓"),
        ["smallcircle.filled.circle"] = U("◉"),
        ["smallcircle.circle"] = U("◎"),
        ["circle.circle"] = U("◎"),
        ["arrow.left.and.right"] = U("↔"),
        ["arrow.up.and.down"] = U("↕"),
        ["equal.circle"] = U("="),
        ["arrow.left.and.line.vertical.and.arrow.right"] = U("⇹"),
        ["arrow.left.and.right.square"] = U("⇔"),
        ["square.dashed"] = U("⬚"),
        ["cursorarrow"] = U("↖"),
        ["square.grid.3x3"] = U("⊞"),
        ["viewfinder"] = U("⌖"),
        ["scope"] = U("⌖"),
        ["target"] = U("⌖"),
        ["arrow.turn.up.right"] = U("↱"),
        ["square.on.square"] = U("⧉"),
        ["rectangle.portrait.on.rectangle.portrait.angled"] = U("⧉"),
        ["circle.bottomhalf.filled"] = U("◒"),
        ["triangle.bottomhalf.filled"] = U("◭"),
        ["square.stack.3d.up"] = U("⇑"),
        ["square.stack"] = U("▱"),
        ["rectangle.split.2x1"] = U("◫"),
        ["square.on.square.squareshape.controlhandles"] = U("▱"),
        ["arrow.right.square"] = U("➔"),
        ["square.grid.3x1.below.line.grid.1x2"] = U("▦"),
        ["circle.grid.cross"] = U("✲"),
        ["align.horizontal.center"] = U("⊟"),
        ["arrow.left.and.right.righttriangle.left.righttriangle.right"] = U("⇋"),
        ["arrow.up.square"] = U("⬆"),
        ["square.split.diagonal"] = U("◩"),
        ["square.fill.on.square"] = U("◼"),
        ["square.2.layers.3d.top.filled"] = U("⧈"),
        ["arrow.up.and.down.square"] = U("⇕"),
        ["trapezoid.and.line.vertical"] = U("⏢"),
        ["square"] = U("□"),
        ["sparkles"] = U("✦"),
        ["line.3.horizontal"] = U("≡"),
        ["circle.dotted.and.circle"] = U("◍"),
        ["mountain.2"] = U("⛰"),
        ["square.on.square.intersection.dashed"] = U("⧇"),
        ["minus.square"] = U("⊟"),
        ["plus.square.fill.on.square.fill"] = U("⧉"),
        ["textformat"] = U("T"),
    };

    /// <summary>An icon element for a symbol name, at `size` DIPs.</summary>
    public static FrameworkElement Create(string? symbol, double size = 16, Brush? foreground = null)
    {
        symbol ??= "";
        switch (symbol)
        {
            case "sculpt3d.project":
                return new Image
                {
                    Source = new BitmapImage(new Uri(System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "BrandMark.png"))),
                    Width = size * 1.5,
                    Height = size * 1.5,
                    Stretch = Stretch.Uniform,
                };
            case "sculpt3d.visualize":
                return ColorWheel(size * 1.3);
            case "sculpt3d.magnet":
                return Magnet(size * 1.1, foreground);
        }
        if (!Map.TryGetValue(symbol, out var icon)) icon = Guess(symbol);
        if (icon.Fluent)
        {
            var f = new FontIcon { Glyph = icon.Text, FontSize = size };
            if (foreground != null) f.Foreground = foreground;
            return f;
        }
        var t = new TextBlock
        {
            Text = icon.Text,
            FontSize = size * 1.05,
            FontFamily = new FontFamily("Segoe UI Symbol"),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            TextAlignment = TextAlignment.Center,
        };
        if (foreground != null) t.Foreground = foreground;
        return t;
    }

    /// <summary>Best effort for names missing from the map (new symbols from the core).</summary>
    private static Icon Guess(string symbol)
    {
        if (symbol.Contains("trash")) return F("");
        if (symbol.Contains("eye")) return F("");
        if (symbol.Contains("pencil")) return F("");
        if (symbol.Contains("arrow")) return U("➔");
        if (symbol.Contains("circle")) return U("○");
        if (symbol.Contains("square")) return U("□");
        if (symbol.Contains("cube")) return U("⬢");
        return U("•");
    }

    private static FrameworkElement ColorWheel(double size)
    {
        // Conic gradients aren't available; six coloured wedges make the wheel.
        var grid = new Grid { Width = size, Height = size };
        var colors = new[] { "#FFFF3B30", "#FFFF9500", "#FFFFCC00", "#FF34C759", "#FF32ADE6", "#FF007AFF", "#FFAF52DE", "#FFFF2D55" };
        double r = size / 2;
        for (int i = 0; i < colors.Length; i++)
        {
            double a0 = i * 2 * Math.PI / colors.Length - Math.PI / 2, a1 = (i + 1) * 2 * Math.PI / colors.Length - Math.PI / 2;
            var fig = new PathFigure { StartPoint = new Windows.Foundation.Point(r, r), IsClosed = true };
            fig.Segments.Add(new LineSegment { Point = new Windows.Foundation.Point(r + r * Math.Cos(a0), r + r * Math.Sin(a0)) });
            fig.Segments.Add(new ArcSegment
            {
                Point = new Windows.Foundation.Point(r + r * Math.Cos(a1), r + r * Math.Sin(a1)),
                Size = new Windows.Foundation.Size(r, r),
                SweepDirection = SweepDirection.Clockwise,
            });
            var geo = new PathGeometry();
            geo.Figures.Add(fig);
            grid.Children.Add(new Microsoft.UI.Xaml.Shapes.Path { Data = geo, Fill = Ui.BrushFromHex(colors[i]) });
        }
        grid.Children.Add(new Ellipse
        {
            Width = size * 0.45,
            Height = size * 0.45,
            Fill = new SolidColorBrush(Microsoft.UI.ColorHelper.FromArgb(170, 255, 255, 255)),
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        });
        return grid;
    }

    /// <summary>A horseshoe magnet with red tips (snapping).</summary>
    private static FrameworkElement Magnet(double size, Brush? foreground)
    {
        var fg = foreground ?? Ui.Brush("TextFillColorPrimaryBrush");
        var canvas = new Canvas { Width = size, Height = size };
        double w = size, h = size;
        // The bend: a half ring below the legs.
        var fig = new PathFigure { StartPoint = new Windows.Foundation.Point(w * 0.2, h * 0.55) };
        fig.Segments.Add(new ArcSegment
        {
            Point = new Windows.Foundation.Point(w * 0.8, h * 0.55),
            Size = new Windows.Foundation.Size(w * 0.3, w * 0.3),
            SweepDirection = SweepDirection.Counterclockwise,
        });
        var geo = new PathGeometry();
        geo.Figures.Add(fig);
        canvas.Children.Add(new Microsoft.UI.Xaml.Shapes.Path { Data = geo, Stroke = fg, StrokeThickness = w * 0.18 });
        foreach (double x in new[] { w * 0.2, w * 0.8 })
        {
            var leg = new Rectangle { Width = w * 0.18, Height = h * 0.43, Fill = fg };
            Canvas.SetLeft(leg, x - w * 0.09);
            Canvas.SetTop(leg, h * 0.12);
            canvas.Children.Add(leg);
            var tip = new Rectangle { Width = w * 0.18, Height = h * 0.16, Fill = new SolidColorBrush(Microsoft.UI.Colors.Red) };
            Canvas.SetLeft(tip, x - w * 0.09);
            Canvas.SetTop(tip, h * 0.12);
            canvas.Children.Add(tip);
        }
        return canvas;
    }
}
