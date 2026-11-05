// Building blocks for the code-built UI: theme brushes, text, buttons, cards,
// key caps, label pills and segmented pickers. Brushes come from the WinUI
// theme dictionaries so the app follows light/dark (panels rebuild on theme
// changes, see DocumentView).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace Sculpt3D.UI;

public static class Ui
{
    public const double RailRow = 40;

    // ---- Brushes ----------------------------------------------------------

    public static Brush Brush(string key, Brush? fallback = null)
    {
        if (Application.Current.Resources.TryGetValue(key, out object value) && value is Brush b) return b;
        return fallback ?? new SolidColorBrush(Colors.Gray);
    }

    public static Brush BrushFromHex(string hex)
    {
        hex = hex.TrimStart('#');
        byte a = 255;
        int i = 0;
        if (hex.Length == 8) { a = byte.Parse(hex[..2], NumberStyles.HexNumber); i = 2; }
        byte r = byte.Parse(hex.Substring(i, 2), NumberStyles.HexNumber);
        byte g = byte.Parse(hex.Substring(i + 2, 2), NumberStyles.HexNumber);
        byte bl = byte.Parse(hex.Substring(i + 4, 2), NumberStyles.HexNumber);
        return new SolidColorBrush(ColorHelper.FromArgb(a, r, g, bl));
    }

    public static SolidColorBrush Solid(Color c) => new(c);

    /// <summary>Panels over the 3D view: in-app acrylic like the Mac's thick material.</summary>
    public static Brush PanelBackground => Brush("AcrylicInAppFillColorDefaultBrush", Brush("LayerFillColorDefaultBrush"));
    public static Brush OpaquePanelBackground => Brush("SolidBackgroundFillColorBaseBrush", Brush("LayerFillColorDefaultBrush"));
    public static Brush Stroke => Brush("CardStrokeColorDefaultBrush", Brush("ControlStrokeColorDefaultBrush"));
    public static Brush Primary => Brush("TextFillColorPrimaryBrush");
    public static Brush Secondary => Brush("TextFillColorSecondaryBrush");
    public static Brush Tertiary => Brush("TextFillColorTertiaryBrush");
    public static Brush Accent => Brush("AccentFillColorDefaultBrush");
    public static Brush AccentText => Brush("AccentTextFillColorPrimaryBrush", Accent);
    public static Brush OnAccent => Brush("TextOnAccentFillColorPrimaryBrush", new SolidColorBrush(Colors.White));
    public static Brush Caution => Brush("SystemFillColorCautionBrush", new SolidColorBrush(Colors.Orange));
    public static Brush Critical => Brush("SystemFillColorCriticalBrush", new SolidColorBrush(Colors.Red));
    public static Brush Success => Brush("SystemFillColorSuccessBrush", new SolidColorBrush(Colors.Green));
    public static Brush Subtle => Brush("SubtleFillColorSecondaryBrush", new SolidColorBrush(ColorHelper.FromArgb(20, 128, 128, 128)));
    public static Brush ActiveTile => Brush("TextFillColorPrimaryBrush");

    // ---- Text -------------------------------------------------------------

    public static TextBlock Text(string text, double size = 13, Windows.UI.Text.FontWeight? weight = null, Brush? brush = null, bool wrap = false)
    {
        var t = new TextBlock { Text = text, FontSize = size, VerticalAlignment = VerticalAlignment.Center };
        if (weight.HasValue) t.FontWeight = weight.Value;
        if (brush != null) t.Foreground = brush;
        if (wrap) t.TextWrapping = TextWrapping.WrapWholeWords;
        else t.TextTrimming = TextTrimming.CharacterEllipsis;
        return t;
    }

    public static TextBlock Headline(string text) => Text(text, 15, FontWeights.SemiBold);
    public static TextBlock Caption(string text, bool wrap = true) => Text(text, 12, null, Secondary, wrap);
    public static TextBlock Mono(string text, double size = 13, Windows.UI.Text.FontWeight? weight = null)
    {
        var t = Text(text, size, weight);
        t.FontFamily = new FontFamily("Cascadia Mono, Consolas, Segoe UI");
        return t;
    }

