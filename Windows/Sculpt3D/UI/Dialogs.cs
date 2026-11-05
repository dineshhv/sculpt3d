// Sheets and dialogs: Variables, Field of View, Sketch transforms, Sketch
// text, Export / Export to Slicer, save prompts, About and Controls
// (MenuPanels.swift, ExportSheet.swift, Panels.swift VariablesPanel).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Sculpt3D.Core;
using Sculpt3D.Native;

namespace Sculpt3D.UI;

public static class Dialogs
{
    private static bool _open;

    /// <summary>Shows a ContentDialog (one at a time, as WinUI requires).</summary>
    public static async Task<ContentDialogResult> ShowAsync(XamlRoot root, ContentDialog dialog)
    {
        if (_open || root == null) return ContentDialogResult.None;
        _open = true;
        dialog.XamlRoot = root;
        if (Application.Current.Resources.TryGetValue("DefaultContentDialogStyle", out object style) && style is Style s) dialog.Style = s;
        try
        {
            return await dialog.ShowAsync();
        }
        finally
        {
            _open = false;
        }
    }

    public static bool IsOpen => _open;

    public static Task<ContentDialogResult> MessageAsync(XamlRoot root, string title, string message) =>
        ShowAsync(root, new ContentDialog { Title = title, Content = Ui.Text(message, 14, null, null, wrap: true), CloseButtonText = "OK" });

    /// <summary>"Save changes to X?" → Primary = Save, Secondary = Don't Save, None = Cancel.</summary>
    public static Task<ContentDialogResult> AskSaveAsync(XamlRoot root, string name) =>
        ShowAsync(root, new ContentDialog
        {
            Title = $"Do you want to save the changes to “{name}”?",
            Content = Ui.Text("Your changes will be lost if you don't save them.", 14, null, null, wrap: true),
            PrimaryButtonText = "Save",
            SecondaryButtonText = "Don't Save",
            CloseButtonText = "Cancel",
            DefaultButton = ContentDialogButton.Primary,
        });

    // ---- Variables -----------------------------------------------------------------

    public static async Task ShowVariablesAsync(DocumentView v)
    {
        var view = new VariablesView(v);
        view.Update();
        v.LivePanels.Add(view);
        try
        {
            await ShowAsync(v.XamlRoot, new ContentDialog { Title = "Variables", Content = view, CloseButtonText = "Done", DefaultButton = ContentDialogButton.Close });
        }
        finally
        {
            v.LivePanels.Remove(view);
            v.FocusViewport();
        }
    }

    private sealed class VariablesView : StatePanel
    {
        private readonly Dictionary<string, (CommitField Name, CommitField Expression, TextBlock Value)> _rows = new();

        public VariablesView(DocumentView view) : base(view) { }

        protected override string Signature() => string.Join(",", S.At("settings").Arr("variables").Select(x => x.Str("id")));

        protected override void Refresh()
        {
            foreach (var x in S.At("settings").Arr("variables"))
            {
                if (!_rows.TryGetValue(x.Str("id"), out var row)) continue;
                row.Name.Set(x.Str("name"));
                row.Expression.Set(x.Str("expression"));
                row.Value.Text = x.Str("valueText");
                row.Value.Foreground = x.At("value") == null ? Ui.Critical : Ui.Secondary;
            }
        }

