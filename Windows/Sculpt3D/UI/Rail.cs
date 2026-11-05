// The left tool rail (per mode: modeling, sketching, measuring, visualize),
// the tool flyout of a category, the sketch constraint column and the
// right-hand view controls (Rail.swift).
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
using Sculpt3D.Core;

namespace Sculpt3D.UI;

public sealed record RailItem(string Id, string Title, string Symbol, Action Action,
                              string? Subtitle = null, string? Keys = null, bool Active = false,
                              bool Enabled = true, bool Dot = false);

public static class Rail
{
    /// <summary>Icons share one rounded column; labels float beside them as pills
    /// (on the left of the icons when `trailing`).</summary>
    public static FrameworkElement Group(IReadOnlyList<RailItem> items, bool showLabels = true, bool trailing = false)
    {
        var icons = new StackPanel { Spacing = 4 };
        var labels = new StackPanel { Spacing = 4 };
        foreach (var item in items)
        {
            var iconButton = Ui.Plain(Ui.RailIcon(item.Symbol, item.Active, item.Enabled), item.Action, ToolTip(item), item.Enabled);
            iconButton.Width = 40;
            iconButton.Height = Ui.RailRow;
            icons.Children.Add(iconButton);

            var pill = Ui.LabelPill(item.Title, item.Subtitle, item.Keys, item.Enabled, item.Dot);
            var labelButton = Ui.Plain(pill, item.Action, null, item.Enabled);
            labelButton.Height = Ui.RailRow;
            labelButton.HorizontalAlignment = trailing ? HorizontalAlignment.Right : HorizontalAlignment.Left;
            labelButton.HorizontalContentAlignment = trailing ? HorizontalAlignment.Right : HorizontalAlignment.Left;
            labelButton.VerticalContentAlignment = VerticalAlignment.Center;
            labels.Children.Add(labelButton);
        }
        var iconCard = new Border
        {
            Child = icons,
            Padding = new Thickness(3, 0, 3, 0),
            CornerRadius = new CornerRadius(12),
            Background = Ui.PanelBackground,
            BorderBrush = Ui.Stroke,
            BorderThickness = new Thickness(1),
            VerticalAlignment = VerticalAlignment.Top,
        };
        var grid = new Grid { ColumnSpacing = 6 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        if (!showLabels)
        {
            grid.Children.Add(iconCard);
            return grid;
        }
        // The label column has no card around it; it starts level with the icons.
        labels.Margin = new Thickness(0, 1, 0, 0);
        if (trailing)
        {
            grid.Children.Add(labels);
            Grid.SetColumn(iconCard, 1);
            grid.Children.Add(iconCard);
        }
        else
        {
            grid.Children.Add(iconCard);
            Grid.SetColumn(labels, 1);
            grid.Children.Add(labels);
        }
        return grid;
    }

    private static string ToolTip(RailItem item)
    {
        var tip = item.Title;
        if (!string.IsNullOrEmpty(item.Keys)) tip += $" ({Ui.KeyText(item.Keys)})";
        return tip;
    }
}

/// <summary>Left rail. Its contents follow the mode.</summary>
public sealed class ToolRail : StatePanel
{
    public ToolRail(DocumentView view) : base(view) { }

    /// <summary>The middle group (the category icons), for lining up the flyout.</summary>
    public FrameworkElement? MiddleGroup { get; private set; }

    protected override string Signature()
    {
        var sketch = S.At("sketch");
        // Not ShowProject: rebuilding would pull the Project popover's anchor away.
        return string.Join("|", S.Str("mode"), S.Str("modeTitle"), V.OpenMenu ?? "", V.ShowItems, V.ShowSearch,
            S.At("view").At("section").Bool("enabled"), S.At("view").Bool("measuring"),
            sketch == null ? "" : sketch.At("tools").Sig() + sketch.At("plane").Str("name") + sketch.Count("selection") + sketch.Bool("constructionMode"),
            S.At("tools").Sig());
    }