    // ---- Layout -----------------------------------------------------------

    public static StackPanel HStack(double spacing, params UIElement?[] children)
    {
        var s = new StackPanel { Orientation = Orientation.Horizontal, Spacing = spacing };
        foreach (var c in children) if (c != null) s.Children.Add(c);
        return s;
    }

    public static StackPanel VStack(double spacing, params UIElement?[] children)
    {
        var s = new StackPanel { Orientation = Orientation.Vertical, Spacing = spacing };
        foreach (var c in children) if (c != null) s.Children.Add(c);
        return s;
    }

    /// <summary>A row with `left` taking the room and `right` at its natural width.</summary>
    public static Grid Row(UIElement left, UIElement? right, double spacing = 8)
    {
        var g = new Grid { ColumnSpacing = spacing };
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        g.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        g.Children.Add(left);
        if (right != null)
        {
            Grid.SetColumn((FrameworkElement)right, 1);
            g.Children.Add(right);
        }
        return g;
    }

    /// <summary>A floating card over the viewport.</summary>
    public static Border Card(UIElement child, double padding = 14, double corner = 16, bool opaque = false)
    {
        return new Border
        {
            Child = child,
            Padding = new Thickness(padding),
            CornerRadius = new CornerRadius(corner),
            Background = opaque ? OpaquePanelBackground : PanelBackground,
            BorderBrush = Stroke,
            BorderThickness = new Thickness(1),
        };
    }

    public static Border Capsule(UIElement child, double hPad = 14, double vPad = 8)
    {
        return new Border
        {
            Child = child,
            Padding = new Thickness(hPad, vPad, hPad, vPad),
            CornerRadius = new CornerRadius(999),
            Background = PanelBackground,
            BorderBrush = Stroke,
            BorderThickness = new Thickness(1),
        };
    }

    public static Border Divider(bool vertical = false)
    {
        return vertical
            ? new Border { Width = 1, Height = 24, Background = Stroke, Margin = new Thickness(2, 0, 2, 0), VerticalAlignment = VerticalAlignment.Center }
            : new Border { Height = 1, Background = Stroke, Margin = new Thickness(0, 4, 0, 4) };
    }

    // ---- Buttons ----------------------------------------------------------

    /// <summary>Buttons over the viewport don't take keyboard focus on click, so
    /// viewport keys (Esc, Return, tool letters) keep working, like the Mac.</summary>
    public static T NoFocus<T>(T control) where T : Control
    {
        control.AllowFocusOnInteraction = false;
        return control;
    }

    public static Button Button(string title, Action onClick, string? symbol = null, bool accent = false, bool enabled = true, string? tooltip = null)
    {
        object content = symbol == null
            ? title
            : HStack(6, Icons.Create(symbol, 14), new TextBlock { Text = title, VerticalAlignment = VerticalAlignment.Center });
        var b = new Button { Content = content, IsEnabled = enabled, MinHeight = 32 };
        if (accent && Application.Current.Resources.TryGetValue("AccentButtonStyle", out object s) && s is Style style) b.Style = style;
        b.Click += (_, _) => onClick();
        if (tooltip != null) ToolTipService.SetToolTip(b, tooltip);
        return NoFocus(b);
    }

    public static Button IconButton(string symbol, Action onClick, string tooltip, bool enabled = true, double size = 15, bool accent = false)
    {
        var b = new Button
        {
            Content = Icons.Create(symbol, size),
            IsEnabled = enabled,
            MinWidth = 34,
            MinHeight = 32,
            Padding = new Thickness(6, 4, 6, 4),
        };
        if (accent && Application.Current.Resources.TryGetValue("AccentButtonStyle", out object s) && s is Style style) b.Style = style;
        b.Click += (_, _) => onClick();
        ToolTipService.SetToolTip(b, tooltip);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(b, tooltip);
        return NoFocus(b);
    }

