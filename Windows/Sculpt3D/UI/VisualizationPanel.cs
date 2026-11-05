// Visualize mode's right-hand panel: material library, environment and
// camera (VisualizationPanel.swift).
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Sculpt3D.Core;
using Windows.Foundation;

namespace Sculpt3D.UI;

public sealed class VisualizationPanel : StatePanel
{
    private string _query = "";
    private readonly HashSet<string> _collapsed = new();
    private JsonNode? _library;
    private string _libraryQuery = "\u0000";
    private readonly Dictionary<string, SliderBinding> _sliders = new();
    private StackPanel? _tabs;

    public VisualizationPanel(DocumentView view) : base(view) { }

    private static readonly string[] Tabs = { "Material", "Environment", "Camera" };

    protected override string Signature()
    {
        if (S.Str("mode") != "visualize") return "";
        var parts = new List<string> { V.VisualizeTab.ToString(), _query, string.Join(",", _collapsed) };
        switch (V.VisualizeTab)
        {
            case 0:
                var vis = S.At("visualize");
                parts.Add(vis.Str("targetText") + vis.At("usedMaterials").Sig() + vis.At("current").Str("preset") + vis.At("targets").Sig());
                parts.Add(S.Count("bodies").ToString());
                break;
            case 1:
                var env = S.At("settings").At("environment");
                parts.Add(env.Bool("castShadows") + "" + env.Bool("ambientOcclusion") + env.Bool("gradient") + env.Bool("showGrid") +
                          env.Bool("groundShadow") + env.Bool("smoothShading") + env.At("background").Sig());
                break;
            case 2:
                parts.Add(S.At("settings").At("camera").Bool("orthographic").ToString());
                break;
        }
        return string.Join("|", parts);
    }

    protected override void Refresh()
    {
        var env = S.At("settings").At("environment");
        var cam = S.At("settings").At("camera");
        var current = S.At("visualize").At("current");
        foreach (var (key, slider) in _sliders)
        {
            switch (key)
            {
                case "metallic":
                case "roughness":
                case "opacity":
                    slider.Set(current.Num(key));
                    break;
                case "fieldOfView":
                    slider.Set(cam.Num(key));
                    break;
                default:
                    slider.Set(env.Num(key));
                    break;
            }
        }
    }