    protected override UIElement Build()
    {
        string mode = S.Str("mode");
        var grid = new Grid { RowSpacing = 8 };
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var projectItem = new RailItem("project", "Project", "sculpt3d.project", () => V.ToggleProjectPanel(), Subtitle: S.Str("modeTitle"));
        var top = Rail.Group(new[]
        {
            projectItem,
            new RailItem("items", "Items", "square.3.layers.3d", () => V.SetShowItems(!V.ShowItems), Active: V.ShowItems),
        });
        V.AttachProjectFlyout(top);
        grid.Children.Add(top);

        FrameworkElement middle = mode switch
        {
            "visualize" => Rail.Group(new[]
            {
                Search(),
                new RailItem("exitViz", "Exit Visualization", "xmark", () => V.Run("visualize.exit")),
            }),
            "sketching" => SketchRail(),
            "measuring" => Rail.Group(new[]
            {
                new RailItem("exitMeasure", "Exit Measure", "xmark", () => V.Run("view.setMeasuring", new JsonObject { ["on"] = false })),
                new RailItem("measureMode", "Measure mode", "ruler", () => { }, Subtitle: "Object Measurements", Active: true),
            }),
            _ => ModelingGroup(),
        };
        MiddleGroup = middle;
        middle.VerticalAlignment = VerticalAlignment.Center;
        var scroll = new ScrollViewer
        {
            Content = middle,
            VerticalScrollBarVisibility = ScrollBarVisibility.Hidden,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            VerticalAlignment = VerticalAlignment.Center,
        };
        Grid.SetRow(scroll, 1);
        grid.Children.Add(scroll);

        if (mode != "visualize")
        {
            var section = S.At("view").At("section").Bool("enabled");
            var bottom = Rail.Group(new[]
            {
                new RailItem("section", "Section View", "square.split.bottomrightquarter", () => V.Run("view.toggleSection"),
                             Subtitle: section ? "On" : "Off", Keys: "⌥⌘S", Active: section),
                new RailItem("measure", "Measure", "ruler", () => V.Run("view.toggleMeasuring"), Keys: "⌥⌘M",
                             Active: S.At("view").Bool("measuring")),
            });
            Grid.SetRow(bottom, 2);
            grid.Children.Add(bottom);
        }
        return grid;
    }

    private RailItem Search() =>
        new("search", "Search", "magnifyingglass", () => V.SetShowSearch(true), Keys: "⌘F", Active: V.ShowSearch);

    private FrameworkElement ModelingGroup()
    {
        var items = new List<RailItem> { Search() };
        foreach (var c in S.Arr("tools").Where(c => c.Bool("onRail")))
        {
            string id = c.Str("id");
            items.Add(new RailItem(id, c.Str("title"), c.Str("symbol"), () =>
            {
                if (id == "visualize")
                {
                    V.SetOpenMenu(null);
                    V.Run("visualize.enter");
                }
                else
                {
                    V.SetOpenMenu(V.OpenMenu == id ? null : id);
                }
            }, Active: V.OpenMenu == id, Dot: c.Bool("dot")));
        }
        return Rail.Group(items, showLabels: V.OpenMenu == null);
    }

    private FrameworkElement SketchRail()
    {
        var s = S.At("sketch");
        bool hasSelection = s.Count("selection") > 0;
        var stack = new StackPanel { Spacing = 10 };
        stack.Children.Add(Rail.Group(new[]
        {
            Search(),
            new RailItem("exit", "Exit Sketching", "xmark", () => V.Run("sketch.cancel")),
            new RailItem("plane", s.At("plane").Str("name", "No active plane"), "square.grid.3x3", () => V.Run("sketch.lookAt")),
        }));
        var tools = new List<RailItem>();
        foreach (var t in s.Arr("tools"))
        {
            string id = t.Str("id");
            if (id == "select" || id == "trim") continue;
            tools.Add(ToolItem(t));
        }
        tools.Add(new RailItem("project", "Project", "rectangle.portrait.on.rectangle.portrait.angled", () => V.Run("sketch.project")));
        tools.Add(new RailItem("offset", "Offset Edge", "square.on.square", () => V.Run("sketch.offset"), Keys: "O", Enabled: hasSelection));
        tools.Add(new RailItem("fillet", "Fillet", "arrow.turn.up.right", () => V.Run("sketch.fillet"), Enabled: hasSelection));
        stack.Children.Add(Rail.Group(tools));
        var select = s.Arr("tools").FirstOrDefault(t => t.Str("id") == "select");
        var trim = s.Arr("tools").FirstOrDefault(t => t.Str("id") == "trim");
        var editing = new List<RailItem>();
        if (select != null) editing.Add(ToolItem(select));
        if (trim != null) editing.Add(ToolItem(trim));
        editing.Add(new RailItem("delete", "Delete", "trash", () => V.Run("sketch.deleteSelection"), Enabled: hasSelection));
        stack.Children.Add(Rail.Group(editing));
        bool construction = s.Bool("constructionMode");
        stack.Children.Add(Rail.Group(new[]
        {
            new RailItem("construction", "Construction", "square.dashed", () => V.Run("sketch.toggleConstructionMode"),
                         Subtitle: construction ? "On" : "Off", Active: construction),
        }));
        return stack;
    }