    /// <summary>A flat button (no chrome) around arbitrary content.</summary>
    public static Button Plain(UIElement content, Action onClick, string? tooltip = null, bool enabled = true)
    {
        var b = new Button
        {
            Content = content,
            IsEnabled = enabled,
            Padding = new Thickness(0),
            Background = new SolidColorBrush(Colors.Transparent),
            BorderThickness = new Thickness(0),
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            VerticalContentAlignment = VerticalAlignment.Stretch,
        };
        b.Click += (_, _) => onClick();
        if (tooltip != null)
        {
            ToolTipService.SetToolTip(b, tooltip);
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(b, tooltip);
        }
        return NoFocus(b);
    }

    /// <summary>A row of toggle buttons acting as a segmented picker. Clicking a
    /// segment selects it (and calls onSelect); SetSegment updates it from state.</summary>
    public static StackPanel Segmented(IReadOnlyList<string> options, int selected, Action<int> onSelect, bool enabled = true)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2 };
        for (int i = 0; i < options.Count; i++)
        {
            int index = i;
            var t = new ToggleButton
            {
                Content = options[i],
                IsChecked = i == selected,
                IsEnabled = enabled,
                MinWidth = 44,
                Padding = new Thickness(10, 4, 10, 4),
            };
            t.Click += (_, _) =>
            {
                SetSegment(row, index);
                onSelect(index);
            };
            row.Children.Add(NoFocus(t));
        }
        return row;
    }

    public static void SetSegment(StackPanel row, int selected)
    {
        for (int i = 0; i < row.Children.Count; i++)
            if (row.Children[i] is ToggleButton t) t.IsChecked = i == selected;
    }

    // ---- Rail building blocks -------------------------------------------------

    /// <summary>Converts the Mac's modifier glyphs ("⇧⌘F") to Windows key caps ["Ctrl", "Shift", "F"].</summary>
    public static List<string> KeyNames(string keys)
    {
        bool ctrl = false, shift = false, alt = false;
        var rest = new List<string>();
        foreach (char ch in keys)
        {
            switch (ch)
            {
                case '⌘':
                case '⌃': ctrl = true; break;
                case '⇧': shift = true; break;
                case '⌥': alt = true; break;
                case '⌫': rest.Add("Del"); break;
                case '↩': rest.Add("Enter"); break;
                default: rest.Add(ch.ToString().ToUpperInvariant()); break;
            }
        }
        var caps = new List<string>();
        if (ctrl) caps.Add("Ctrl");
        if (alt) caps.Add("Alt");
        if (shift) caps.Add("Shift");
        caps.AddRange(rest);
        return caps;
    }

    /// <summary>Plain-text form of a shortcut for menus and tooltips ("Ctrl+Shift+F").</summary>
    public static string KeyText(string keys) => string.Join("+", KeyNames(keys));

    public static StackPanel KeyCaps(string keys)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2, VerticalAlignment = VerticalAlignment.Center };
        foreach (var k in KeyNames(keys))
        {
            row.Children.Add(new Border
            {
                Background = Subtle,
                CornerRadius = new CornerRadius(3),
                Padding = new Thickness(3, 0, 3, 1),
                MinWidth = 14,
                Child = new TextBlock { Text = k, FontSize = 9, FontWeight = FontWeights.SemiBold, Foreground = Secondary, TextAlignment = TextAlignment.Center },
            });
        }
        return row;
    }

    /// <summary>The label beside a rail icon: a separate rounded pill.</summary>
    public static Border LabelPill(string title, string? subtitle, string? keys, bool enabled, bool dot)
    {
        var text = new StackPanel { Spacing = 0, VerticalAlignment = VerticalAlignment.Center };
        text.Children.Add(new TextBlock { Text = title, FontSize = 13, FontWeight = FontWeights.SemiBold });
        if (!string.IsNullOrEmpty(subtitle)) text.Children.Add(new TextBlock { Text = subtitle, FontSize = 10, Foreground = Secondary });
        var row = HStack(6, text);
        if (!string.IsNullOrEmpty(keys)) row.Children.Add(KeyCaps(keys));
        if (dot) row.Children.Add(new Microsoft.UI.Xaml.Shapes.Ellipse { Width = 6, Height = 6, Fill = Accent, VerticalAlignment = VerticalAlignment.Center });
        return new Border
        {
            Child = row,
            Padding = new Thickness(9, 5, 9, 5),
            CornerRadius = new CornerRadius(9),
            Background = PanelBackground,
            BorderBrush = Stroke,
            BorderThickness = new Thickness(1),
            Opacity = enabled ? 1 : 0.6,
            VerticalAlignment = VerticalAlignment.Center,
        };
    }

    /// <summary>A rail icon tile: dark glyph on a soft tile, inverted when active.</summary>
    public static Border RailIcon(string symbol, bool active, bool enabled)
    {
        var fg = active ? Brush("TextFillColorInverseBrush", new SolidColorBrush(Colors.White)) : Primary;
        var icon = Icons.Create(symbol, 17, fg);
        icon.HorizontalAlignment = HorizontalAlignment.Center;
        icon.VerticalAlignment = VerticalAlignment.Center;
        return new Border
        {
            Width = 34,
            Height = 34,
            CornerRadius = new CornerRadius(9),
            Background = active ? ActiveTile : new SolidColorBrush(Colors.Transparent),
            Child = icon,
            Opacity = enabled ? 1 : 0.45,
        };
    }

    // ---- Numbers ----------------------------------------------------------

    /// <summary>Parses what a user typed: "," or "." decimals.</summary>
    public static bool TryParseNumber(string text, out double value)
    {
        text = text.Trim().Replace(',', '.');
        return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value);
    }

    public static string Invariant(double v) => v.ToString("0.######", CultureInfo.InvariantCulture);
}