    protected override UIElement? Build()
    {
        _sliders.Clear();
        if (S.Str("mode") != "visualize") return null;
        _tabs = Ui.Segmented(Tabs, V.VisualizeTab, i => V.SetVisualizeTab(i));
        var content = new StackPanel { Spacing = 10 };
        switch (V.VisualizeTab)
        {
            case 0: MaterialTab(content); break;
            case 1: EnvironmentTab(content); break;
            default: CameraTab(content); break;
        }
        var grid = new Grid { RowSpacing = 10 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.Children.Add(_tabs);
        var scroll = new ScrollViewer { Content = content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, Padding = new Thickness(0, 0, 8, 0) };
        Grid.SetRow(scroll, 1);
        grid.Children.Add(scroll);
        var card = Ui.Card(grid, 12, 16, opaque: true);
        card.Width = 300;
        card.VerticalAlignment = VerticalAlignment.Stretch;
        return card;
    }

    private static TextBlock Section(string title)
    {
        var t = Ui.Text(title, 13, FontWeights.SemiBold);
        t.Margin = new Thickness(0, 6, 0, 0);
        return t;
    }

    // ---- Material -------------------------------------------------------------------

    private Microsoft.UI.Dispatching.DispatcherQueueTimer? _searchTimer;
    private string _pendingQuery = "";

    private Microsoft.UI.Dispatching.DispatcherQueueTimer CreateSearchTimer()
    {
        var t = V.DispatcherQueue.CreateTimer();
        t.Interval = TimeSpan.FromMilliseconds(300);
        t.IsRepeating = false;
        t.Tick += (_, _) =>
        {
            if (_pendingQuery == _query) return;
            _query = _pendingQuery;
            Update();
        };
        return t;
    }

    private JsonNode? Library()
    {
        if (_libraryQuery != _query)
        {
            var reply = V.Doc.Command("materials.library", new JsonObject { ["query"] = _query });
            _library = reply.At("families");
            _libraryQuery = _query;
        }
        return _library;
    }

    private void MaterialTab(StackPanel c)
    {
        var vis = S.At("visualize");
        bool hasTargets = vis.Count("targets") > 0;
        var search = new TextBox { PlaceholderText = "Search materials", Text = _query };
        search.TextChanged += (_, _) =>
        {
            // Rebuild once typing settles; the new box takes the focus back (Loaded).
            _searchTimer ??= CreateSearchTimer();
            _pendingQuery = search.Text.Trim();
            _searchTimer.Stop();
            _searchTimer.Start();
        };
        search.Loaded += (_, _) =>
        {
            if (_query.Length > 0)
            {
                search.Focus(FocusState.Programmatic);
                search.SelectionStart = search.Text.Length;
            }
        };
        c.Children.Add(search);
        c.Children.Add(Ui.Caption(vis.Str("targetText")));

        if (_query.Length == 0 && S.Count("bodies") > 0)
        {
            c.Children.Add(Section("Used materials"));
            foreach (var used in vis.Arr("usedMaterials"))
            {
                var a = used.At("appearance");
                var ids = new JsonArray(used.Arr("bodies").Select(b => (JsonNode?)JsonValue.Create(b.AsInt())).ToArray());
                var row = new Grid { ColumnSpacing = 10, Padding = new Thickness(6, 4, 6, 4) };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.Children.Add(MaterialSphere(a, 28));
                var names = Ui.VStack(0, Ui.Text(a.Str("finish"), 11, null, Ui.Secondary), Ui.Text(a.Str("name"), 13));
                Grid.SetColumn(names, 1);
                row.Children.Add(names);
                var count = Ui.HStack(4, Ui.Text(used.Int("count").ToString(), 12, null, Ui.Secondary), Icons.Create("chevron.right", 10, Ui.Secondary));
                Grid.SetColumn(count, 2);
                row.Children.Add(count);
                c.Children.Add(Ui.Plain(row, () => V.Run("selection.selectBodies", new JsonObject { ["ids"] = ids.DeepClone() }),
                    "Select the bodies using this material"));
            }
        }

        var families = Library().Items().ToList();
        foreach (var fam in families)
        {
            string family = fam.Str("family");
            bool open = !_collapsed.Contains(family) || _query.Length > 0;
            var header = Ui.Row(Section(family), _query.Length == 0 ? Icons.Create(open ? "chevron.down" : "chevron.right", 10, Ui.Secondary) : null);
            c.Children.Add(Ui.Plain(header, () =>
            {
                if (_query.Length > 0) return;
                if (!_collapsed.Remove(family)) _collapsed.Add(family);
                Update();
            }));
            if (!open) continue;
            var grid = new Grid { ColumnSpacing = 10, RowSpacing = 12 };
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var presets = fam.Arr("presets").ToList();
            for (int i = 0; i < presets.Count; i++)
            {
                if (i % 2 == 0) grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
                var tile = (FrameworkElement)PresetTile(presets[i], hasTargets);
                Grid.SetRow(tile, i / 2);
                Grid.SetColumn(tile, i % 2);
                grid.Children.Add(tile);
            }
            c.Children.Add(grid);
        }
        if (families.Count == 0) c.Children.Add(Ui.Caption($"No material matches “{_query}”"));

        c.Children.Add(Section("Custom"));
        var current = vis.At("current");
        var color = current.At("color").Rgb();
        var picker = new ColorPicker { Color = color, IsAlphaEnabled = false, IsMoreButtonVisible = false };
        var flyout = new Flyout { Content = picker };
        flyout.Closed += (_, _) =>
        {
            if (picker.Color != color) V.Run("appearance.set", new JsonObject { ["color"] = J.ColorArray(picker.Color) });
        };
        var colorButton = new Button
        {
            Content = Ui.HStack(8, new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius(11), Background = new SolidColorBrush(color) }, new TextBlock { Text = "Colour" }),
            Flyout = flyout,
            IsEnabled = hasTargets,
        };
        c.Children.Add(Ui.HStack(10, MaterialSphere(current, 40), colorButton));
        c.Children.Add(SliderRow("Metallic", "metallic", 0, 1, current.Num("metallic"), "0.00", v => SetAppearance("metallic", v), hasTargets));
        c.Children.Add(SliderRow("Roughness", "roughness", 0, 1, current.Num("roughness"), "0.00", v => SetAppearance("roughness", v), hasTargets));
        c.Children.Add(SliderRow("Opacity", "opacity", 0.05, 1, current.Num("opacity", 1), "0.00", v => SetAppearance("opacity", v), hasTargets));
        c.Children.Add(Ui.Button("Reset to default", () => V.Run("appearance.set", new JsonObject { ["reset"] = true }), enabled: hasTargets));
    }

