// The bars over the viewport: SelectionBar, OperationPanel (live tool
// parameters), SketchBar with its ConstraintBar, SectionPanel, the point-pick
// banner, pinned measurements and Measure's side panel.
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
using Sculpt3D.Core;

namespace Sculpt3D.UI;

/// <summary>A numeric field that commits what was typed on Enter or when it
/// loses focus, and takes the state's value otherwise.</summary>
public sealed class CommitField
{
    public TextBox Box { get; }
    private string _shown;

    public CommitField(string text, Action<string> commit, double width = 0, string? tooltip = null)
    {
        _shown = text;
        Box = new TextBox { Text = text, TextAlignment = TextAlignment.Right, MinWidth = 56, VerticalAlignment = VerticalAlignment.Center };
        if (width > 0) Box.Width = width;
        if (tooltip != null) ToolTipService.SetToolTip(Box, tooltip);
        void Commit()
        {
            if (Box.Text == _shown) return;
            _shown = Box.Text;
            commit(Box.Text);
        }
        Box.KeyDown += (_, e) =>
        {
            if (e.Key == Windows.System.VirtualKey.Enter)
            {
                e.Handled = true;
                Commit();
            }
            else if (e.Key == Windows.System.VirtualKey.Escape)
            {
                e.Handled = true;
                Box.Text = _shown;
            }
        };
        Box.LostFocus += (_, _) => Commit();
    }

    /// <summary>Shows the state's text unless the user is typing.</summary>
    public void Set(string text)
    {
        if (Box.FocusState != FocusState.Unfocused) return;
        _shown = text;
        if (Box.Text != text) Box.Text = text;
    }
}

public sealed class SelectionBar : StatePanel
{
    public SelectionBar(DocumentView view) : base(view) { }

    private bool Shown => S.At("draft") == null && S.At("sketch") == null && S.Str("mode") != "visualize";

    protected override string Signature() =>
        Shown ? S.At("selection").Str("summary") + S.At("selectionBar").Sig() + S.At("selection").Count("picks") : "";

    protected override UIElement? Build()
    {
        if (!Shown) return null;
        var sel = S.At("selection");
        if (sel.Count("picks") == 0)
        {
            var hint = Ui.Text(sel.Str("hintPointer").Replace("⌥-drag", "Shift-drag").Replace("scroll to orbit, pinch to zoom", "wheel to zoom"),
                13, null, Ui.Secondary, wrap: true);
            hint.TextAlignment = TextAlignment.Center;
            hint.MaxLines = 2;
            var capsule = Ui.Capsule(hint, 14, 8);
            capsule.HorizontalAlignment = HorizontalAlignment.Center;
            return capsule;
        }
        var row = Ui.HStack(6);
        var summary = Ui.Text(sel.Str("summary"), 13, FontWeights.Medium);
        summary.Margin = new Thickness(8, 0, 4, 0);
        row.Children.Add(summary);
        foreach (var b in S.Arr("selectionBar"))
        {
            string id = b.Str("id"), title = b.Str("title"), symbol = b.Str("symbol");
            bool iconOnly = id == "selection.clear" || b.Bool("destructive");
            row.Children.Add(iconOnly
                ? Ui.IconButton(symbol, () => V.Run(id), title)
                : Ui.Button(title, () => V.Run(id), symbol, tooltip: title));
        }
        var card = Ui.Card(row, 6, 14);
        card.HorizontalAlignment = HorizontalAlignment.Center;
        return card;
    }
}

/// <summary>Floating card that edits the parameters of the operation being drafted.</summary>
public sealed class OperationPanel : StatePanel
{
    private readonly Dictionary<int, CommitField> _fields = new();
    private readonly Dictionary<int, StackPanel> _choices = new();
    private StackPanel? _combine, _axis;
    private TextBlock? _error;
    private Button? _apply;

    public OperationPanel(DocumentView view) : base(view) { }