        protected override UIElement Build()
        {
            _rows.Clear();
            var stack = new StackPanel { Spacing = 8, MinWidth = 460 };
            stack.Children.Add(Ui.Caption("Type a variable's name, or an expression like “width * 2”, into any length or angle field."));
            foreach (var x in S.At("settings").Arr("variables"))
            {
                string id = x.Str("id");
                var name = new CommitField(x.Str("name"), t => V.Run("variable.update", new JsonObject { ["id"] = id, ["name"] = t }), 110);
                name.Box.TextAlignment = TextAlignment.Left;
                name.Box.PlaceholderText = "name";
                var expression = new CommitField(x.Str("expression"), t => V.Run("variable.update", new JsonObject { ["id"] = id, ["expression"] = t }), 180);
                expression.Box.TextAlignment = TextAlignment.Left;
                expression.Box.PlaceholderText = "expression";
                var value = Ui.Mono(x.Str("valueText"), 13);
                value.Width = 70;
                value.TextAlignment = TextAlignment.Right;
                value.Foreground = x.At("value") == null ? Ui.Critical : Ui.Secondary;
                var trash = Ui.IconButton("trash", () => V.Run("variable.remove", new JsonObject { ["id"] = id }), "Remove variable");
                stack.Children.Add(Ui.HStack(8, name.Box, Ui.Text("=", 14), expression.Box, value, trash));
                _rows[id] = (name, expression, value);
            }
            stack.Children.Add(Ui.Button("Add", () => V.Run("variable.add"), "plus"));
            return stack;
        }
    }

    // ---- Field of view ----------------------------------------------------------------

    public static async Task ShowFieldOfViewAsync(DocumentView v)
    {
        var cam = v.Doc.State.At("settings").At("camera");
        var ortho = new ToggleSwitch { OnContent = "Orthographic (no perspective)", OffContent = "Orthographic (no perspective)", IsOn = cam.Bool("orthographic") };
        var value = Ui.Mono($"{cam.Num("fieldOfView", 40):0}°", 13);
        var slider = new SliderBinding(10, 100, cam.Num("fieldOfView", 40), x =>
        {
            value.Text = $"{x:0}°";
            v.Run("settings.setCamera", new JsonObject { ["fieldOfView"] = x });
        }, 1);
        slider.Slider.Width = 240;
        slider.Slider.IsEnabled = !ortho.IsOn;
        ortho.Toggled += (_, _) =>
        {
            slider.Slider.IsEnabled = !ortho.IsOn;
            v.Run("settings.setCamera", new JsonObject { ["orthographic"] = ortho.IsOn });
        };
        var content = Ui.VStack(12, ortho, Ui.HStack(10, slider.Slider, value),
            Ui.Caption("Wide angles exaggerate perspective; narrow ones flatten it, like a telephoto lens."));
        var dialog = new ContentDialog
        {
            Title = "Field of View",
            Content = content,
            PrimaryButtonText = "Reset",
            CloseButtonText = "Done",
            DefaultButton = ContentDialogButton.Close,
        };
        dialog.PrimaryButtonClick += (_, e) =>
        {
            e.Cancel = true; // keep the dialog open, like the Mac sheet
            v.Run("settings.setCamera", new JsonObject { ["reset"] = true });
            var c = v.Doc.State.At("settings").At("camera");
            ortho.IsOn = c.Bool("orthographic");
            slider.Set(c.Num("fieldOfView", 40));
            value.Text = $"{c.Num("fieldOfView", 40):0}°";
        };
        await ShowAsync(v.XamlRoot, dialog);
    }

    // ---- Sketch transforms -------------------------------------------------------------