    private void SetAppearance(string key, double value) => V.Run("appearance.set", new JsonObject { [key] = value });

    private UIElement PresetTile(JsonNode? preset, bool enabled)
    {
        string id = preset.Str("id");
        bool active = S.At("visualize").At("current").Str("preset") == id;
        var sphere = MaterialSphere(preset.At("appearance"), 64);
        var frame = new Border
        {
            Child = sphere,
            Padding = new Thickness(6),
            CornerRadius = new CornerRadius(12),
            BorderThickness = new Thickness(2),
            BorderBrush = active ? Ui.Accent : new SolidColorBrush(Colors.Transparent),
            Background = active ? Ui.Subtle : new SolidColorBrush(Colors.Transparent),
            HorizontalAlignment = HorizontalAlignment.Center,
        };
        var finish = Ui.Text(preset.Str("finish"), 11, null, Ui.Secondary);
        finish.HorizontalAlignment = HorizontalAlignment.Center;
        var name = Ui.Text(preset.Str("name"), 12);
        name.HorizontalAlignment = HorizontalAlignment.Center;
        var stack = Ui.VStack(4, frame, finish, name);
        return Ui.Plain(stack, () => V.Run("appearance.set", new JsonObject { ["preset"] = id }), preset.Str("title"), enabled);
    }