    protected override string Signature()
    {
        var d = S.At("draft");
        if (d == null) return "";
        var parts = new List<string> { d.Str("title"), d.Str("case"), d.Str("replacing"), d.Bool("canEditSketch").ToString(), S.At("units").Str("id") };
        foreach (var p in d.Arr("parameters")) parts.Add(p.Str("id") + p.Str("label") + p.Str("displayUnit"));
        foreach (var c in d.Arr("choices")) parts.Add(c.Str("id") + c.Str("label") + c.At("options").Sig());
        parts.Add(d.At("combineMode").At("options").Sig());
        parts.Add((d.At("revolveAxis") != null).ToString());
        return string.Join("|", parts);
    }

    protected override UIElement? Build()
    {
        _fields.Clear();
        _choices.Clear();
        _combine = _axis = null;
        var d = S.At("draft");
        if (d == null) return null;
        var stack = new StackPanel { Spacing = 10 };

        var header = Ui.HStack(8, Icons.Create(d.Str("symbol"), 16), Ui.Headline(d.Str("title")));
        UIElement? editSketch = d.Bool("canEditSketch") ? Ui.Button("Edit Sketch", () => V.Run("draft.editSketch"), "pencil.and.outline") : null;
        stack.Children.Add(Ui.Row(header, editSketch));

        foreach (var p in d.Arr("parameters")) stack.Children.Add(ParameterRow(p));

        foreach (var c in d.Arr("choices"))
        {
            int id = c.Int("id");
            var options = c.Arr("options").Select(o => o.AsStr()).ToList();
            var seg = Ui.Segmented(options, c.Int("selected"), i => V.Run("draft.setChoice", new JsonObject { ["id"] = id, ["index"] = i }));
            _choices[id] = seg;
            stack.Children.Add(Labeled(c.Str("label"), seg));
        }

        var combine = d.At("combineMode");
        if (combine != null)
        {
            var options = combine.Arr("options").ToList();
            _combine = Ui.Segmented(options.Select(o => o.Str("title")).ToList(), options.FindIndex(o => o.Str("id") == combine.Str("selected")),
                i => V.Run("draft.setCombineMode", new JsonObject { ["mode"] = options[i].Str("id") }));
            stack.Children.Add(Labeled("Result", _combine));
        }

        var axis = d.At("revolveAxis");
        if (axis != null)
        {
            var options = axis.Arr("options").ToList();
            _axis = Ui.Segmented(options.Select(o => o.Str("title")).ToList(), options.FindIndex(o => o.Bool("aroundV") == axis.Bool("aroundV")),
                i => V.Run("draft.setRevolveAxis", new JsonObject { ["aroundV"] = options[i].Bool("aroundV") }));
            stack.Children.Add(Labeled("Axis", _axis));
        }

        _error = Ui.Text("", 12, null, Ui.Caution, wrap: true);
        stack.Children.Add(_error);

        _apply = Ui.Button("Apply", () => V.Run("draft.apply"), accent: true, tooltip: "Apply (Enter)");
        var cancel = Ui.Button("Cancel", () => V.Run("draft.cancel"), tooltip: "Cancel (Esc)");
        stack.Children.Add(Ui.Row(cancel, _apply));
        Refresh();

        var card = Ui.Card(stack, 14, 16, opaque: false);
        card.Width = 320;
        card.HorizontalAlignment = HorizontalAlignment.Center;
        return card;
    }

    private static UIElement Labeled(string label, UIElement control)
    {
        var l = Ui.Text(label, 13);
        l.Width = 92;
        return Ui.HStack(8, l, control);
    }

    private UIElement ParameterRow(JsonNode? p)
    {
        int id = p.Int("id");
        var label = Ui.Text(p.Str("label"), 13);
        var field = new CommitField(p.StrOrNull("binding") ?? p.Str("text"),
            text => V.Run("draft.setParameter", new JsonObject { ["id"] = id, ["text"] = text }),
            tooltip: "A number, or an expression using variables");
        _fields[id] = field;
        var minus = Ui.IconButton("minus", () => V.Run("draft.stepParameter", new JsonObject { ["id"] = id, ["direction"] = -1 }), "Decrease");
        var plus = Ui.IconButton("plus", () => V.Run("draft.stepParameter", new JsonObject { ["id"] = id, ["direction"] = 1 }), "Increase");
        var unit = Ui.Text(p.Str("displayUnit"), 12, null, Ui.Secondary);
        unit.Width = 26;
        var grid = new Grid { ColumnSpacing = 6 };
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(92) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        UIElement[] cells = { label, minus, field.Box, unit, plus };
        for (int i = 0; i < cells.Length; i++)
        {
            Grid.SetColumn((FrameworkElement)cells[i], i);
            grid.Children.Add(cells[i]);
        }
        return grid;
    }

