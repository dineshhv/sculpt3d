// Native text drawn over the 3D view from sculpt_view_annotations
// (AppCore/API.md "Annotations"): sketch constraint glyphs and dimension values
// (tap to select, double-tap a dimension to type a value), the snapping hint,
// point-measurement labels with ×, and the view cube.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Sculpt3D.Core;
using Windows.Foundation;

namespace Sculpt3D.UI;

public sealed class AnnotationsOverlay : Canvas
{
    private readonly DocumentView _view;
    private string _signature = "";
    private TextBox? _editor;
    private int _editingIndex = -1;

    public AnnotationsOverlay(DocumentView view)
    {
        _view = view;
        // No background: empty areas pass pointer input to the viewport below.
    }

    public void Update()
    {
        var state = _view.Doc.State;
        var ann = _view.Doc.Annotations;
        bool sketching = state.At("sketch") != null;
        bool showMeasurements = state.At("view").Bool("showPinnedMeasurements");
        var annotations = sketching ? ann.At("annotations") : null;
        var measurements = showMeasurements ? ann.At("measurements") : null;
        string sig = annotations.Sig() + measurements.Sig() + ActualTheme;
        if (sig == _signature) return;
        _signature = sig;

        // Keep an open dimension editor; rebuild everything else.
        for (int i = Children.Count - 1; i >= 0; i--)
            if (Children[i] != _editor) Children.RemoveAt(i);

        foreach (var a in annotations.Items()) AddAnnotation(a);
        foreach (var m in measurements.Items()) AddMeasurement(m);
        if (_editor != null && !sketching) CloseEditor();
    }

    private void Place(FrameworkElement e, double x, double y)
    {
        e.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        Canvas.SetLeft(e, Math.Round(x - e.DesiredSize.Width / 2));
        Canvas.SetTop(e, Math.Round(y - e.DesiredSize.Height / 2));
        Children.Add(e);
    }

    private void AddAnnotation(JsonNode? a)
    {
        string kind = a.Str("kind");
        bool selected = a.Bool("selected");
        bool dimension = kind == "dimension";
        var text = new TextBlock
        {
            Text = a.Str("text"),
            FontSize = dimension ? 12 : 11,
            FontWeight = dimension ? FontWeights.SemiBold : FontWeights.Bold,
            Foreground = selected ? new SolidColorBrush(Colors.White) : Ui.Primary,
        };
        if (dimension) text.FontFamily = new FontFamily("Cascadia Mono, Consolas, Segoe UI");
        var label = new Border
        {
            Child = text,
            Padding = new Thickness(dimension ? 6 : 4, 2, dimension ? 6 : 4, 2),
            CornerRadius = new CornerRadius(5),
            Background = selected ? new SolidColorBrush(Colors.Orange) : Ui.PanelBackground,
            BorderBrush = Ui.Stroke,
            BorderThickness = new Thickness(selected ? 0 : 1),
        };
        int index = a.Int("constraint", -1);
        if (kind == "hint" || index < 0)
        {
            label.IsHitTestVisible = false;
        }
        else
        {
            label.Tapped += (_, e) =>
            {
                e.Handled = true;
                _view.Run("sketch.toggleConstraintSelection", new JsonObject { ["index"] = index });
            };
            if (dimension)
            {
                label.DoubleTapped += (_, e) =>
                {
                    e.Handled = true;
                    OpenEditor(index, a.Str("editValue", a.Str("text")), a.Str("unit"), a.Num("x"), a.Num("y"));
                };
                ToolTipService.SetToolTip(label, "Click to select · double-click to type a value");
            }
        }
        Place(label, a.Num("x"), a.Num("y"));
    }

    private void AddMeasurement(JsonNode? m)
    {
        string id = m.Str("id");
        var close = Ui.Plain(Icons.Create("xmark", 10, Ui.Secondary), () => _view.Run("measure.removePoint", new JsonObject { ["id"] = id }), "Remove measurement");
        close.Width = 18;
        close.Height = 18;
        var row = Ui.HStack(4, Ui.Mono(m.Str("text"), 12, FontWeights.SemiBold), close);
        var pill = Ui.Capsule(row, 8, 3);
        Place(pill, m.Num("x"), m.Num("y"));
    }

    // ---- In-place dimension editing ------------------------------------------------

    private void OpenEditor(int index, string value, string unit, double x, double y)
    {
        CloseEditor();
        _editingIndex = index;
        _editor = new TextBox
        {
            Text = value,
            MinWidth = 90,
            FontSize = 12,
            PlaceholderText = unit,
        };
        ToolTipService.SetToolTip(_editor, string.IsNullOrEmpty(unit) ? "Value or expression; Enter applies, Esc cancels" : $"Value in {unit}, or an expression; Enter applies, Esc cancels");
        _editor.KeyDown += (_, e) =>
        {
            if (e.Key == Windows.System.VirtualKey.Enter)
            {
                e.Handled = true;
                Commit();
            }
            else if (e.Key == Windows.System.VirtualKey.Escape)
            {
                e.Handled = true;
                CloseEditor();
                _view.FocusViewport();
            }
        };
        _editor.LostFocus += (_, _) => CloseEditor();
        Place(_editor, x, y);
        _editor.Loaded += (_, _) =>
        {
            _editor?.Focus(FocusState.Programmatic);
            _editor?.SelectAll();
        };
    }

    private void Commit()
    {
        if (_editor == null) return;
        string text = _editor.Text;
        int index = _editingIndex;
        CloseEditor();
        var reply = _view.Run("sketch.setDimension", new JsonObject { ["index"] = index, ["text"] = text });
        _view.FocusViewport();
    }