    /// <summary>A material ball: shaded body, specular highlight sized by roughness, rim.</summary>
    public static FrameworkElement MaterialSphere(JsonNode? appearance, double size)
    {
        var c = appearance.At("color").Rgb();
        double rough = Math.Clamp(appearance.Num("roughness", 0.5), 0.02, 1);
        double metal = Math.Clamp(appearance.Num("metallic"), 0, 1);
        double alpha = Math.Clamp(appearance.Num("opacity", 1), 0.05, 1);
        Windows.UI.Color Lift(Windows.UI.Color k, double t) =>
            ColorHelper.FromArgb(255, (byte)(k.R + (255 - k.R) * t), (byte)(k.G + (255 - k.G) * t), (byte)(k.B + (255 - k.B) * t));
        Windows.UI.Color Scale(Windows.UI.Color k, double t) =>
            ColorHelper.FromArgb(255, (byte)Math.Min(255, k.R * t), (byte)Math.Min(255, k.G * t), (byte)Math.Min(255, k.B * t));

        var grid = new Grid { Width = size, Height = size };
        var body = new RadialGradientBrush
        {
            Center = new Point(0.35, 0.3),
            GradientOrigin = new Point(0.35, 0.3),
            RadiusX = 0.8,
            RadiusY = 0.8,
            MappingMode = BrushMappingMode.RelativeToBoundingBox,
        };
        byte opacityByte = (byte)Math.Round(alpha < 1 ? (0.18 + alpha * 0.5) * 255 : 255);
        var lit = Lift(c, 0.25 * (1 - metal));
        var dark = Scale(c, 0.35 + 0.25 * (1 - metal));
        body.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(opacityByte, lit.R, lit.G, lit.B), Offset = 0 });
        body.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(opacityByte, c.R, c.G, c.B), Offset = 0.5 });
        body.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(opacityByte, dark.R, dark.G, dark.B), Offset = 1 });
        grid.Children.Add(new Ellipse { Fill = body });
        if (metal > 0.01)
        {
            // Studio reflection: a dark horizon band across metals.
            var bands = new LinearGradientBrush { StartPoint = new Point(0.5, 0), EndPoint = new Point(0.5, 1), Opacity = metal * (1 - Math.Min(rough * 1.4, 1) * 0.6) };
            bands.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(0, 255, 255, 255), Offset = 0 });
            bands.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(0, 0, 0, 0), Offset = 0.48 });
            bands.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(150, 0, 0, 0), Offset = 0.56 });
            bands.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(40, 0, 0, 0), Offset = 0.75 });
            bands.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(0, 0, 0, 0), Offset = 1 });
            grid.Children.Add(new Ellipse { Fill = bands });
        }
        double hs = size * (0.12 + 0.35 * rough);
        var highlight = new Ellipse
        {
            Width = hs * 1.25,
            Height = hs,
            Fill = new SolidColorBrush(Colors.White),
            Opacity = (1 - rough) * 0.85 + 0.08,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            RenderTransform = new TranslateTransform { X = -size * 0.17, Y = -size * 0.2 },
        };
        grid.Children.Add(highlight);
        grid.Children.Add(new Ellipse { Stroke = new SolidColorBrush(ColorHelper.FromArgb(40, 0, 0, 0)), StrokeThickness = 0.5 });
        grid.Clip = new RectangleGeometry { Rect = new Rect(0, 0, size, size) };
        return grid;
    }

    private UIElement SliderRow(string title, string key, double min, double max, double value, string format, Action<double> set, bool enabled = true, double step = 0.01)
    {
        var valueText = Ui.Mono(value.ToString(format), 11);
        valueText.Foreground = Ui.Secondary;
        var binding = new SliderBinding(min, max, value, v =>
        {
            valueText.Text = v.ToString(format);
            set(v);
        }, step);
        binding.Slider.IsEnabled = enabled;
        binding.Slider.ValueChanged += (_, e) => valueText.Text = e.NewValue.ToString(format);
        _sliders[key] = binding;
        return Ui.VStack(0, Ui.Row(Ui.Text(title, 12), valueText), binding.Slider);
    }

    // ---- Environment ----------------------------------------------------------------

    private void EnvironmentTab(StackPanel c)
    {
        var env = S.At("settings").At("environment");
        var vis = S.At("visualize");
        void Set(string key, JsonNode? value) => V.Run("settings.setEnvironment", new JsonObject { [key] = value });

        c.Children.Add(Section("Background"));
        var swatches = Ui.HStack(8);
        var bg = env.At("background").Rgb();
        foreach (var b in vis.Arr("backgrounds"))
        {
            var color = b.Rgb();
            bool on = Math.Abs(color.R - bg.R) + Math.Abs(color.G - bg.G) + Math.Abs(color.B - bg.B) < 6;
            var dot = new Ellipse
            {
                Width = 24,
                Height = 24,
                Fill = new SolidColorBrush(color),
                Stroke = on ? Ui.Accent : new SolidColorBrush(ColorHelper.FromArgb(38, 0, 0, 0)),
                StrokeThickness = on ? 3 : 1,
            };
            var rgb = b!.DeepClone();
            swatches.Children.Add(Ui.Plain(dot, () => Set("background", rgb.DeepClone())));
        }
        c.Children.Add(swatches);
        var picker = new ColorPicker { Color = bg, IsAlphaEnabled = false, IsMoreButtonVisible = false };
        var flyout = new Flyout { Content = picker };
        flyout.Closed += (_, _) => { if (picker.Color != bg) Set("background", J.ColorArray(picker.Color)); };
        c.Children.Add(new Button
        {
            Content = Ui.HStack(8, new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius(11), Background = new SolidColorBrush(bg) }, new TextBlock { Text = "Colour" }),
            Flyout = flyout,
        });
        c.Children.Add(Toggle("Gradient", env.Bool("gradient"), v => Set("gradient", v)));
        c.Children.Add(Toggle("Show grid", env.Bool("showGrid"), v => Set("showGrid", v)));

        c.Children.Add(Section("Lighting"));
        c.Children.Add(SliderRow("Intensity", "lightIntensity", 0, 2, env.Num("lightIntensity"), "0.0×", v => Set("lightIntensity", v)));
        c.Children.Add(SliderRow("Direction", "lightAzimuth", -180, 180, env.Num("lightAzimuth"), "0°", v => Set("lightAzimuth", v), step: 1));
        c.Children.Add(SliderRow("Height", "lightElevation", 10, 85, env.Num("lightElevation"), "0°", v => Set("lightElevation", v), step: 1));
        c.Children.Add(Toggle("Ground shadow", env.Bool("groundShadow"), v => Set("groundShadow", v)));

        c.Children.Add(Section("Rendering"));
        c.Children.Add(Toggle("Cast shadows", env.Bool("castShadows"), v => Set("castShadows", v)));
        if (env.Bool("castShadows"))
            c.Children.Add(SliderRow("Shadow softness", "shadowSoftness", 0, 1, env.Num("shadowSoftness"), "0.00", v => Set("shadowSoftness", v)));
        c.Children.Add(Toggle("Ambient occlusion", env.Bool("ambientOcclusion"), v => Set("ambientOcclusion", v)));
        if (env.Bool("ambientOcclusion"))
            c.Children.Add(SliderRow("Occlusion", "occlusionStrength", 0, 1, env.Num("occlusionStrength"), "0.00", v => Set("occlusionStrength", v)));
        c.Children.Add(Toggle("Smooth shading", env.Bool("smoothShading"), v => Set("smoothShading", v)));
        c.Children.Add(Ui.Button("Reset environment", () => V.Run("settings.setEnvironment", new JsonObject { ["reset"] = true })));
    }

    private static ToggleSwitch Toggle(string title, bool on, Action<bool> set)
    {
        var t = new ToggleSwitch { OnContent = title, OffContent = title, IsOn = on };
        t.Toggled += (_, _) => { if (t.IsOn != on) set(t.IsOn); };
        return t;
    }

    // ---- Camera -------------------------------------------------------------------

    private void CameraTab(StackPanel c)
    {
        var cam = S.At("settings").At("camera");
        bool ortho = cam.Bool("orthographic");
        c.Children.Add(Section("Projection"));
        c.Children.Add(Ui.Segmented(new[] { "Perspective", "Orthographic" }, ortho ? 1 : 0,
            i => V.Run("settings.setCamera", new JsonObject { ["orthographic"] = i == 1 })));
        var fov = SliderRow("Field of view", "fieldOfView", 20, 90, cam.Num("fieldOfView", 40), "0°",
            v => V.Run("settings.setCamera", new JsonObject { ["fieldOfView"] = v }), !ortho, 1);
        ((FrameworkElement)fov).Opacity = ortho ? 0.5 : 1;
        c.Children.Add(fov);
        c.Children.Add(Section("Capture"));
        c.Children.Add(Ui.Caption("Renders the current view at 4× resolution with the environment and materials."));
        var capture = Ui.Button("Capture", () => _ = V.SaveScreenshotAsync(4, "Sculpt3D Render"), "camera.aperture", accent: true);
        capture.HorizontalAlignment = HorizontalAlignment.Stretch;
        c.Children.Add(capture);
    }
}