    protected override void Refresh()
    {
        var d = S.At("draft");
        if (d == null) return;
        foreach (var p in d.Arr("parameters"))
            if (_fields.TryGetValue(p.Int("id"), out var f)) f.Set(p.StrOrNull("binding") ?? p.Str("text"));
        foreach (var c in d.Arr("choices"))
            if (_choices.TryGetValue(c.Int("id"), out var seg)) Ui.SetSegment(seg, c.Int("selected"));
        var combine = d.At("combineMode");
        if (_combine != null && combine != null)
            Ui.SetSegment(_combine, combine.Arr("options").ToList().FindIndex(o => o.Str("id") == combine.Str("selected")));
        var axis = d.At("revolveAxis");
        if (_axis != null && axis != null)
            Ui.SetSegment(_axis, axis.Arr("options").ToList().FindIndex(o => o.Bool("aroundV") == axis.Bool("aroundV")));
        if (_error != null)
        {
            string? error = d.StrOrNull("error");
            _error.Text = error == null ? "" : "⚠ " + error;
            _error.Visibility = error == null ? Visibility.Collapsed : Visibility.Visible;
        }
        if (_apply != null) _apply.IsEnabled = d.Bool("canApply");
    }
}

/// <summary>Sketch mode: the constraint bar (or the selected dimension's value)
/// above the status and the ways out of the sketch.</summary>
public sealed class SketchBar : StatePanel
{
    private CommitField? _dimension, _offset, _fillet;

    public SketchBar(DocumentView view) : base(view) { }

    private bool Shown => S.At("draft") == null && S.At("sketch") != null;

    protected override string Signature()
    {
        if (!Shown) return "";
        var s = S.At("sketch");
        var dim = s.At("selectedDimension");
        return string.Join("|", s.Str("statusText"), s.At("status").Bool("fullyConstrained"), s.Str("profilesText"), s.Str("escapeTitle"),
            s.Str("replacing"), s.Bool("isEmpty"), s.At("constraintBar").Sig(), s.At("availableConstraints").Sig(),
            dim == null ? "" : dim.Str("index") + dim.Str("label") + dim.Str("displayUnit"), S.At("units").Str("id"));
    }

    protected override void Refresh()
    {
        var s = S.At("sketch");
        _dimension?.Set(s.At("selectedDimension").Str("text"));
        _offset?.Set(s.Str("offsetDistanceText"));
        _fillet?.Set(s.Str("filletRadiusText"));
    }

    protected override UIElement? Build()
    {
        _dimension = _offset = _fillet = null;
        if (!Shown) return null;
        var s = S.At("sketch");
        var stack = new StackPanel { Spacing = 8, HorizontalAlignment = HorizontalAlignment.Center };
        if (s.At("constraintBar").Bool("visible")) stack.Children.Add(ConstraintBar(s));

        var row = Ui.HStack(6);
        var status = new StackPanel { Spacing = 1, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(6, 0, 6, 0) };
        bool full = s.At("status").Bool("fullyConstrained") && !s.Bool("isEmpty");
        var statusText = Ui.Text(s.Str("statusText"), 12, null, full ? Ui.Success : Ui.Primary);
        status.Children.Add(full ? Ui.HStack(4, Icons.Create("checkmark.seal.fill", 11, Ui.Success), statusText) : statusText);
        status.Children.Add(Ui.Text(s.Str("profilesText"), 12, null, Ui.Secondary));
        row.Children.Add(status);
        row.Children.Add(Ui.Button(s.Str("escapeTitle", "Cancel"), () => V.Run("sketch.escape"), tooltip: "Esc"));
        bool empty = s.Bool("isEmpty");
        if (s.StrOrNull("replacing") != null)
        {
            row.Children.Add(Ui.Button("Done", () => V.Run("sketch.finish"), "checkmark", accent: true, tooltip: "Done (Enter)"));
        }
        else
        {
            row.Children.Add(Ui.Button("Done", () => V.Run("sketch.finishAsItem"), "checkmark", enabled: !empty,
                tooltip: "Keep the sketch as an item, for sweep, loft or later use"));
            row.Children.Add(Ui.Button("Revolve", () => V.Run("sketch.revolve"), "arrow.triangle.2.circlepath", enabled: !empty));
            row.Children.Add(Ui.Button("Extrude", () => V.Run("sketch.finish"), "square.stack.3d.up", accent: true, enabled: !empty, tooltip: "Extrude (Enter)"));
        }
        var actions = Ui.Card(row, 6, 14);
        actions.HorizontalAlignment = HorizontalAlignment.Center;
        stack.Children.Add(actions);
        return stack;
    }