    public static async Task ShowSketchTransformAsync(DocumentView v, string kind)
    {
        string title = kind switch
        {
            "scale" => "Scale Sketch",
            "rotate" => "Rotate Sketch Around Axis",
            "linearPattern" => "Pattern - Linear",
            _ => "Pattern - Circular",
        };
        string hint = kind switch
        {
            "scale" => "Scales the selected geometry about a selected point, or its centre.",
            "rotate" => "Turns the selected geometry about a selected point, or the sketch origin.",
            "linearPattern" => "Repeats the selected geometry along a direction (0° is the sketch's X axis).",
            _ => "Repeats the selected geometry around a selected point, or the sketch origin.",
        };
        NumberBox Number(string header, double value, double min = double.MinValue, double max = double.MaxValue) =>
            new() { Header = header, Value = value, Minimum = min, Maximum = max, SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact, Width = 220 };
        var factor = Number("Scale factor (×)", 2, 0.0001);
        var angle = Number("Angle (°)", 90);
        var count = Number("Instances", 3, 2, kind == "linearPattern" ? 200 : 360);
        var spacing = Number($"Spacing ({v.Doc.State.At("units").Str("symbol", "mm")})", 20 / Math.Max(v.Doc.State.At("units").Num("scale", 1), 1e-9));
        var direction = Number("Direction (°)", 0);
        var total = Number("Total angle (°)", 360);
        var keep = new CheckBox { Content = "Keep original" };
        var stack = Ui.VStack(10, Ui.Caption(hint));
        switch (kind)
        {
            case "scale": stack.Children.Add(factor); stack.Children.Add(keep); break;
            case "rotate": stack.Children.Add(angle); stack.Children.Add(keep); break;
            case "linearPattern": stack.Children.Add(count); stack.Children.Add(spacing); stack.Children.Add(direction); break;
            default: stack.Children.Add(count); stack.Children.Add(total); break;
        }
        var dialog = new ContentDialog { Title = title, Content = stack, PrimaryButtonText = "Apply", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Primary };
        if (await ShowAsync(v.XamlRoot, dialog) != ContentDialogResult.Primary) return;
        static double N(NumberBox b, double fallback) => double.IsNaN(b.Value) ? fallback : b.Value;
        var args = new JsonObject { ["kind"] = kind };
        switch (kind)
        {
            case "scale":
                args["factor"] = N(factor, 2);
                args["keepOriginal"] = keep.IsChecked == true;
                break;
            case "rotate":
                args["angle"] = N(angle, 90);
                args["keepOriginal"] = keep.IsChecked == true;
                break;
            case "linearPattern":
                args["count"] = (int)Math.Round(N(count, 3));
                args["spacing"] = N(spacing, 20) * v.Doc.State.At("units").Num("scale", 1);
                args["direction"] = N(direction, 0);
                break;
            default:
                args["count"] = (int)Math.Round(N(count, 3));
                args["totalAngle"] = N(total, 360);
                break;
        }
        v.Run("sketch.transform", args);
        v.FocusViewport();
    }

    // ---- Sketch text ----------------------------------------------------------------

    private static string _lastText = "Sculpt3D", _lastFont = "Segoe UI";
    private static bool _lastBold = true;
    private static double _lastHeight = 20;

    public static async Task ShowSketchTextAsync(DocumentView v)
    {
        var text = new TextBox { Header = "Text", Text = _lastText, Width = 300 };
        var fonts = new ComboBox { Header = "Font", Width = 300, IsEditable = false };
        string[] families;
        try { families = Microsoft.Graphics.Canvas.Text.CanvasTextFormat.GetSystemFontFamilies(); }
        catch { families = new[] { "Segoe UI", "Arial", "Bahnschrift", "Georgia", "Consolas", "Times New Roman" }; }
        foreach (var f in families.OrderBy(f => f, StringComparer.CurrentCultureIgnoreCase)) fonts.Items.Add(f);
        fonts.SelectedItem = families.Contains(_lastFont) ? _lastFont : fonts.Items.FirstOrDefault();
        var bold = new CheckBox { Content = "Bold", IsChecked = _lastBold };
        var italic = new CheckBox { Content = "Italic" };
        var height = new NumberBox { Header = "Height (mm)", Value = _lastHeight, Minimum = 0.1, SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Compact, Width = 160 };
        var content = Ui.VStack(10, Ui.Caption("Adds the text as closed outlines at the sketch origin, ready to extrude or emboss."),
            text, fonts, Ui.HStack(16, bold, italic), height);
        var dialog = new ContentDialog { Title = "Text", Content = content, PrimaryButtonText = "Add Text", CloseButtonText = "Cancel", DefaultButton = ContentDialogButton.Primary };
        text.TextChanged += (_, _) => dialog.IsPrimaryButtonEnabled = text.Text.Trim().Length > 0;
        if (await ShowAsync(v.XamlRoot, dialog) != ContentDialogResult.Primary) return;
        _lastText = text.Text;
        _lastFont = fonts.SelectedItem as string ?? "Segoe UI";
        _lastBold = bold.IsChecked == true;
        _lastHeight = double.IsNaN(height.Value) ? 20 : Math.Max(height.Value, 0.1);
        try
        {
            var path = TextOutlines.Build(_lastText, _lastFont, _lastBold, italic.IsChecked == true, _lastHeight);
            if (path.Count == 0)
            {
                v.ShowMessage("The text has no outlines");
                return;
            }
            v.Run("sketch.addOutlines", new JsonObject { ["path"] = path });
        }
        catch (Exception e)
        {
            AppSettings.Log("Sketch text: " + e);
            v.ShowMessage("Couldn't lay out the text: " + e.Message);
        }
        v.FocusViewport();
    }