/// <summary>A slider that reports user changes live and can be updated from the
/// state without echoing back, and without jumping while it is being dragged.</summary>
public sealed class SliderBinding
{
    private bool _suppress, _dragging;
    public Slider Slider { get; }

    /// <summary>Sliders being dragged right now: panels hold off rebuilding meanwhile.</summary>
    public static int Dragging { get; private set; }

    /// <summary>Raised when a drag ends, so deferred rebuilds can happen.</summary>
    public static event Action? DragEnded;

    private void SetDragging(bool on)
    {
        if (on == _dragging) return;
        _dragging = on;
        Dragging += on ? 1 : -1;
        if (!on) DragEnded?.Invoke();
    }

    public SliderBinding(double min, double max, double value, Action<double> onChange, double step = 0.01)
    {
        Slider = new Slider { Minimum = min, Maximum = max, StepFrequency = step, Value = Math.Clamp(value, min, max), IsThumbToolTipEnabled = false };
        Slider.ValueChanged += (_, e) =>
        {
            if (!_suppress && Math.Abs(e.NewValue - e.OldValue) > 1e-9) onChange(e.NewValue);
        };
        Slider.AddHandler(UIElement.PointerPressedEvent, new Microsoft.UI.Xaml.Input.PointerEventHandler((_, _) => SetDragging(true)), true);
        Slider.AddHandler(UIElement.PointerReleasedEvent, new Microsoft.UI.Xaml.Input.PointerEventHandler((_, _) => SetDragging(false)), true);
        Slider.AddHandler(UIElement.PointerCaptureLostEvent, new Microsoft.UI.Xaml.Input.PointerEventHandler((_, _) => SetDragging(false)), true);
        Slider.Unloaded += (_, _) => SetDragging(false);
        Ui.NoFocus(Slider);
    }

    public void Set(double value)
    {
        if (_dragging) return;
        value = Math.Clamp(value, Slider.Minimum, Slider.Maximum);
        if (Math.Abs(Slider.Value - value) < 1e-9) return;
        _suppress = true;
        Slider.Value = value;
        _suppress = false;
    }
}
