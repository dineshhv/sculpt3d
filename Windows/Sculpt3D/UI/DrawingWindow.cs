// File ▸ New Drawing: standard views of the model on a sheet (drawing.sheet),
// with its properties and PDF / SVG / DXF export (DrawingView.swift).
using System;
using System.Linq;
using System.Text.Json.Nodes;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Sculpt3D.Core;
using Windows.Foundation;

namespace Sculpt3D.UI;

public sealed class DrawingWindow : Window
{
    private readonly DocumentView _view;

    /// <summary>The document this drawing shows.</summary>
    public DocumentView Owner => _view;
    private readonly Canvas _sheet = new();
    private readonly Grid _paperArea = new() { Background = new SolidColorBrush(ColorHelper.FromArgb(255, 219, 219, 219)) };
    private readonly TextBlock _message = new() { HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
    private readonly Border _properties = new() { Width = 260, Padding = new Thickness(14) };
    private JsonNode? _reply;
    private bool _queued;

    public DrawingWindow(DocumentView view)
    {
        _view = view;
        Title = $"{view.Doc.Name} — Drawing";
        try { AppWindow.SetIcon(System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "Sculpt3D.ico")); } catch { }
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1300, 900));
        if (!view.Drawing.ContainsKey("title")) view.Drawing["title"] = view.Doc.Name;

        var exportMenu = new MenuFlyout();
        exportMenu.Items.Add(ItemsPanel.MenuItem("PDF…", () => _ = view.ExportKindAsync("pdf", drawing: true)));
        exportMenu.Items.Add(ItemsPanel.MenuItem("SVG…", () => _ = view.ExportKindAsync("svg", drawing: true)));
        exportMenu.Items.Add(ItemsPanel.MenuItem("DXF…", () => _ = view.ExportKindAsync("dxfDrawing", drawing: true)));
        var export = new DropDownButton { Content = Ui.HStack(6, Icons.Create("square.and.arrow.up", 14), new TextBlock { Text = "Export" }), Flyout = exportMenu };
        var toggleProps = Ui.Button("Properties", () =>
        {
            _properties.Visibility = _properties.Visibility == Visibility.Visible ? Visibility.Collapsed : Visibility.Visible;
        }, "slider.horizontal.3");
        var done = Ui.Button("Done", Close);
        var toolbar = Ui.Row(Ui.Headline("Drawing"), Ui.HStack(8, toggleProps, export, done));
        toolbar.Padding = new Thickness(12);

        _paperArea.Children.Add(_sheet);
        _paperArea.Children.Add(_message);
        _paperArea.SizeChanged += (_, _) => Draw();

        var body = new Grid();
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        body.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        body.Children.Add(_paperArea);
        Grid.SetColumn(_properties, 1);
        _properties.Background = Ui.OpaquePanelBackground;
        body.Children.Add(_properties);

        var root = new Grid { Background = Ui.Brush("SolidBackgroundFillColorBaseBrush") };
        root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        root.Children.Add(toolbar);
        Grid.SetRow(body, 1);
        root.Children.Add(body);
        Content = root;

        view.Doc.StateChanged += QueueReload;
        Closed += (_, _) => view.Doc.StateChanged -= QueueReload;
        Reload();
    }

    private void QueueReload()
    {
        if (_queued) return;
        _queued = true;
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            _queued = false;
            Reload();
        });
    }

    private void Reload()
    {
        _reply = _view.Doc.Command("drawing.sheet", new JsonObject { ["drawing"] = _view.DrawingSettingsJson() });
        BuildProperties();
        Draw();
    }

    private void Draw()
    {
        _sheet.Children.Clear();
        var sheet = _reply.At("sheet");
        if (!_reply.Bool("ok") || sheet == null)
        {
            _message.Text = _reply.Str("error", "Add a body to the model to draw it.");
            _message.Visibility = Visibility.Visible;
            return;
        }
        _message.Visibility = Visibility.Collapsed;
        double w = sheet.At("size").At(0).AsNum(420), h = sheet.At("size").At(1).AsNum(297);
        double aw = _paperArea.ActualWidth - 40, ah = _paperArea.ActualHeight - 40;
        if (aw <= 0 || ah <= 0) return;
        double k = Math.Min(aw / w, ah / h);
        _sheet.Width = w * k;
        _sheet.Height = h * k;
        _sheet.HorizontalAlignment = HorizontalAlignment.Center;
        _sheet.VerticalAlignment = VerticalAlignment.Center;
        _sheet.Background = new SolidColorBrush(Colors.White);
        var ink = new SolidColorBrush(ColorHelper.FromArgb(255, 20, 23, 31));
        foreach (var s in sheet.Arr("strokes"))
        {
            var line = new Polyline
            {
                Stroke = ink,
                StrokeThickness = Math.Max(s.Num("width", 0.35) * k, 0.5),
                StrokeLineJoin = PenLineJoin.Round,
                StrokeStartLineCap = PenLineCap.Round,
                StrokeEndLineCap = PenLineCap.Round,
            };
            if (s.Bool("dashed"))
            {
                double t = line.StrokeThickness;
                line.StrokeDashArray = new DoubleCollection { 1.6 * k / t, 1.0 * k / t };
            }
            foreach (var p in s.Arr("points")) line.Points.Add(new Point(p.At(0).AsNum() * k, p.At(1).AsNum() * k));
            _sheet.Children.Add(line);
        }
        foreach (var l in sheet.Arr("labels"))
        {
            var t = new TextBlock
            {
                Text = l.Str("text"),
                FontSize = Math.Max(l.Num("size", 3.5) * k, 4),
                FontWeight = l.Bool("bold") ? FontWeights.Bold : FontWeights.Normal,
                Foreground = ink,
            };
            t.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            Canvas.SetLeft(t, l.At("at").At(0).AsNum() * k - t.DesiredSize.Width / 2);
            Canvas.SetTop(t, l.At("at").At(1).AsNum() * k - t.DesiredSize.Height / 2);
            _sheet.Children.Add(t);
        }
    }

    private void Set(string key, JsonNode? value)
    {
        _view.Drawing[key] = value;
        Reload();
    }

    private void BuildProperties()
    {
        var d = _view.Drawing;
        var stack = new StackPanel { Spacing = 10 };
        stack.Children.Add(Ui.Headline("Sheet"));
        var title = new TextBox { Header = "Title", Text = d["title"]?.ToString() ?? _view.Doc.Name };
        title.LostFocus += (_, _) => { if (title.Text != (d["title"]?.ToString() ?? "")) Set("title", title.Text); };
        title.KeyDown += (_, e) => { if (e.Key == Windows.System.VirtualKey.Enter) Set("title", title.Text); };
        stack.Children.Add(title);

        var papers = _reply.Arr("papers").ToList();
        var paper = new ComboBox { Header = "Paper", HorizontalAlignment = HorizontalAlignment.Stretch };
        foreach (var p in papers) paper.Items.Add(p.Str("title"));
        paper.SelectedIndex = Math.Max(0, papers.FindIndex(p => p.Str("id") == (d["paper"]?.ToString() ?? "a3")));
        paper.SelectionChanged += (_, _) => { if (paper.SelectedIndex >= 0) Set("paper", papers[paper.SelectedIndex].Str("id")); };
        stack.Children.Add(paper);

        var scales = _reply.Arr("scales").ToList();
        var scale = new ComboBox { Header = "Scale", HorizontalAlignment = HorizontalAlignment.Stretch };
        scale.Items.Add($"Fit ({_reply.At("sheet").Str("scaleText")})");
        foreach (var s in scales) scale.Items.Add(s.Str("title"));
        double? current = d["scale"] is JsonValue sv && sv.TryGetValue(out double cs) ? cs : null;
        scale.SelectedIndex = current == null ? 0 : Math.Max(0, scales.FindIndex(s => Math.Abs(s.Num("value") - current.Value) < 1e-9) + 1);
        scale.SelectionChanged += (_, _) =>
        {
            int i = scale.SelectedIndex;
            if (i < 0) return;
            Set("scale", i == 0 ? null : JsonValue.Create(scales[i - 1].Num("value")));
        };
        stack.Children.Add(scale);

        stack.Children.Add(Ui.Headline("Views"));
        var selectedViews = (d["views"] as JsonArray)?.Select(x => x?.ToString()).ToHashSet() ?? new();
        foreach (var v in _reply.Arr("views"))
        {
            string id = v.Str("id");
            var box = new CheckBox { Content = v.Str("title"), IsChecked = selectedViews.Contains(id) };
            box.Click += (_, _) =>
            {
                if (box.IsChecked == true) selectedViews.Add(id); else selectedViews.Remove(id);
                var order = _reply.Arr("views").Select(x => x.Str("id")).Where(x => selectedViews.Contains(x));
                Set("views", new JsonArray(order.Select(x => (JsonNode?)JsonValue.Create(x)).ToArray()));
            };
            stack.Children.Add(box);
        }
        stack.Children.Add(Ui.Headline("Lines"));
        var hidden = new ToggleSwitch { OnContent = "Hidden lines", OffContent = "Hidden lines", IsOn = d["showHidden"]?.GetValue<bool>() ?? true };
        hidden.Toggled += (_, _) => Set("showHidden", hidden.IsOn);
        var dims = new ToggleSwitch { OnContent = "Overall dimensions", OffContent = "Overall dimensions", IsOn = d["showDimensions"]?.GetValue<bool>() ?? true };
        dims.Toggled += (_, _) => Set("showDimensions", dims.IsOn);
        stack.Children.Add(hidden);
        stack.Children.Add(dims);
        _properties.Child = new ScrollViewer { Content = stack, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
    }
}