    private UIElement ConstraintBar(JsonNode? s)
    {
        var row = Ui.HStack(6);
        var dim = s.At("selectedDimension");
        var bar = s.At("constraintBar");
        if (dim != null)
        {
            int index = dim.Int("index");
            double step = dim.Num("step", 1), shown = dim.Num("displayValue");
            _dimension = new CommitField(dim.Str("text"),
                t => V.Run("sketch.setDimension", new JsonObject { ["index"] = index, ["text"] = t }), 110, "A number, or an expression using variables");
            var label = Ui.Text(dim.Str("label"), 13);
            label.Width = 92;
            row.Children.Add(label);
            row.Children.Add(Ui.IconButton("minus", () => StepDimension(index, -1), "Decrease"));
            row.Children.Add(_dimension.Box);
            row.Children.Add(Ui.Text(dim.Str("displayUnit"), 12, null, Ui.Secondary));
            row.Children.Add(Ui.IconButton("plus", () => StepDimension(index, 1), "Increase"));
        }
        else
        {
            foreach (var c in s.Arr("availableConstraints"))
            {
                string kind = c.Str("kind");
                row.Children.Add(Ui.Button(c.Str("title"), () => V.Run("sketch.constrain", new JsonObject { ["kind"] = kind }), c.Str("symbol")));
            }
            if (bar.Bool("noneFits")) row.Children.Add(Ui.Caption("No constraint fits this selection", false));
            if (bar.Bool("showOffset"))
            {
                row.Children.Add(Ui.Button("Offset", () => V.Run("sketch.offset"), "square.on.square",
                    tooltip: "Parallel copy of the selected lines, arcs and circles"));
                _offset = new CommitField(s.Str("offsetDistanceText"), t => V.Run("sketch.setOffsetDistance", new JsonObject { ["text"] = t }), 64);
                row.Children.Add(_offset.Box);
            }
            if (bar.Bool("showFillet"))
            {
                row.Children.Add(Ui.Button("Fillet", () => V.Run("sketch.fillet"), "arrow.turn.up.right", tooltip: "Round the corner with a tangent arc"));
                _fillet = new CommitField(s.Str("filletRadiusText"), t => V.Run("sketch.setFilletRadius", new JsonObject { ["text"] = t }), 64);
                row.Children.Add(_fillet.Box);
            }
            if (bar.Bool("showDisconnect"))
                row.Children.Add(Ui.Button("Disconnect", () => V.Run("sketch.disconnect"), "link.badge.plus",
                    tooltip: "Give each curve meeting here its own end point"));
            if (bar.Bool("showMakeConstruction"))
                row.Children.Add(Ui.Button("Make Construction", () => V.Run("sketch.toggleConstruction"), "line.diagonal",
                    tooltip: "Construction geometry guides the sketch but makes no profile"));
        }
        row.Children.Add(Ui.Divider(vertical: true));
        row.Children.Add(Ui.IconButton("trash", () => V.Run("sketch.deleteSelection"), "Delete selection (Del)"));
        var card = Ui.Card(new ScrollViewer
        {
            Content = row,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Auto,
            VerticalScrollBarVisibility = ScrollBarVisibility.Disabled,
            HorizontalScrollMode = ScrollMode.Auto,
        }, 6, 14);
        card.HorizontalAlignment = HorizontalAlignment.Center;
        return card;
    }