    // ---- Export ----------------------------------------------------------------------

    /// <summary>File ▸ Export… (every format, favourites for Quick Export) or Export to Slicer….</summary>
    public static async Task ShowExportAsync(DocumentView v, bool slicer)
    {
        var state = v.Doc.State;
        var exportState = state.At("export");
        string? pendingExport = null;
        string? pendingSlicer = null;
        ContentDialog? dialog = null;
        var content = new StackPanel { Spacing = 10, Width = 520 };
        content.Children.Add(Ui.Caption(exportState.Str("scope"), false));

        if (!slicer)
        {
            var list = new StackPanel { Spacing = 4 };
            var favorites = new HashSet<string>(AppSettings.Current.QuickExportFormats);
            foreach (var kind in exportState.Arr("kinds"))
            {
                string id = kind.Str("id");
                bool available = kind.Bool("available"), enabled = kind.Bool("enabled");
                var star = new Microsoft.UI.Xaml.Controls.Primitives.ToggleButton
                {
                    Content = Icons.Create(favorites.Contains(id) ? "star.fill" : "star", 13, favorites.Contains(id) ? Ui.Caution : Ui.Secondary),
                    IsChecked = favorites.Contains(id),
                    IsEnabled = available,
                    Padding = new Thickness(6, 4, 6, 4),
                    Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent),
                    BorderThickness = new Thickness(0),
                };
                ToolTipService.SetToolTip(star, "Favourite: included in Quick Export");
                star.Click += (_, _) =>
                {
                    bool on = star.IsChecked == true;
                    if (on) favorites.Add(id); else favorites.Remove(id);
                    star.Content = Icons.Create(on ? "star.fill" : "star", 13, on ? Ui.Caution : Ui.Secondary);
                    AppSettings.Current.QuickExportFormats = exportState.Arr("kinds").Select(k => k.Str("id")).Where(favorites.Contains).ToList();
                    AppSettings.Current.Save();
                };
                var texts = Ui.VStack(1, Ui.Text($"{kind.Str("title")} (.{kind.Str("extension")})", 13, FontWeights.Medium),
                    Ui.Text(kind.Str("detail"), 12, null, Ui.Secondary, wrap: true));
                var export = Ui.Button("Export…", () =>
                {
                    pendingExport = id;
                    dialog?.Hide();
                }, enabled: available && enabled);
                var row = new Grid { ColumnSpacing = 10, Opacity = available ? 1 : 0.5 };
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
                row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                row.Children.Add(star);
                Grid.SetColumn(texts, 1);
                row.Children.Add(texts);
                Grid.SetColumn(export, 2);
                row.Children.Add(export);
                list.Children.Add(row);
            }
            content.Children.Add(new ScrollViewer { Content = list, MaxHeight = 380, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
            content.Children.Add(Ui.Caption("★ marks formats for Quick Export to Favorite Formats.", false));

            var folderText = Ui.Text(AppSettings.Current.QuickExportFolder, 12, null, Ui.Secondary);
            folderText.TextTrimming = TextTrimming.CharacterEllipsis;
            var choose = Ui.Button("Choose…", async () =>
            {
                var folder = await Pickers.PickFolderAsync(v.Main);
                if (folder == null) return;
                AppSettings.Current.QuickExportFolder = folder;
                AppSettings.Current.Save();
                folderText.Text = folder;
            });
            var now = Ui.Button("Export Favorites Now", () =>
            {
                QuickExport(v);
                dialog?.Hide();
            });
            content.Children.Add(Ui.Headline("Quick Export"));
            content.Children.Add(Ui.Row(Ui.HStack(6, Icons.Create("folder", 13), folderText), Ui.HStack(6, choose, now)));
        }
        else
        {
            var formats = new RadioButtons { Header = "Format" };
            formats.Items.Add("3MF (keeps colours and separate parts)");
            formats.Items.Add("STL");
            formats.Items.Add("OBJ");
            formats.SelectedIndex = 0;
            string Format() => formats.SelectedIndex switch { 1 => "stl", 2 => "obj", _ => "threeMF" };
            content.Children.Add(formats);
            var slicers = Slicers.Installed();
            if (slicers.Count == 0)
                content.Children.Add(Ui.Caption("No slicer found. Open with the default app for the format, choose an app, or install Bambu Studio, OrcaSlicer, PrusaSlicer or Cura."));
            foreach (var (name, exe) in slicers)
            {
                content.Children.Add(Ui.Row(Ui.Text(name, 13), Ui.Button($"Open in {name}", () =>
                {
                    pendingSlicer = Format() + "|" + exe;
                    dialog?.Hide();
                })));
            }
            content.Children.Add(Ui.HStack(8,
                Ui.Button("Open with Default App", () => { pendingSlicer = Format() + "|"; dialog?.Hide(); }),
                Ui.Button("Choose App…", () => { pendingSlicer = Format() + "|?"; dialog?.Hide(); })));
        }

        dialog = new ContentDialog { Title = slicer ? "Export to Slicer" : "Export", Content = content, CloseButtonText = "Done" };
        await ShowAsync(v.XamlRoot, dialog);
        if (pendingExport != null) await v.ExportKindAsync(pendingExport);
        if (pendingSlicer != null) OpenInSlicer(v, pendingSlicer);
    }

    public static void QuickExport(DocumentView v)
    {
        var formats = AppSettings.Current.QuickExportFormats;
        if (formats.Count == 0)
        {
            v.ShowMessage("Pick favourite formats in File ▸ Export… first");
            return;
        }
        var reply = v.Run("export.quick", new JsonObject
        {
            ["formats"] = new JsonArray(formats.Select(f => (JsonNode?)JsonValue.Create(f)).ToArray()),
            ["folder"] = AppSettings.Current.QuickExportFolder,
        });
        var first = reply.Arr("paths").FirstOrDefault()?.AsStr();
        if (!string.IsNullOrEmpty(first))
        {
            try { System.Diagnostics.Process.Start("explorer.exe", $"/select,\"{first}\""); }
            catch (Exception e) { AppSettings.Log("Explorer: " + e.Message); }
        }
    }

    private static void OpenInSlicer(DocumentView v, string request)
    {
        var parts = request.Split('|', 2);
        string format = parts[0], app = parts.Length > 1 ? parts[1] : "";
        string folder = Path.Combine(Path.GetTempPath(), "Sculpt3D Slicer");
        Directory.CreateDirectory(folder);
        var reply = v.Run("export.write", new JsonObject { ["kind"] = format, ["folder"] = folder, ["name"] = v.Doc.Name });
        string path = reply.Str("path");
        if (string.IsNullOrEmpty(path)) return;
        try
        {
            if (app == "?")
                System.Diagnostics.Process.Start("rundll32.exe", $"shell32.dll,OpenAs_RunDLL {path}");
            else if (app.Length > 0)
                System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(app) { UseShellExecute = false, ArgumentList = { path } });
            else
                DocumentView.OpenWithShell(path);
        }
        catch (Exception e)
        {
            v.ShowMessage("Couldn't open the slicer: " + e.Message);
        }
    }