    private void CloseEditor()
    {
        if (_editor == null) return;
        var e = _editor;
        _editor = null;
        _editingIndex = -1;
        Children.Remove(e);
    }
}

/// <summary>Orientation cube: shows the camera's attitude; click a face to look at it.</summary>
public sealed class ViewCube : Grid
{
    private readonly DocumentView _view;
    private readonly Canvas _canvas = new() { Width = Side, Height = Side };
    private string _signature = "";
    private const double Side = 100;

    private readonly record struct Face(string Name, string Id, double Nx, double Ny, double Nz);

    private static readonly Face[] Faces =
    {
        new("Top", "top", 0, 0, 1),
        new("Bottom", "bottom", 0, 0, -1),
        new("Front", "front", 0, -1, 0),
        new("Back", "back", 0, 1, 0),
        new("Right", "right", 1, 0, 0),
        new("Left", "left", -1, 0, 0),
    };

    public ViewCube(DocumentView view)
    {
        _view = view;
        Width = Side;
        Height = Side;
        Children.Add(_canvas);
        ToolTipService.SetToolTip(this, "Click a face to look at it");
    }

    public void Update()
    {
        var cam = _view.Doc.Annotations.At("camera");
        if (cam == null) return;
        string sig = cam.At("right").Sig() + cam.At("up").Sig() + ActualTheme;
        if (sig == _signature) return;
        _signature = sig;
        var right = cam.At("right").Vec3();
        var up = cam.At("up").Vec3();
        var fwd = cam.At("forward").Vec3();
        _canvas.Children.Clear();

        Point Project((double X, double Y, double Z) p)
        {
            double s = Side * 0.38;
            return new Point(Side / 2 + (p.X * right.X + p.Y * right.Y + p.Z * right.Z) * s,
                             Side / 2 - (p.X * up.X + p.Y * up.Y + p.Z * up.Z) * s);
        }

        var visible = new List<(Face face, Point[] pts, double facing)>();
        foreach (var f in Faces)
        {
            double facing = -(f.Nx * fwd.X + f.Ny * fwd.Y + f.Nz * fwd.Z);
            if (facing <= 0.01) continue;
            // Two in-plane axes for the face.
            (double X, double Y, double Z) u = Math.Abs(f.Nz) > 0.5 ? (1, 0, 0) : (0, 0, 1);
            (double X, double Y, double Z) n = (f.Nx, f.Ny, f.Nz);
            (double X, double Y, double Z) v = (n.Y * u.Z - n.Z * u.Y, n.Z * u.X - n.X * u.Z, n.X * u.Y - n.Y * u.X);
            (double X, double Y, double Z) c = (n.X * 0.5, n.Y * 0.5, n.Z * 0.5);
            (double X, double Y, double Z) Corner(double a, double b) =>
                (c.X + u.X * 0.5 * a + v.X * 0.5 * b, c.Y + u.Y * 0.5 * a + v.Y * 0.5 * b, c.Z + u.Z * 0.5 * a + v.Z * 0.5 * b);
            var pts = new[] { Project(Corner(-1, -1)), Project(Corner(1, -1)), Project(Corner(1, 1)), Project(Corner(-1, 1)) };
            visible.Add((f, pts, facing));
        }
        bool dark = ActualTheme == ElementTheme.Dark;
        foreach (var (face, pts, facing) in visible.OrderBy(t => t.facing))
        {
            double shade = 1 - facing;
            byte g = (byte)Math.Clamp((int)Math.Round((dark ? 0.32 - shade * 0.1 : 0.97 - shade * 0.12) * 255), 0, 255);
            var poly = new Polygon
            {
                Fill = new SolidColorBrush(ColorHelper.FromArgb(235, g, g, (byte)Math.Min(255, g + 3))),
                Stroke = new SolidColorBrush(ColorHelper.FromArgb(64, 0, 0, 0)),
                StrokeThickness = 1,
            };
            foreach (var p in pts) poly.Points.Add(p);
            string id = face.Id;
            poly.Tapped += (_, e) =>
            {
                e.Handled = true;
                _view.Run("view.setOrientation", new JsonObject { ["face"] = id });
            };
            _canvas.Children.Add(poly);
            var mid = new Point(pts.Average(p => p.X), pts.Average(p => p.Y));
            var label = new TextBlock { Text = face.Name, FontSize = 8, FontWeight = FontWeights.SemiBold, Foreground = Ui.Secondary, IsHitTestVisible = false };
            label.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            Canvas.SetLeft(label, mid.X - label.DesiredSize.Width / 2);
            Canvas.SetTop(label, mid.Y - label.DesiredSize.Height / 2);
            _canvas.Children.Add(label);
        }
        // Axis stubs from the cube's corner.
        var o = Project((-0.5, -0.5, -0.5));
        var axes = new (double X, double Y, double Z, Windows.UI.Color C)[]
        {
            (1, 0, 0, Colors.Red), (0, 1, 0, Colors.Green), (0, 0, 1, Colors.Blue),
        };
        foreach (var (x, y, z, c) in axes)
        {
            var e = Project((-0.5 + x * 1.35, -0.5 + y * 1.35, -0.5 + z * 1.35));
            _canvas.Children.Add(new Line
            {
                X1 = o.X, Y1 = o.Y, X2 = e.X, Y2 = e.Y,
                Stroke = new SolidColorBrush(ColorHelper.FromArgb(204, c.R, c.G, c.B)),
                StrokeThickness = 1.5,
                IsHitTestVisible = false,
            });
        }
    }
}