    private void StepDimension(int index, int direction)
    {
        var dim = S.At("sketch").At("selectedDimension");
        if (dim == null) return;
        double value = dim.Num("displayValue") + direction * dim.Num("step", 1);
        V.Run("sketch.setDimension", new JsonObject { ["index"] = index, ["text"] = Ui.Invariant(value) });
    }
}

public sealed class SectionPanel : StatePanel
{
    private StackPanel? _axis;
    private SliderBinding? _offset;
    private TextBlock? _offsetText;
    private ToggleSwitch? _flip;

    public SectionPanel(DocumentView view) : base(view) { }

    protected override string Signature()
    {
        var sec = S.At("view").At("section");
        return sec.Bool("enabled") ? "on" + sec.At("range").Sig() : "";
    }

    protected override void Refresh()
    {
        var sec = S.At("view").At("section");
        if (_axis != null) Ui.SetSegment(_axis, sec.Int("axis"));
        _offset?.Set(sec.Num("offset"));
        if (_offsetText != null) _offsetText.Text = sec.Str("offsetText");
        if (_flip != null && _flip.IsOn != sec.Bool("flipped")) _flip.IsOn = sec.Bool("flipped");
    }

    protected override UIElement? Build()
    {
        var sec = S.At("view").At("section");
        if (!sec.Bool("enabled")) return null;
        var header = Ui.Row(Ui.HStack(6, Icons.Create("square.split.bottomrightquarter", 14), Ui.Headline("Section View")),
            Ui.IconButton("xmark", () => V.Run("view.section", new JsonObject { ["enabled"] = false }), "Close section view"));
        _axis = Ui.Segmented(new[] { "X", "Y", "Z" }, sec.Int("axis"), i => V.Run("view.section", new JsonObject { ["axis"] = i }));
        var range = sec.At("range");
        _offset = new SliderBinding(range.At(0).AsNum(-100), range.At(1).AsNum(100), sec.Num("offset"),
            v => V.Run("view.section", new JsonObject { ["offset"] = v }), 0.1);
        _offset.Slider.Width = 170;
        _offsetText = Ui.Mono(sec.Str("offsetText"), 12);
        _offsetText.Width = 44;
        _flip = new ToggleSwitch { OnContent = "Flip side", OffContent = "Flip side", IsOn = sec.Bool("flipped") };
        _flip.Toggled += (_, _) =>
        {
            if (_flip.IsOn != S.At("view").At("section").Bool("flipped")) V.Run("view.section", new JsonObject { ["flipped"] = _flip.IsOn });
        };
        var stack = Ui.VStack(8, header, Ui.HStack(8, Ui.Text("Cut along", 13, null, Ui.Secondary), _axis),
            Ui.HStack(8, Ui.Text("Offset", 13, null, Ui.Secondary), _offset.Slider, _offsetText), _flip);
        var card = Ui.Card(stack, 14);
        card.Width = 320;
        card.HorizontalAlignment = HorizontalAlignment.Center;
        return card;
    }
}

/// <summary>The prompt shown while picking points, with a way out.</summary>
public sealed class PointPickBanner : StatePanel
{
    public PointPickBanner(DocumentView view) : base(view) { }

    protected override string Signature() => S.At("pointPick").Sig();

    protected override UIElement? Build()
    {
        var p = S.At("pointPick");
        if (p == null) return null;
        var text = Ui.VStack(1, Ui.Text(p.Str("title"), 13, FontWeights.SemiBold), Ui.Text(p.Str("text"), 12, null, Ui.Secondary));
        var row = Ui.HStack(10, Icons.Create("scope", 16, Ui.AccentText), text, Ui.Button("Cancel", () => V.Run("pick.cancel"), tooltip: "Esc"));
        var capsule = Ui.Capsule(row, 14, 6);
        capsule.HorizontalAlignment = HorizontalAlignment.Center;
        return capsule;
    }
}

/// <summary>Pinned measurements, kept on screen outside Measure mode, and Measure mode's hint.</summary>
public sealed class PinnedCard : StatePanel
{
    public PinnedCard(DocumentView view) : base(view) { }