    private RailItem ToolItem(JsonNode? t)
    {
        string id = t.Str("id");
        return new RailItem(id, t.Str("title"), t.Str("symbol"), () => V.Run("sketch.setTool", new JsonObject { ["tool"] = id }),
                            Subtitle: t.StrOrNull("subtitle"), Keys: t.StrOrNull("key"), Active: t.Bool("active"));
    }
}

/// <summary>The tools of the open rail category, as a second icon column with label pills.</summary>
public sealed class ToolFlyout : StatePanel
{
    public ToolFlyout(DocumentView view) : base(view) { }

    protected override string Signature()
    {
        if (V.OpenMenu == null || S.Str("mode") != "modeling") return "";
        return V.OpenMenu + S.Arr("tools").FirstOrDefault(c => c.Str("id") == V.OpenMenu).Sig();
    }

    protected override UIElement? Build()
    {
        if (V.OpenMenu == null || S.Str("mode") != "modeling") return null;
        var category = S.Arr("tools").FirstOrDefault(c => c.Str("id") == V.OpenMenu);
        if (category == null) return null;
        var items = new List<RailItem>();
        foreach (var t in category.Arr("tools"))
        {
            string id = t.Str("id");
            string hint = t.Str("hint");
            bool enabled = t.Bool("enabled");
            items.Add(new RailItem(id, t.Str("title"), t.Str("symbol"), () =>
            {
                V.SetOpenMenu(null);
                V.RunTool(id);
            }, Keys: t.StrOrNull("shortcut"), Enabled: enabled, Subtitle: enabled ? null : (string.IsNullOrEmpty(hint) ? null : hint)));
        }
        return new ScrollViewer
        {
            Content = Rail.Group(items),
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
    }
}

/// <summary>Constraints down the right side while sketching.</summary>
public sealed class ConstraintColumn : StatePanel
{
    public ConstraintColumn(DocumentView view) : base(view) { }

    protected override string Signature() => S.Str("mode") == "sketching" ? S.At("sketch").At("constraintColumn").Sig() : "";

    protected override UIElement? Build()
    {
        if (S.Str("mode") != "sketching") return null;
        var items = S.At("sketch").Arr("constraintColumn").Select(c =>
        {
            string id = c.Str("id");
            return new RailItem(id, c.Str("title"), c.Str("symbol"), () => V.Run("sketch.constraintColumn", new JsonObject { ["id"] = id }),
                                Keys: c.StrOrNull("keys"), Enabled: c.Bool("enabled"));
        }).ToList();
        return new ScrollViewer
        {
            Content = Rail.Group(items, trailing: true),
            VerticalScrollBarVisibility = ScrollBarVisibility.Hidden,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
        };
    }
}

/// <summary>View cube, snapping, grid and display controls on the right.</summary>
public sealed class ViewControlsColumn : StatePanel
{
    private ViewCube? _cube;

    public ViewControlsColumn(DocumentView view) : base(view) { }

    public void UpdateCube() => _cube?.Update();

