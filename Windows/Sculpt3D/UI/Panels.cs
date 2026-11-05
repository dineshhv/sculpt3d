// Sidebars and popovers: Project, Snap and Grid settings, Items (bodies,
// sketches and planes), History and Properties (Panels.swift, MenuPanels.swift).
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json.Nodes;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Sculpt3D.Core;

namespace Sculpt3D.UI;

public static class Panels
{
    /// <summary>Project popover: units, snap to grid and a summary.</summary>
    public static FrameworkElement ProjectPanel(DocumentView v)
    {
        var s = v.Doc.State;
        var settings = s.At("settings");
        var units = settings.Arr("unitOptions").ToList();
        int selected = units.FindIndex(u => u.Str("id") == settings.Str("units"));
        var picker = Ui.Segmented(units.Select(u => u.Str("symbol")).ToList(), selected,
            i => v.Run("settings.setUnits", new JsonObject { ["units"] = units[i].Str("id") }));
        var snap = new ToggleSwitch { Header = "Snap to grid", IsOn = settings.At("snap").Bool("grid") };
        snap.Toggled += (_, _) => v.Run("settings.setSnap", new JsonObject { ["grid"] = snap.IsOn });
        var panel = Ui.VStack(12, Ui.Headline("Project"), picker, snap, Ui.Caption(s.At("project").Str("summary")));
        panel.Width = 260;
        return panel;
    }

    private static ToggleSwitch SnapToggle(DocumentView v, string title, string key)
    {
        var t = new ToggleSwitch { Header = null, OnContent = title, OffContent = title, IsOn = v.Doc.State.At("settings").At("snap").Bool(key) };
        t.Toggled += (_, _) => v.Run("settings.setSnap", new JsonObject { [key] = t.IsOn });
        return t;
    }

    /// <summary>The magnet's popover (SnapSettingsPanel).</summary>
    public static FrameworkElement SnapSettings(DocumentView v)
    {
        var panel = Ui.VStack(4,
            Ui.Headline("Snap to"),
            SnapToggle(v, "Grid", "grid"),
            SnapToggle(v, "Sketch Guide Lines", "guideLines"),
            SnapToggle(v, "Sketch Guide Points", "guidePoints"),
            SnapToggle(v, "3D Guide Points", "points3D"),
            SnapToggle(v, "Distant Edges", "distantEdges"),
            Ui.Divider(),
            Ui.Headline("Show"),
            SnapToggle(v, "Guide Points", "showGuidePoints"),
            SnapToggle(v, "Snapping Hints", "showHints"));
        panel.Width = 280;
        return panel;
    }

    /// <summary>The grid label's popover (GridSettingsPanel): Automatic or a fixed spacing.</summary>
    public static FrameworkElement GridSettings(DocumentView v)
    {
        var view = v.Doc.State.At("view");
        var fixedGrid = view.NumOrNull("fixedGrid");
        var options = view.Arr("gridOptions").ToList();
        var list = new RadioButtons { Header = "Grid" };
        list.Items.Add("Automatic");
        int selected = 0;
        for (int i = 0; i < options.Count; i++)
        {
            list.Items.Add(options[i].Str("title"));
            if (fixedGrid.HasValue && Math.Abs(options[i].Num("value") - fixedGrid.Value) < 1e-9) selected = i + 1;
        }
        list.SelectedIndex = selected;
        list.SelectionChanged += (_, _) =>
        {
            int i = list.SelectedIndex;
            if (i < 0) return;
            var args = new JsonObject { ["value"] = i == 0 ? null : JsonValue.Create(options[i - 1].Num("value")) };
            v.Run("view.setFixedGrid", args);
        };
        var panel = Ui.VStack(6, list);
        panel.Width = 180;
        return panel;
    }