    protected override string Signature() =>
        S.At("view").Bool("measuring") + S.At("view").Bool("showPinnedMeasurements").ToString() + S.At("measure").At("pinned").Sig();

    protected override UIElement? Build()
    {
        if (S.At("view").Bool("measuring"))
        {
            var hint = Ui.Capsule(Ui.Text("Select objects to list available measurements.", 13), 14, 8);
            hint.HorizontalAlignment = HorizontalAlignment.Center;
            return hint;
        }
        var pinned = S.At("measure").Arr("pinned").ToList();
        if (!S.At("view").Bool("showPinnedMeasurements") || pinned.Count == 0) return null;
        var stack = new StackPanel { Spacing = 4 };
        for (int i = 0; i < pinned.Count; i++)
        {
            int index = i;
            stack.Children.Add(Ui.HStack(6, Icons.Create("pin.fill", 10, Ui.Secondary), Ui.Mono(pinned[i].AsStr(), 12),
                Ui.Plain(Icons.Create("xmark", 10, Ui.Secondary), () => V.Run("measure.unpin", new JsonObject { ["index"] = index }), "Unpin")));
        }
        var card = Ui.Card(stack, 10, 12);
        card.HorizontalAlignment = HorizontalAlignment.Center;
        return card;
    }
}

/// <summary>Measure mode's side panel: the measurements, deltas and pins.</summary>
public sealed class MeasureSidePanel : StatePanel
{
    public MeasureSidePanel(DocumentView view) : base(view) { }

    protected override string Signature() => S.Str("mode") == "measuring" ? S.At("measure").Sig() + S.At("view").Bool("measureDeltas") : "";

    protected override UIElement? Build()
    {
        if (S.Str("mode") != "measuring") return null;
        var m = S.At("measure");
        var stack = new StackPanel { Spacing = 10 };
        stack.Children.Add(Ui.Row(Ui.Headline("Measure"), Ui.IconButton("xmark", () => V.Run("view.setMeasuring", new JsonObject { ["on"] = false }), "Close")));
        var deltas = new ToggleSwitch { OnContent = "Include X, Y, Z Deltas", OffContent = "Include X, Y, Z Deltas", IsOn = S.At("view").Bool("measureDeltas") };
        deltas.Toggled += (_, _) => V.Run("view.set", new JsonObject { ["measureDeltas"] = deltas.IsOn });
        stack.Children.Add(deltas);
        var rows = m.Arr("rows").ToList();
        if (rows.Count == 0) stack.Children.Add(Ui.Caption(m.Str("message")));
        foreach (var r in rows)
        {
            string label = r.Str("label"), text = r.Str("text");
            var value = Ui.Mono(text, 13, FontWeights.Medium);
            value.IsTextSelectionEnabled = true;
            var pin = Ui.Plain(Icons.Create("pin", 12, Ui.Secondary),
                () => V.Run("measure.pin", new JsonObject { ["label"] = label, ["value"] = text }), "Pin to always show");
            pin.Width = 24;
            stack.Children.Add(Ui.Row(Ui.Text(label, 13, null, Ui.Secondary), Ui.HStack(6, value, pin)));
        }
        stack.Children.Add(Ui.Divider());
        stack.Children.Add(Ui.Text("Pinned", 12, FontWeights.SemiBold, Ui.Secondary));
        var pinned = m.Arr("pinned").ToList();
        if (pinned.Count == 0) stack.Children.Add(Ui.Caption("Pin measurements to keep them visible."));
        for (int i = 0; i < pinned.Count; i++)
        {
            int index = i;
            var unpin = Ui.Plain(Icons.Create("pin.slash", 12, Ui.Secondary), () => V.Run("measure.unpin", new JsonObject { ["index"] = index }), "Unpin");
            unpin.Width = 24;
            stack.Children.Add(Ui.Row(Ui.Mono(pinned[i].AsStr(), 12), unpin));
        }
        var card = Ui.Card(new ScrollViewer { Content = stack, VerticalScrollBarVisibility = ScrollBarVisibility.Auto }, 14);
        card.Width = 280;
        card.VerticalAlignment = VerticalAlignment.Top;
        return card;
    }
}