    protected override string Signature()
    {
        var view = S.At("view");
        // Snap settings refresh in place (Refresh) so the magnet's popover stays open while toggling.
        return string.Join("|", view.Str("gridLabel"), S.At("settings").At("environment").Bool("showGrid"),
            view.Str("displayModeSymbol"), view.Str("displayModeTitle"), V.ShowHistory, S.Str("mode"), view.Str("fixedGrid"),
            view.At("gridOptions").Sig());
    }

    private Button? _magnet;
    private bool _magnetActive;

    protected override void Refresh()
    {
        bool snapGrid = S.At("settings").At("snap").Bool("grid");
        if (_magnet != null && snapGrid != _magnetActive)
        {
            _magnetActive = snapGrid;
            _magnet.Content = Ui.RailIcon("sculpt3d.magnet", snapGrid, true);
        }
    }

    protected override UIElement Build()
    {
        var view = S.At("view");
        _cube = new ViewCube(V);
        _cube.Update();

        bool snapGrid = S.At("settings").At("snap").Bool("grid");
        var magnet = Ui.Plain(Ui.RailIcon("sculpt3d.magnet", snapGrid, true), () => { }, "Snapping");
        _magnet = magnet;
        _magnetActive = snapGrid;
        magnet.Width = 40;
        magnet.Height = 40;
        var snapFlyout = new Flyout { Placement = FlyoutPlacementMode.LeftEdgeAlignedTop };
        snapFlyout.Opening += (_, _) => snapFlyout.Content = Panels.SnapSettings(V);
        magnet.Flyout = snapFlyout;

        var gridLabel = new TextBlock
        {
            Text = view.Str("gridLabel").Replace("\\n", "\n"),
            FontSize = 10,
            FontWeight = FontWeights.Bold,
            TextAlignment = TextAlignment.Center,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        };
        var gridButton = Ui.Plain(new Border { Width = 34, Height = 34, Child = gridLabel }, () => { }, "Grid size");
        gridButton.Width = 40;
        gridButton.Height = 40;
        var gridFlyout = new Flyout { Placement = FlyoutPlacementMode.LeftEdgeAlignedTop };
        gridFlyout.Opening += (_, _) => gridFlyout.Content = Panels.GridSettings(V);
        gridButton.Flyout = gridFlyout;

        bool showGrid = S.At("settings").At("environment").Bool("showGrid");
        var gridToggle = Ui.Plain(Ui.RailIcon("move.3d", !showGrid, true),
            () => V.Run("settings.setEnvironment", new JsonObject { ["showGrid"] = !showGrid }),
            showGrid ? "Hide grid and axes" : "Show grid and axes");
        gridToggle.Width = 40;
        gridToggle.Height = 40;

        var tiles = new Border
        {
            Child = Ui.VStack(4, magnet, gridButton, gridToggle),
            Padding = new Thickness(3),
            CornerRadius = new CornerRadius(12),
            Background = Ui.PanelBackground,
            BorderBrush = Ui.Stroke,
            BorderThickness = new Thickness(1),
            VerticalAlignment = VerticalAlignment.Top,
        };
        var top = Ui.HStack(8, _cube, tiles);
        top.HorizontalAlignment = HorizontalAlignment.Right;

        var controls = Rail.Group(new[]
        {
            new RailItem("display", "Display Modes", view.Str("displayModeSymbol", "cube.fill"), () => V.Run("view.cycleDisplayMode"),
                         Subtitle: view.Str("displayModeTitle")),
            new RailItem("shot", "Screenshot", "camera", () => _ = V.SaveScreenshotAsync(2)),
            new RailItem("export", "Export STEP", "square.and.arrow.up", () => _ = V.ExportKindAsync("step")),
            new RailItem("history", "History", "clock.arrow.circlepath", () => V.SetShowHistory(!V.ShowHistory), Keys: "⌥⌘H", Active: V.ShowHistory),
        }, trailing: true);
        controls.HorizontalAlignment = HorizontalAlignment.Right;
        var stack = Ui.VStack(10, top, controls);
        stack.HorizontalAlignment = HorizontalAlignment.Right;
        return stack;
    }
}