    /// <summary>A colour dot for a body's appearance.</summary>
    public static Ellipse Dot(JsonNode? rgb, double size = 10) =>
        new() { Width = size, Height = size, Fill = new SolidColorBrush(rgb.Rgb()), Stroke = Ui.Stroke, StrokeThickness = 0.5, VerticalAlignment = VerticalAlignment.Center };

    public static Grid PropertyRow(string label, string value)
    {
        var l = Ui.Text(label, 13, null, Ui.Secondary);
        var r = Ui.Mono(value, 13);
        r.IsTextSelectionEnabled = true;
        r.TextAlignment = TextAlignment.Right;
        r.TextWrapping = TextWrapping.Wrap;
        return Ui.Row(l, r);
    }
}

/// <summary>Bodies, sketches and planes: select, show/hide, rename, and the item actions.</summary>
public sealed class ItemsPanel : StatePanel
{
    private string? _renaming;

    public ItemsPanel(DocumentView view) : base(view) { }

    protected override string Signature() =>
        V.ShowItems && S.Str("mode") != "sketching" && V.OpenMenu == null
            ? S.At("bodies").Sig() + S.At("items").Sig() + _renaming
            : "";

    protected override UIElement? Build()
    {
        if (!V.ShowItems || S.Str("mode") == "sketching" || V.OpenMenu != null) return null;
        var stack = new StackPanel { Spacing = 2 };
        stack.Children.Add(Header("Bodies"));
        var bodies = S.Arr("bodies").ToList();
        if (bodies.Count == 0) stack.Children.Add(Ui.Caption("No bodies yet"));
        foreach (var b in bodies) stack.Children.Add(BodyRow(b));
        stack.Children.Add(Header("Sketches & Planes"));
        var items = S.Arr("items").ToList();
        if (items.Count == 0) stack.Children.Add(Ui.Caption("Sketches kept with Done and construction planes appear here"));
        foreach (var i in items) stack.Children.Add(ItemRow(i));
        var card = Ui.Card(new ScrollViewer { Content = stack, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, 10);
        card.Width = 260;
        card.VerticalAlignment = VerticalAlignment.Top;
        card.MaxHeight = 640;
        return card;
    }

    private static TextBlock Header(string title)
    {
        var t = Ui.Text(title, 12, FontWeights.SemiBold, Ui.Secondary);
        t.Margin = new Thickness(4, 8, 0, 4);
        return t;
    }

    private UIElement RenameBox(string current, Action<string> commit)
    {
        var box = new TextBox { Text = current, MinWidth = 120 };
        bool done = false;
        void Finish(bool apply)
        {
            if (done) return;
            done = true;
            _renaming = null;
            if (apply && box.Text.Trim().Length > 0 && box.Text != current) commit(box.Text.Trim());
            else V.Refresh();
        }
        box.KeyDown += (_, e) =>
        {
            if (e.Key == Windows.System.VirtualKey.Enter) { e.Handled = true; Finish(true); }
            else if (e.Key == Windows.System.VirtualKey.Escape) { e.Handled = true; Finish(false); }
        };
        box.LostFocus += (_, _) => Finish(true);
        box.Loaded += (_, _) => { box.Focus(FocusState.Programmatic); box.SelectAll(); };
        return box;
    }

    private UIElement BodyRow(JsonNode? b)
    {
        int id = b.Int("id");
        bool selected = b.Bool("selected"), hidden = b.Bool("hidden");
        string name = b.Str("name");
        UIElement title;
        if (_renaming == "body" + id)
        {
            title = RenameBox(name, n => V.Run("body.rename", new JsonObject { ["id"] = id, ["name"] = n }));
        }
        else
        {
            var text = Ui.Text(name, 13, null, selected ? Ui.AccentText : hidden ? Ui.Secondary : Ui.Primary);
            title = Ui.Plain(text, () => V.Run("selection.toggleBody", new JsonObject { ["id"] = id }));
        }
        var eye = Ui.Plain(Icons.Create(hidden ? "eye.slash" : "eye", 13, Ui.Secondary),
            () => V.Run("body.toggleHidden", new JsonObject { ["id"] = id }), hidden ? "Show" : "Hide");
        eye.Width = 28;
        var row = new Grid { ColumnSpacing = 8, Padding = new Thickness(4, 3, 4, 3) };
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        row.Children.Add(Panels.Dot(b.At("appearance").At("color")));
        Grid.SetColumn((FrameworkElement)title, 1);
        row.Children.Add(title);
        Grid.SetColumn(eye, 2);
        row.Children.Add(eye);
        var stack = Ui.VStack(1, row);
        if (selected)
        {
            foreach (var line in b.At("properties").Arr("captionLines"))
            {
                var c = Ui.Caption(line.AsStr());
                c.Margin = new Thickness(22, 0, 0, 0);
                stack.Children.Add(c);
            }
        }
        var menu = new MenuFlyout();
        menu.Items.Add(MenuItem("Rename", () => { _renaming = "body" + id; V.Refresh(); }));
        menu.Items.Add(MenuItem(hidden ? "Show" : "Hide", () => V.Run("body.toggleHidden", new JsonObject { ["id"] = id })));
        menu.Items.Add(MenuItem("Properties", () => V.SetShowProperties(true)));
        stack.ContextFlyout = menu;
        stack.Background = new SolidColorBrush(Colors.Transparent);
        return stack;
    }

    private UIElement ItemRow(JsonNode? item)
    {
        string id = item.Str("id"), kind = item.Str("kind"), name = item.Str("name");
        bool selected = item.Bool("selected"), hidden = item.Bool("hidden");
        UIElement title;
        if (_renaming == id)
        {
            title = RenameBox(name, n => V.Run("item.rename", new JsonObject { ["id"] = id, ["name"] = n }));
        }
        else
        {
            var row = Ui.HStack(6, Ui.Text(name, 13, null, selected ? Ui.AccentText : hidden ? Ui.Secondary : Ui.Primary));
            var order = item.IntOrNull("order");
            if (order.HasValue)
            {
                row.Children.Add(new Border
                {
                    Background = Ui.Subtle,
                    CornerRadius = new CornerRadius(8),
                    Padding = new Thickness(5, 0, 5, 0),
                    Child = Ui.Text(order.Value.ToString(), 10, FontWeights.SemiBold),
                });
            }
            title = Ui.Plain(row, () => V.Run("item.toggleSelection", new JsonObject { ["id"] = id }));
        }
        var eye = Ui.Plain(Icons.Create(hidden ? "eye.slash" : "eye", 13, Ui.Secondary),
            () => V.Run("item.toggleHidden", new JsonObject { ["id"] = id }), hidden ? "Show" : "Hide");
        eye.Width = 28;
        var grid = new Grid { ColumnSpacing = 8, Padding = new Thickness(4, 3, 4, 3), Background = new SolidColorBrush(Colors.Transparent) };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(16) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.Children.Add(Icons.Create(item.Str("symbol"), 13, Ui.Secondary));
        Grid.SetColumn((FrameworkElement)title, 1);
        grid.Children.Add(title);
        Grid.SetColumn(eye, 2);
        grid.Children.Add(eye);

        var menu = new MenuFlyout();
        menu.Items.Add(MenuItem(item.Str("editTitle", "Edit"), () => V.Run("item.edit", new JsonObject { ["id"] = id })));
        if (kind == "sketch")
        {
            menu.Items.Add(MenuItem("Extrude", () => V.Run("item.extrude", new JsonObject { ["id"] = id })));
            menu.Items.Add(MenuItem("Revolve", () => V.Run("item.revolve", new JsonObject { ["id"] = id })));
        }
        else if (kind == "plane")
        {
            menu.Items.Add(MenuItem("Sketch on Plane", () => V.Run("sketch.startOnItem", new JsonObject { ["id"] = id })));
        }
        menu.Items.Add(MenuItem("Rename", () => { _renaming = id; V.Refresh(); }));
        menu.Items.Add(MenuItem(hidden ? "Show" : "Hide", () => V.Run("item.toggleHidden", new JsonObject { ["id"] = id })));
        menu.Items.Add(new MenuFlyoutSeparator());
        menu.Items.Add(MenuItem("Delete", () => V.Run("item.delete", new JsonObject { ["id"] = id })));
        grid.ContextFlyout = menu;
        return grid;
    }

    public static MenuFlyoutItem MenuItem(string title, Action run, string? symbol = null, bool enabled = true)
    {
        var item = new MenuFlyoutItem { Text = title, IsEnabled = enabled };
        if (symbol != null) item.Icon = new FontIcon { Glyph = "" };
        item.Click += (_, _) => run();
        return item;
    }
}

/// <summary>The parametric history: undo/redo and one row per feature.</summary>
public sealed class HistoryPanel : StatePanel
{
    public HistoryPanel(DocumentView view) : base(view) { }