    // ---- About and controls ---------------------------------------------------------------

    public static Task ShowAboutAsync(XamlRoot root)
    {
        var version = typeof(Dialogs).Assembly.GetName().Version?.ToString(3) ?? "1.0.0";
        string core;
        try { core = SculptNative.Version; } catch { core = "unavailable"; }
        var mark = Icons.Create("sculpt3d.project", 64);
        var content = Ui.VStack(10, mark, Ui.Text("Sculpt3D", 22, FontWeights.Bold),
            Ui.Caption($"Version {version} · core {core}"),
            Ui.Caption("A solid modeler on the OpenCascade B-rep kernel. Rendering through ANGLE (OpenGL ES on Direct3D 11)."));
        return ShowAsync(root, new ContentDialog { Title = "About Sculpt3D", Content = content, CloseButtonText = "OK" });
    }

    public static Task ShowControlsAsync(XamlRoot root)
    {
        string text =
            "Mouse: click a face or edge (Shift/Ctrl-click to add), double-click a body. Drag to orbit, right-drag to orbit, " +
            "middle-drag or Shift/Alt-drag to pan, wheel to zoom (towards the pointer).\n\n" +
            "Touchpad: two-finger scroll orbits, Shift+scroll pans, pinch zooms.\n\n" +
            "Touch and pen: tap a face or edge, double-tap a body; one finger orbits (draws while sketching), two fingers pan and pinch to zoom. " +
            "Pen hover highlights.\n\n" +
            "Sketching: click points, lines and circles to select them (except with the Line tool); the bar above the tools offers the " +
            "constraints that fit. Click a dimension to select it, double-click to type a value. Esc ends a line, then deselects, then leaves " +
            "the sketch; Del deletes the selection. Tool keys: L line, A arc, R rectangle, C circle, P polygon, S select, T trim, O offset; " +
            "Shift+letter for constraints.\n\n" +
            "Modeling keys: E extrude, V revolve, W sweep, F fillet, H shell, M move/rotate, N translate, K sketch on Top. " +
            "Ctrl+1…8 standard views, Ctrl+Shift+F zoom to fit, Ctrl+F searches every tool.";
        var t = Ui.Text(text, 13, null, null, wrap: true);
        t.MaxWidth = 520;
        return ShowAsync(root, new ContentDialog { Title = "Controls", Content = new ScrollViewer { Content = t, MaxHeight = 480 }, CloseButtonText = "OK" });
    }
}