    public bool Shown => V.ShowHistory && RightPanelFree(V);

    /// <summary>History and Properties give way to Visualize, Measure and (on narrow windows) the sketch's constraint column.</summary>
    public static bool RightPanelFree(DocumentView v)
    {
        string mode = v.Doc.State.Str("mode");
        if (mode == "visualize" || mode == "measuring") return false;
        if (mode == "sketching" && v.ActualWidth < 1200) return false;
        return true;
    }

    protected override string Signature() =>
        Shown ? S.At("features").Sig() + S.At("history").Sig() : "";

    protected override UIElement? Build()
    {
        if (!Shown) return null;
        var h = S.At("history");
        var header = Ui.Row(Ui.Headline("History"), Ui.HStack(2,
            Ui.IconButton("arrow.uturn.backward", () => V.Run("edit.undo"), "Undo (Ctrl+Z)", h.Bool("canUndo")),
            Ui.IconButton("arrow.uturn.forward", () => V.Run("edit.redo"), "Redo (Ctrl+Shift+Z)", h.Bool("canRedo"))));
        var features = S.Arr("features").ToList();
        UIElement body;
        if (features.Count == 0)
        {
            body = Ui.VStack(4, Ui.Text(h.Str("emptyTitle", "Workspace is empty"), 13, FontWeights.SemiBold),
                Ui.Caption(h.Str("emptySubtitle", "Your modeling steps will show up here")));
            ((FrameworkElement)body).Margin = new Thickness(0, 24, 0, 24);
            ((FrameworkElement)body).HorizontalAlignment = HorizontalAlignment.Center;
        }
        else
        {
            var list = new StackPanel { Spacing = 1 };
            foreach (var f in features) list.Children.Add(Row(f));
            body = new ScrollViewer { Content = list, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        }
        var grid = new Grid { RowSpacing = 6 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.Children.Add(header);
        Grid.SetRow((FrameworkElement)body, 1);
        grid.Children.Add(body);
        var card = Ui.Card(grid, 12);
        card.Width = 250;
        return card;
    }

    private UIElement Row(JsonNode? f)
    {
        string id = f.Str("id");
        string? error = f.StrOrNull("error");
        bool suppressed = f.Bool("suppressed"), editing = f.Bool("editing"), editable = f.Bool("editable");
        var number = Ui.Text(f.Int("number").ToString(), 11, null, Ui.Tertiary);
        number.Width = 20;
        var icon = Icons.Create(error != null ? "exclamationmark.triangle.fill" : f.Str("symbol"), 14,
            error != null ? Ui.Caution : editing ? Ui.AccentText : Ui.Secondary);
        icon.Width = 20;
        var title = Ui.Text(f.Str("title"), 13, null, suppressed ? Ui.Secondary : Ui.Primary);
        if (suppressed) title.TextDecorations = Windows.UI.Text.TextDecorations.Strikethrough;
        var texts = Ui.VStack(1, title);
        if (error != null)
        {
            var e = Ui.Text(error, 11, null, Ui.Caution, wrap: true);
            e.MaxLines = 2;
            texts.Children.Add(e);
        }
        var row = Ui.HStack(8, number, icon, texts);
        row.Padding = new Thickness(4, 4, 4, 4);
        var button = Ui.Plain(row, () => { if (editable) V.Run("feature.open", new JsonObject { ["id"] = id }); },
            editable ? "Edit " + f.Str("title") : f.Str("title"));
        button.HorizontalContentAlignment = HorizontalAlignment.Left;
        if (editing) button.Background = Ui.Subtle;
        var menu = new MenuFlyout();
        if (f.Bool("hasParameters")) menu.Items.Add(ItemsPanel.MenuItem("Edit", () => V.Run("feature.edit", new JsonObject { ["id"] = id })));
        if (f.Bool("hasSketch")) menu.Items.Add(ItemsPanel.MenuItem("Edit Sketch", () => V.Run("feature.editSketch", new JsonObject { ["id"] = id })));
        menu.Items.Add(ItemsPanel.MenuItem(suppressed ? "Unsuppress" : "Suppress", () => V.Run("feature.toggleSuppressed", new JsonObject { ["id"] = id })));
        menu.Items.Add(new MenuFlyoutSeparator());
        menu.Items.Add(ItemsPanel.MenuItem("Delete", () => V.Run("feature.delete", new JsonObject { ["id"] = id })));
        button.ContextFlyout = menu;
        return button;
    }
}

/// <summary>View ▸ Show Properties Sidebar: the selection's size, colour, opacity and visibility.</summary>
public sealed class PropertiesPanel : StatePanel
{
    private readonly Dictionary<int, (Border Swatch, ColorPicker Picker, SliderBinding Opacity, ToggleSwitch Visible)> _rows = new();

    public PropertiesPanel(DocumentView view) : base(view) { }

    public bool Shown => V.ShowProperties && HistoryPanel.RightPanelFree(V);

    protected override string Signature()
    {
        if (!Shown) return "";
        // Colour, opacity and visibility update in place (Refresh), so sliders keep dragging.
        var bodies = string.Concat(S.Arr("bodies").Where(b => b.Bool("touched") || b.Bool("selected"))
            .Select(b => b.Str("id") + b.Str("name") + b.At("properties").Sig()));
        return S.At("selection").At("touchedBodies").Sig() + bodies + S.At("project").Sig() + S.At("document").Str("name") + S.At("settings").Str("units");
    }

    protected override void Refresh()
    {
        foreach (var b in S.Arr("bodies"))
        {
            if (!_rows.TryGetValue(b.Int("id"), out var row)) continue;
            var color = b.At("appearance").At("color").Rgb();
            row.Swatch.Background = new SolidColorBrush(color);
            row.Opacity.Set(b.At("appearance").Num("opacity", 1));
            if (row.Visible.IsOn == b.Bool("hidden")) row.Visible.IsOn = !b.Bool("hidden");
        }
    }

    protected override UIElement? Build()
    {
        _rows.Clear();
        if (!Shown) return null;
        var stack = new StackPanel { Spacing = 10 };
        stack.Children.Add(Ui.Headline("Properties"));
        var touched = S.At("selection").Arr("touchedBodies").Select(t => t.AsInt()).ToHashSet();
        if (touched.Count == 0)
        {
            var project = S.At("project");
            stack.Children.Add(Panels.PropertyRow("Project", S.At("document").Str("name")));
            stack.Children.Add(Panels.PropertyRow("Bodies", $"{project.Int("bodyCount")} ({project.Int("hiddenBodyCount")} hidden)"));
            stack.Children.Add(Panels.PropertyRow("Features", project.Int("featureCount").ToString()));
            var units = S.At("settings").Arr("unitOptions").ToList();
            var combo = new ComboBox { Header = "Units", HorizontalAlignment = HorizontalAlignment.Stretch };
            foreach (var u in units) combo.Items.Add(u.Str("title"));
            combo.SelectedIndex = units.FindIndex(u => u.Str("id") == S.At("settings").Str("units"));
            combo.SelectionChanged += (_, _) =>
            {
                if (combo.SelectedIndex >= 0) V.Run("settings.setUnits", new JsonObject { ["units"] = units[combo.SelectedIndex].Str("id") });
            };
            stack.Children.Add(combo);
            stack.Children.Add(Ui.Caption("Select a body, face or edge to see its properties."));
        }
        else
        {
            foreach (var b in S.Arr("bodies").Where(b => touched.Contains(b.Int("id")))) stack.Children.Add(BodySection(b));
        }
        var card = Ui.Card(new ScrollViewer { Content = stack, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, 14);
        card.Width = 250;
        card.MaxHeight = 420;
        card.VerticalAlignment = VerticalAlignment.Top;
        return card;
    }

    private UIElement BodySection(JsonNode? b)
    {
        int id = b.Int("id");
        var p = b.At("properties");
        var stack = new StackPanel { Spacing = 8 };
        stack.Children.Add(Ui.Text(b.Str("name"), 13, FontWeights.SemiBold));
        if (p != null)
        {
            stack.Children.Add(Panels.PropertyRow("Size", p.Str("sizeText")));
            stack.Children.Add(Panels.PropertyRow("Volume", p.Str("volumeText")));
            stack.Children.Add(Panels.PropertyRow("Surface area", p.Str("areaText")));
            stack.Children.Add(Panels.PropertyRow("Faces / edges", p.Str("facesEdgesText")));
        }
        var look = b.At("appearance");
        var color = look.At("color").Rgb();
        var swatch = new Border { Width = 40, Height = 22, CornerRadius = new CornerRadius(4), Background = new SolidColorBrush(color), BorderBrush = Ui.Stroke, BorderThickness = new Thickness(1) };
        var picker = new ColorPicker { Color = color, IsAlphaEnabled = false, IsMoreButtonVisible = false, IsColorPreviewVisible = true };
        var flyout = new Flyout { Content = picker };
        var colorButton = new Button { Content = swatch, Padding = new Thickness(2), Flyout = flyout };
        flyout.Opening += (_, _) => picker.Color = ((SolidColorBrush)swatch.Background).Color;
        flyout.Closed += (_, _) =>
        {
            if (picker.Color != ((SolidColorBrush)swatch.Background).Color)
                V.Run("appearance.set", new JsonObject { ["bodies"] = new JsonArray(id), ["color"] = J.ColorArray(picker.Color) });
        };
        stack.Children.Add(Ui.Row(Ui.Text("Color", 13, null, Ui.Secondary), colorButton));
        var opacity = new SliderBinding(0.05, 1, look.Num("opacity", 1),
            v => V.Run("appearance.set", new JsonObject { ["bodies"] = new JsonArray(id), ["opacity"] = v }));
        opacity.Slider.Width = 140;
        stack.Children.Add(Ui.Row(Ui.Text("Opacity", 13, null, Ui.Secondary), opacity.Slider));
        var visible = new ToggleSwitch { IsOn = !b.Bool("hidden"), OnContent = "Visible", OffContent = "Hidden" };
        visible.Toggled += (_, _) =>
        {
            bool hiddenNow = S.Arr("bodies").FirstOrDefault(x => x.Int("id") == id).Bool("hidden");
            if (visible.IsOn == hiddenNow) V.Run("body.setVisible", new JsonObject { ["id"] = id, ["visible"] = visible.IsOn });
        };
        stack.Children.Add(visible);
        stack.Children.Add(Ui.Divider());
        _rows[id] = (swatch, picker, opacity, visible);
        return stack;
    }
}