/// <summary>Slicers installed in the usual places (Export to Slicer).</summary>
public static class Slicers
{
    public static List<(string Name, string Exe)> Installed()
    {
        var found = new List<(string, string)>();
        var roots = new[]
        {
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles),
            Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs"),
        }.Where(r => !string.IsNullOrEmpty(r)).Distinct().ToList();
        var candidates = new (string Name, string Dir, string Exe)[]
        {
            ("Bambu Studio", "Bambu Studio", "bambu-studio.exe"),
            ("OrcaSlicer", "OrcaSlicer", "orca-slicer.exe"),
            ("PrusaSlicer", Path.Combine("Prusa3D", "PrusaSlicer"), "prusa-slicer.exe"),
            ("Creality Print", "Creality Print", "CrealityPrint.exe"),
        };
        foreach (var (name, dir, exe) in candidates)
        {
            foreach (var root in roots)
            {
                var path = Path.Combine(root, dir, exe);
                if (File.Exists(path)) { found.Add((name, path)); break; }
            }
        }
        // UltiMaker Cura installs into a versioned folder.
        foreach (var root in roots)
        {
            try
            {
                foreach (var dir in Directory.EnumerateDirectories(root, "UltiMaker Cura*").Concat(Directory.EnumerateDirectories(root, "Ultimaker Cura*")))
                {
                    var exe = Path.Combine(dir, "UltiMaker-Cura.exe");
                    if (!File.Exists(exe)) exe = Path.Combine(dir, "Ultimaker-Cura.exe");
                    if (File.Exists(exe)) { found.Add(("Cura", exe)); goto doneCura; }
                }
            }
            catch (Exception)
            {
                // Unreadable folder: skip it.
            }
        }
    doneCura:
        return found;
    }
}
