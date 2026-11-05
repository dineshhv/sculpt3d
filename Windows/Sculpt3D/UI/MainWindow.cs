// The app window: menu bar (ModelCommands.swift + the MCP menu), documents in
// tabs, the welcome page, opening/saving with dirty tracking and save prompts.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Sculpt3D.Core;
using Windows.System;

namespace Sculpt3D.UI;

public sealed class MainWindow : Window
{
    public IntPtr Hwnd { get; }

    private readonly Grid _root = new();
    private readonly MenuBar _menuBar = new();
    private readonly TabView _tabs = new()
    {
        IsAddTabButtonVisible = true,
        TabWidthMode = TabViewWidthMode.SizeToContent,
        CanReorderTabs = true,
        CanDragTabs = false,
    };
    private readonly WelcomeView _welcome;
    private readonly List<Action> _menuUpdaters = new();
    private readonly List<DrawingWindow> _drawings = new();
    private MenuFlyoutSubItem? _recentMenu, _measureRecentMenu, _exportKindsMenu;
    private ToggleMenuFlyoutItem? _mcpToggle;
    private MenuFlyoutItem? _mcpStatus;
    private bool _closeConfirmed;
    private string _menuSignature = "";

    public MainWindow()
    {
        Title = "Sculpt3D";
        Hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
        try { AppWindow.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "Sculpt3D.ico")); } catch { }
        var settings = AppSettings.Current;
        AppWindow.Resize(new Windows.Graphics.SizeInt32((int)Math.Max(900, settings.WindowWidth), (int)Math.Max(600, settings.WindowHeight)));
        SystemBackdrop = new MicaBackdrop();

        _welcome = new WelcomeView(this);
        _root.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        _root.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        _root.Children.Add(_menuBar);
        var host = new Grid();
        host.Children.Add(_tabs);
        host.Children.Add(_welcome);
        Grid.SetRow(host, 1);
        _root.Children.Add(host);
        Content = _root;

        _tabs.AddTabButtonClick += (_, _) => NewDocument();
        _tabs.TabCloseRequested += async (_, e) => await CloseTabAsync(e.Tab);
        _tabs.SelectionChanged += (_, _) => OnActiveChanged();

        BuildMenus();
        UpdateContent();

        AppWindow.Closing += OnClosing;
        Closed += (_, _) =>
        {
            foreach (var d in _drawings.ToArray()) d.Close();
            foreach (var view in Views.ToArray())
            {
                view.Close();
                view.Doc.Dispose();
            }
            McpService.StatusChanged -= UpdateMenus;
            McpService.Stop();
            var size = AppWindow.Size;
            AppSettings.Current.WindowWidth = size.Width;
            AppSettings.Current.WindowHeight = size.Height;
            AppSettings.Current.Save();
        };
        McpService.StatusChanged += UpdateMenus;
    }

    // ---- Documents ---------------------------------------------------------------------

    private IEnumerable<DocumentView> Views => _tabs.TabItems.OfType<TabViewItem>().Select(t => t.Tag).OfType<DocumentView>();

    public DocumentView? Active => (_tabs.SelectedItem as TabViewItem)?.Tag as DocumentView;

    public DocumentView NewDocument(string name = "Untitled")
    {
        var doc = new SculptDocument(DispatcherQueue, name);
        ApplyDisplayFormats(doc);
        doc.RegisterMcp();
        return AddTab(doc);
    }

    private DocumentView AddTab(SculptDocument doc)
    {
        var view = new DocumentView(this, doc);
        var tab = new TabViewItem
        {
            Header = doc.Name,
            Content = view,
            Tag = view,
            IconSource = new FontIconSource { Glyph = "" },
        };
        view.Changed += () => OnViewChanged(view, tab);
        _tabs.TabItems.Add(tab);
        _tabs.SelectedItem = tab;
        UpdateContent();
        return view;
    }

    private void OnViewChanged(DocumentView view, TabViewItem tab)
    {
        bool dirty = view.Doc.IsDirty;
        string header = view.Doc.Name + (dirty ? " •" : "");
        if (tab.Header as string != header) tab.Header = header;
        if (view == Active)
        {
            UpdateTitle();
            UpdateMenus();
        }
    }

    private void OnActiveChanged()
    {
        Active?.Doc.ActivateMcp();
        UpdateTitle();
        UpdateMenus();
        Active?.FocusViewport();
    }

    private void UpdateContent()
    {
        bool any = _tabs.TabItems.Count > 0;
        _tabs.Visibility = any ? Visibility.Visible : Visibility.Collapsed;
        _welcome.Visibility = any ? Visibility.Collapsed : Visibility.Visible;
        if (!any) _welcome.Reload();
        UpdateTitle();
        UpdateMenus();
    }

    private void UpdateTitle()
    {
        var a = Active;
        Title = a == null ? "Sculpt3D" : $"{a.Doc.Name}{(a.Doc.IsDirty ? " •" : "")} — Sculpt3D";
    }

    public async Task OpenAsync()
    {
        var paths = await Pickers.OpenFilesAsync(this, Pickers.ProjectFiles, multiple: true);
        foreach (var p in paths) OpenPath(p);
    }

    /// <summary>Opens a .sculpt3d (or legacy .o3d) file in a tab, or selects it when open.</summary>
    public void OpenPath(string path)
    {
        try { path = Path.GetFullPath(path); } catch { }
        var existing = _tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t =>
            t.Tag is DocumentView v && string.Equals(v.Doc.FilePath, path, StringComparison.OrdinalIgnoreCase));
        if (existing != null)
        {
            _tabs.SelectedItem = existing;
            return;
        }
        var doc = new SculptDocument(DispatcherQueue, Path.GetFileNameWithoutExtension(path));
        var error = doc.Load(path);
        if (error != null)
        {
            doc.Dispose();
            _ = Dialogs.MessageAsync(_root.XamlRoot, "Couldn't open the document", $"{Path.GetFileName(path)}: {error}");
            return;
        }
        ApplyDisplayFormats(doc);
        doc.RegisterMcp();
        // A pristine untitled document is replaced, like the Mac's first window.
        var pristine = _tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t =>
            t.Tag is DocumentView v && v.Doc.FilePath == null && !v.Doc.IsDirty && v.Doc.State.Count("features") == 0);
        AddTab(doc);
        if (pristine != null) RemoveTab(pristine);
        AppSettings.Current.AddRecent(path);
        UpdateMenus();
    }

    public async Task<bool> SaveAsync(DocumentView view, bool saveAs = false)
    {
        string? path = view.Doc.FilePath;
        if (saveAs || path == null || path.EndsWith(".o3d", StringComparison.OrdinalIgnoreCase))
        {
            path = await Pickers.SaveFileAsync(this, view.Doc.Name + ".sculpt3d", "Sculpt3D project", ".sculpt3d");
            if (path == null) return false;
        }
        var error = view.Doc.Save(path);
        if (error != null)
        {
            await Dialogs.MessageAsync(_root.XamlRoot, "Couldn't save the document", error);
            return false;
        }
        AppSettings.Current.AddRecent(path);
        view.Refresh();
        return true;
    }

    /// <summary>Asks to save a dirty document. False when the user cancelled.</summary>
    private async Task<bool> ConfirmDiscardAsync(DocumentView view)
    {
        if (!view.Doc.IsDirty) return true;
        var tab = _tabs.TabItems.OfType<TabViewItem>().FirstOrDefault(t => ReferenceEquals(t.Tag, view));
        if (tab != null) _tabs.SelectedItem = tab;
        var result = await Dialogs.AskSaveAsync(_root.XamlRoot, view.Doc.Name);
        return result switch
        {
            ContentDialogResult.Primary => await SaveAsync(view),
            ContentDialogResult.Secondary => true,
            _ => false,
        };
    }

    private async Task CloseTabAsync(TabViewItem tab)
    {
        if (tab.Tag is not DocumentView view) return;
        if (!await ConfirmDiscardAsync(view)) return;
        RemoveTab(tab);
    }

    private void RemoveTab(TabViewItem tab)
    {
        if (tab.Tag is DocumentView view)
        {
            foreach (var d in _drawings.Where(d => d.Owner == view).ToArray()) d.Close();
            view.Close();
            view.Doc.Dispose();
        }
        _tabs.TabItems.Remove(tab);
        UpdateContent();
    }

    private void OnClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_closeConfirmed) return;
        args.Cancel = true;
        _ = ExitAsync();
    }

    /// <summary>Asks to save every dirty document, then closes the window (and the app).</summary>
    public async Task ExitAsync()
    {
        if (Dialogs.IsOpen) return;
        foreach (var view in Views.ToArray())
            if (!await ConfirmDiscardAsync(view)) return;
        _closeConfirmed = true;
        Close();
    }

    public void ShowDrawing(DocumentView view)
    {
        var existing = _drawings.FirstOrDefault(d => d.Owner == view);
        if (existing != null)
        {
            existing.Activate();
            return;
        }
        var w = new DrawingWindow(view);
        _drawings.Add(w);
        w.Closed += (_, _) => _drawings.Remove(w);
        w.Activate();
    }

    // ---- Menus -----------------------------------------------------------------------

    private static readonly JsonObject NoState = new();
    private JsonNode St => Active?.Doc.State ?? NoState;
    private bool HasDoc => Active != null;
    private bool Idle => HasDoc && St.Bool("idle");
    private bool Act(string id) => Active?.Doc.ActionEnabled(id) ?? false;
    private void R(string cmd, JsonObject? args = null) => Active?.Run(cmd, args);
    private void Tool(string id) => Active?.RunTool(id);

    private bool ToolEnabled(string id)
    {
        foreach (var c in St.Arr("tools"))
            foreach (var t in c.Arr("tools"))
                if (t.Str("id") == id) return t.Bool("enabled");
        return false;
    }

    private string ToolTitle(string id, string fallback)
    {
        foreach (var c in St.Arr("tools"))
            foreach (var t in c.Arr("tools"))
                if (t.Str("id") == id) return t.Str("title", fallback);
        return fallback;
    }

    /// <summary>The focused text box, if typing: Ctrl+Z/Y/A then act on its text.</summary>
    private TextBox? FocusedTextBox() =>
        _root.XamlRoot == null ? null : FocusManager.GetFocusedElement(_root.XamlRoot) as TextBox;

    /// <summary>"Ctrl+Shift+Z;Ctrl+Y" → accelerators; "!E" → shown only (the
    /// viewport sends plain letters to the core).</summary>
    private static void SetKeys(MenuFlyoutItem item, string? keys)
    {
        if (string.IsNullOrEmpty(keys)) return;
        if (keys.StartsWith('!'))
        {
            item.KeyboardAcceleratorTextOverride = keys.Substring(1);
            return;
        }
        foreach (var combo in keys.Split(';'))
        {
            var mods = VirtualKeyModifiers.None;
            VirtualKey? key = null;
            foreach (var part in combo.Split('+'))
            {
                switch (part)
                {
                    case "Ctrl": mods |= VirtualKeyModifiers.Control; break;
                    case "Shift": mods |= VirtualKeyModifiers.Shift; break;
                    case "Alt": mods |= VirtualKeyModifiers.Menu; break;
                    default: key = ParseKey(part); break;
                }
            }
            if (key.HasValue) item.KeyboardAccelerators.Add(new KeyboardAccelerator { Key = key.Value, Modifiers = mods });
        }
    }

    private static VirtualKey? ParseKey(string k)
    {
        if (k.Length == 1 && char.IsLetter(k[0])) return VirtualKey.A + (char.ToUpperInvariant(k[0]) - 'A');
        if (k.Length == 1 && char.IsDigit(k[0])) return VirtualKey.Number0 + (k[0] - '0');
        return k switch
        {
            "Delete" => VirtualKey.Delete,
            "F4" => VirtualKey.F4,
            "Escape" => VirtualKey.Escape,
            _ => null,
        };
    }

    private MenuFlyoutItem Item(string text, Action run, Func<bool>? enabled = null, string? keys = null)
    {
        var item = new MenuFlyoutItem { Text = text };
        item.Click += (_, _) =>
        {
            try { run(); }
            catch (Exception e) { AppSettings.Log($"Menu {text}: {e}"); }
        };
        SetKeys(item, keys);
        if (enabled != null) _menuUpdaters.Add(() => item.IsEnabled = enabled());
        return item;
    }

    private MenuFlyoutItem Item(Func<string> text, Action run, Func<bool>? enabled = null, string? keys = null)
    {
        var item = Item(text(), run, enabled, keys);
        _menuUpdaters.Add(() => item.Text = text());
        return item;
    }

    private ToggleMenuFlyoutItem Toggle(string text, Func<bool> isOn, Action<bool> set, Func<bool>? enabled = null, string? keys = null)
    {
        var item = new ToggleMenuFlyoutItem { Text = text };
        item.Click += (_, _) => set(item.IsChecked);
        SetKeys(item, keys);
        _menuUpdaters.Add(() =>
        {
            item.IsChecked = isOn();
            item.IsEnabled = enabled?.Invoke() ?? HasDoc;
        });
        return item;
    }

    private RadioMenuFlyoutItem Radio(string text, string group, Func<bool> isOn, Action run, Func<bool>? enabled = null, string? keys = null)
    {
        var item = new RadioMenuFlyoutItem { Text = text, GroupName = group };
        item.Click += (_, _) => run();
        SetKeys(item, keys);
        _menuUpdaters.Add(() =>
        {
            item.IsChecked = isOn();
            item.IsEnabled = enabled?.Invoke() ?? HasDoc;
        });
        return item;
    }

    private MenuFlyoutSubItem Sub(string text, Func<bool>? enabled, params MenuFlyoutItemBase[] items)
    {
        var sub = new MenuFlyoutSubItem { Text = text };
        foreach (var i in items) sub.Items.Add(i);
        if (enabled != null) _menuUpdaters.Add(() => sub.IsEnabled = enabled());
        return sub;
    }

    private static MenuFlyoutSeparator Sep() => new();

    private MenuBarItem Menu(string title, params MenuFlyoutItemBase[] items)
    {
        var m = new MenuBarItem { Title = title };
        foreach (var i in items) m.Items.Add(i);
        _menuBar.Items.Add(m);
        return m;
    }

    private MenuFlyoutItem ToolItem(string id, string? title = null, string? keys = null) =>
        Item(title ?? ToolTitle(id, id), () => Tool(id), () => ToolEnabled(id), keys);

    private MenuFlyoutItem ActionItem(string title, string action, string? keys = null) =>
        Item(title, () => R(action), () => Act(action), keys);

    private MenuFlyoutItem SketchTool(string title, string tool, string? keys = null, int? sides = null, bool? circumscribed = null)
    {
        return Item(title, () =>
        {
            var args = new JsonObject { ["tool"] = tool };
            if (sides.HasValue) args["sides"] = sides.Value;
            if (circumscribed.HasValue) args["circumscribed"] = circumscribed.Value;
            R("sketch.setTool", args);
        }, () => HasDoc && St.At("draft") == null, keys);
    }

    private void Look(string preset) => R("view.look", new JsonObject { ["preset"] = preset });

    private void BuildMenus()
    {
        // File
        _recentMenu = Sub("Open Recent", () => AppSettings.Current.ExistingRecentFiles().Any());
        _exportKindsMenu = new MenuFlyoutSubItem { Text = "Export to" };
        Menu("File",
            Item("New Project", () => NewDocument(), null, "Ctrl+N"),
            Item("Open…", () => _ = OpenAsync(), null, "Ctrl+O"),
            _recentMenu,
            Sep(),
            Item("Save", () => { if (Active != null) _ = SaveAsync(Active); }, () => HasDoc, "Ctrl+S"),
            Item("Save As…", () => { if (Active != null) _ = SaveAsync(Active, saveAs: true); }, () => HasDoc, "Ctrl+Shift+S"),
            Item("Close", () => { if (_tabs.SelectedItem is TabViewItem t) _ = CloseTabAsync(t); }, () => HasDoc, "Ctrl+W"),
            Sep(),
            Item("New Drawing", () => { if (Active != null) ShowDrawing(Active); }, () => HasDoc),
            Sep(),
            ActionItem("Import File…", "file.importFile", "Ctrl+Shift+I"),
            ActionItem("Import Project…", "file.importProject", "Ctrl+Shift+P"),
            ActionItem("Import Material…", "file.importMaterial"),
            ActionItem("Import Image as Decal…", "file.importDecal"),
            Sep(),
            Item("View in Browser", () => _ = Active?.WebPageAsync(false), () => HasDoc),
            Item("Copy Link", () => Active?.CopyLink(), () => Active?.Doc.FilePath != null),
            Item("Publish Current Version…", () => _ = Active?.WebPageAsync(true), () => HasDoc),
            Sep(),
            Item("Export…", () => { if (Active != null) _ = Dialogs.ShowExportAsync(Active, false); }, () => HasDoc, "Ctrl+Shift+E"),
            _exportKindsMenu,
            Sep(),
            Item("Exit", () => _ = ExitAsync(), null, "!Alt+F4"));
        _menuUpdaters.Add(() => _exportKindsMenu.IsEnabled = HasDoc);

        // Edit
        Menu("Edit",
            Item("Undo", () => { var t = FocusedTextBox(); if (t != null) t.Undo(); else R("edit.undo"); },
                () => Act("edit.undo") || FocusedTextBox() != null, "Ctrl+Z"),
            Item("Redo", () => { var t = FocusedTextBox(); if (t != null) t.Redo(); else R("edit.redo"); },
                () => Act("edit.redo") || FocusedTextBox() != null, "Ctrl+Shift+Z;Ctrl+Y"),
            Sep(),
            ActionItem("Duplicate", "edit.duplicate", "Ctrl+D"),
            Item("Delete", () => R("edit.delete"), () => Act("edit.delete"), "!Del"),
            Item("Select All Bodies", () => { var t = FocusedTextBox(); if (t != null) t.SelectAll(); else R("edit.selectAll"); },
                () => Act("edit.selectAll") || FocusedTextBox() != null, "Ctrl+A"),
            ActionItem("Deselect All", "edit.deselectAll", "Ctrl+Shift+A"),
            Sep(),
            Item("Search Tools…", () => Active?.SetShowSearch(true), () => HasDoc, "Ctrl+F"),
            Item("Variables…", () => { if (Active != null) _ = Dialogs.ShowVariablesAsync(Active); }, () => HasDoc));

        // Item
        Menu("Item",
            ActionItem("Show/Hide", "item.toggleVisibilityOfSelection", "Ctrl+Shift+H"),
            Sep(),
            ActionItem("Isolate", "item.isolate"),
            ActionItem("Exit Isolation", "item.exitIsolation"),
            Sep(),
            ActionItem("Zoom to", "view.zoomToSelection"),
            Item("Reveal in Items Sidebar", () => Active?.SetShowItems(true),
                () => St.At("selection").Count("picks") > 0 || St.At("selection").Count("items") > 0, "Alt+Shift+I"),
            Sep(),
            ActionItem("Show Hidden Bodies", "item.showAll", "Alt+H"),
            ActionItem("Invert Body Visibility", "item.invertVisibility", "Alt+Shift+H"));

        // Sketch
        Func<bool> inSketch = () => St.At("sketch") != null;
        Func<bool> canTransform = () => St.At("sketch").Bool("canTransform");
        var polygonItems = new List<MenuFlyoutItemBase>
        {
            SketchTool("Inscribed Polygon", "polygon", circumscribed: false),
            SketchTool("Circumscribed Polygon", "polygon", circumscribed: true),
            Sep(),
        };
        foreach (int n in new[] { 3, 4, 5, 6, 8, 10, 12 }) polygonItems.Add(SketchTool($"{n} Sides", "polygon", sides: n));
        Menu("Sketch",
            SketchTool("Line", "line", "!L"),
            SketchTool("Arc", "arc", "!A"),
            Sub("Spline", () => HasDoc, SketchTool("Fit Point Spline", "spline"), SketchTool("Control Point Spline", "controlSpline")),
            Sub("Rectangle", () => HasDoc, SketchTool("2-Point Rectangle", "rectangle", "!R"), SketchTool("Center Rectangle", "centerRectangle")),
            SketchTool("Circle", "circle", "!C"),
            SketchTool("Ellipse", "ellipse"),
            Sub("Polygon", () => HasDoc, polygonItems.ToArray()),
            SketchTool("Point", "point"),
            Sep(),
            Item("Project as Sketch", () => R("sketch.project"), inSketch),
            ActionItem("Offset Edge", "sketch.offset", "!O"),
            ActionItem("Fillet Sketch", "sketch.fillet"),
            Item("Text…", () => R("sketch.text"), inSketch),
            Sep(),
            Item("Scale Sketch", () => { if (Active != null) _ = Dialogs.ShowSketchTransformAsync(Active, "scale"); }, canTransform),
            Item("Pattern - Linear", () => { if (Active != null) _ = Dialogs.ShowSketchTransformAsync(Active, "linearPattern"); }, canTransform),
            Item("Pattern - Circular", () => { if (Active != null) _ = Dialogs.ShowSketchTransformAsync(Active, "circularPattern"); }, canTransform),
            Item("Rotate Sketch Around Axis", () => { if (Active != null) _ = Dialogs.ShowSketchTransformAsync(Active, "rotate"); }, canTransform),
            ActionItem("Mirror Sketch", "sketch.mirror"),
            Sep(),
            SketchTool("Trim", "trim", "!T"),
            Toggle("Show Constraints", () => St.At("view").Bool("showConstraints"),
                on => R("sketch.setShowConstraints", new JsonObject { ["on"] = on }), inSketch),
            Toggle("Construction Mode", () => St.At("sketch").Bool("constructionMode"),
                on => R("sketch.setConstructionMode", new JsonObject { ["on"] = on }), inSketch),
            Sep(),
            Item("Finish Sketch", () => R("sketch.finishAsItem"), inSketch),
            Item("Cancel Sketch", () => R("sketch.cancel"), inSketch));

        // Insert
        Menu("Insert",
            ToolItem("insert.variable", "Variable…", "Ctrl+Shift+V"),
            ToolItem("insert.image", "Image…"),
            ToolItem("insert.import", "File…"),
            ToolItem("insert.project", "Import Project…"),
            Sep(),
            Sub("Primitive", () => Idle,
                ToolItem("insert.box", "Box"), ToolItem("insert.cylinder", "Cylinder"), ToolItem("insert.sphere", "Sphere"),
                ToolItem("insert.cone", "Cone"), ToolItem("insert.torus", "Torus")),
            Sub("Terrain", () => Idle,
                ActionItem("Rolling Hills", "model.rollingHills"),
                ActionItem("From Height Map…", "file.importHeightMap")));

        // Construct
        Menu("Construct",
            Sub("Plane", () => HasDoc,
                ToolItem("construct.offset", "Offset Plane"),
                ActionItem("Midplane", "model.midplane"),
                ActionItem("Plane Through 3 Points", "pick.planeThrough3Points"),
                ActionItem("Plane on Curve at Point", "pick.planeOnCurveAtPoint"),
                ActionItem("Plane Perpendicular to Edge at Point", "pick.planePerpendicularToEdgeAtPoint"),
                ActionItem("Plane Through Edge at Angle", "model.planeThroughEdge"),
                ActionItem("Plane Parallel to Face at Point", "pick.planeParallelToFaceAtPoint"),
                Sep(),
                ToolItem("construct.angled", "Angled Plane")),
            Sub("Axis", () => HasDoc,
                ActionItem("Axis Through 2 Points", "pick.axisThrough2Points"),
                ActionItem("Axis Through 2 Planes", "model.axisThroughTwoPlanes"),
                ActionItem("Axis of Cylinder/Cone", "model.axisOfRoundFace"),
                ActionItem("Axis Along Edge", "model.axisAlongEdge"),
                ActionItem("Axis Perpendicular to Face at Point", "pick.axisPerpendicularToFaceAtPoint")));

        // Transform
        Menu("Transform",
            ToolItem("transform.move", "Move/Rotate", "!M"),
            ToolItem("transform.scale", "Scale"),
            ToolItem("transform.translate", "Translate", "!N"),
            Sub("Pattern", () => HasDoc,
                ToolItem("transform.linear", "Pattern - Linear"),
                ToolItem("transform.circular", "Pattern - Circular"),
                ToolItem("transform.scatter", "Scatter")),
            ToolItem("transform.rotate", "Rotate Around Axis"),
            ToolItem("transform.align", "Align"),
            ToolItem("transform.mirror", "Mirror"));

        // Tools
        _measureRecentMenu = Sub("Recent", () => HasDoc);
        Menu("Tools",
            ToolItem("tools.offset", "Offset Face"),
            ToolItem("tools.fillet", "Fillet", "!F"),
            ToolItem("tools.chamfer", "Chamfer"),
            ToolItem("tools.extrude", "Extrude", "!E"),
            ToolItem("tools.shell", "Shell", "!H"),
            ToolItem("tools.loft", "Loft"),
            Sep(),
            ToolItem("tools.union", "Union", "Ctrl+U"),
            ToolItem("tools.subtract", "Subtract", "Ctrl+B"),
            ToolItem("tools.intersect", "Intersect", "Ctrl+I"),
            Sep(),
            ToolItem("tools.split", "Split Body"),
            ToolItem("tools.revolve", "Revolve", "!V"),
            ToolItem("tools.sweep", "Sweep", "!W"),
            ToolItem("tools.fill", "Fill (Beta)"),
            ToolItem("tools.replaceFace", "Replace Face"),
            ToolItem("tools.project", "Project as Edge"),
            ActionItem("Wrap & Emboss", "model.emboss"),
            Sep(),
            Item(() => St.At("view").Bool("visualizing") ? "Exit Visualize" : "Visualize",
                () => R(St.At("view").Bool("visualizing") ? "visualize.exit" : "visualize.enter"),
                () => HasDoc && St.At("sketch") == null),
            Sep(),
            Item(() => St.At("view").At("section").Bool("enabled") ? "Hide Section View" : "Section View",
                () => R("view.toggleSection"), () => HasDoc, "Ctrl+Alt+S"),
            Item("Edit Section View", () => R("view.section", new JsonObject { ["enabled"] = true }),
                () => St.At("view").At("section").Bool("enabled")),
            ActionItem("2D Section View", "view.lookAtSection"),
            Sep(),
            Sub("Measure", () => HasDoc,
                _measureRecentMenu,
                Sep(),
                ActionItem("Object Measurements", "measure.objects"),
                Toggle("Measure Mode", () => St.At("view").Bool("measuring"),
                    on => R("view.setMeasuring", new JsonObject { ["on"] = on }), () => HasDoc, "Ctrl+Alt+M"),
                ActionItem("Point-to-Point Distance", "pick.measurePointToPoint"),
                ActionItem("3-Point Angle", "pick.measureThreePointAngle")),
            Sep(),
            ActionItem("Sketch", "model.sketch"));

        // View
        var displayItems = new List<MenuFlyoutItemBase>();
        foreach (var (id, title, key) in new[]
                 {
                     ("wireframe", "Wireframe", "W"), ("xray", "X-Ray", "X"), ("shaded", "Shaded", "S"),
                     ("visualized", "Visualized", "V"), ("zebra", "Zebra Analysis", "Z"), ("curvature", "Curvature Map", "C"),
                 })
        {
            displayItems.Add(Radio(title, "display", () => St.At("view").Str("displayMode") == id,
                () => R("view.setDisplayMode", new JsonObject { ["mode"] = id }), null, "Alt+" + key));
        }
        var unitItems = new List<MenuFlyoutItemBase>();
        foreach (var (id, title) in new[] { ("mm", "Millimeters"), ("cm", "Centimeters"), ("m", "Meters"), ("inch", "Inches"), ("ft", "Feet") })
            unitItems.Add(Radio(title, "units", () => St.At("settings").Str("units") == id,
                () => R("settings.setUnits", new JsonObject { ["units"] = id })));
        Menu("View",
            new MenuFlyoutItemBase[]
            {
                Toggle("Show History Sidebar", () => Active?.ShowHistory ?? false, on => Active?.SetShowHistory(on), null, "Ctrl+Alt+H"),
                Toggle("Show Items Sidebar", () => Active?.ShowItems ?? false, on => Active?.SetShowItems(on), null, "Ctrl+Alt+I"),
                Toggle("Show Properties Sidebar", () => Active?.ShowProperties ?? false, on => Active?.SetShowProperties(on), null, "Ctrl+Alt+P"),
                Sep(),
                Item("Material Properties", () => Active?.SetVisualizeTab(0), () => St.At("view").Bool("visualizing")),
                Item("Environment Properties", () => Active?.SetVisualizeTab(1), () => St.At("view").Bool("visualizing")),
                Item("Camera Properties", () => Active?.SetVisualizeTab(2), () => St.At("view").Bool("visualizing")),
                Item("Drawing Properties", () => { if (Active != null) ShowDrawing(Active); }, () => HasDoc),
                Sep(),
                Item("Collapse All", () => Active?.CollapseAll(), () => HasDoc, "Alt+L"),
                Sep(),
                Sub("Grid Position", () => HasDoc,
                    Radio("At Origin", "gridpos", () => !St.At("view").Bool("gridAtModelBottom"),
                        () => R("view.set", new JsonObject { ["gridAtModelBottom"] = false })),
                    Radio("Under the Model", "gridpos", () => St.At("view").Bool("gridAtModelBottom"),
                        () => R("view.set", new JsonObject { ["gridAtModelBottom"] = true }))),
                Toggle("Show Grid", () => St.At("settings").At("environment").Bool("showGrid"),
                    on => R("settings.setEnvironment", new JsonObject { ["showGrid"] = on })),
                Toggle("Show World Axes", () => St.At("view").Bool("showWorldAxes"), on => R("view.set", new JsonObject { ["showWorldAxes"] = on })),
                Sep(),
            }.Concat(displayItems).Concat(new MenuFlyoutItemBase[]
            {
                Sep(),
                Toggle("Show Edges", () => St.At("view").Bool("showEdges"), on => R("view.set", new JsonObject { ["showEdges"] = on })),
                Toggle("Show Sketches", () => St.At("view").Bool("showSketches"), on => R("view.set", new JsonObject { ["showSketches"] = on })),
                Toggle("Show Hidden Edges", () => St.At("view").Bool("showHiddenEdges"), on => R("view.set", new JsonObject { ["showHiddenEdges"] = on })),
                Toggle("Show Decals", () => St.At("view").Bool("showDecals"), on => R("view.set", new JsonObject { ["showDecals"] = on })),
                Sep(),
                Toggle("Show Pinned Measurements", () => St.At("view").Bool("showPinnedMeasurements"),
                    on => R("view.set", new JsonObject { ["showPinnedMeasurements"] = on })),
                Sep(),
                ActionItem("Go to Modeling", "view.goToModeling"),
                Sep(),
                Sub("Units", () => HasDoc, unitItems.ToArray()),
                Sub("Foot & Inch Format", () => St.At("displayFormat").Bool("footInchEnabled"),
                    Radio("Architectural", "footinch", () => St.At("displayFormat").Str("footInch") == "architectural", () => SetFormat("footInch", "architectural")),
                    Radio("Decimal", "footinch", () => St.At("displayFormat").Str("footInch") == "decimal", () => SetFormat("footInch", "decimal"))),
                Sub("Degree Format", () => HasDoc,
                    Radio("Decimal", "degrees", () => St.At("displayFormat").Str("degrees") == "decimal", () => SetFormat("degrees", "decimal")),
                    Radio("Degrees, Minutes, Seconds", "degrees", () => St.At("displayFormat").Str("degrees") == "degreesMinutesSeconds",
                        () => SetFormat("degrees", "degreesMinutesSeconds"))),
                ToggleWithTitle(() => St.At("view").Str("lockGridTitle", "Lock Grid Size"), () => St.At("view").At("fixedGrid") != null,
                    on => R("view.setGridLock", new JsonObject { ["on"] = on })),
                Item("Tip: Adjust grid size by zooming", () => { }, () => false),
            }).ToArray());

        // Camera
        Menu("Camera",
            Item("Default View", () => Look("iso"), () => HasDoc, "Ctrl+1"),
            Item("Front", () => Look("front"), () => HasDoc, "Ctrl+2"),
            Item("Back", () => Look("back"), () => HasDoc, "Ctrl+3"),
            Item("Top", () => Look("top"), () => HasDoc, "Ctrl+4"),
            Item("Bottom", () => Look("bottom"), () => HasDoc, "Ctrl+5"),
            Item("Right", () => Look("right"), () => HasDoc, "Ctrl+6"),
            Item("Left", () => Look("left"), () => HasDoc, "Ctrl+7"),
            Item("Nearest Ortho View", () => R("view.snapToNearestOrtho"), () => HasDoc, "Ctrl+8"),
            Sub("Rotate View", () => HasDoc,
                Item("Rotate Left 90°", () => R("view.rotate", new JsonObject { ["yaw"] = -1 })),
                Item("Rotate Right 90°", () => R("view.rotate", new JsonObject { ["yaw"] = 1 })),
                Item("Rotate Up 90°", () => R("view.rotate", new JsonObject { ["pitch"] = 1 })),
                Item("Rotate Down 90°", () => R("view.rotate", new JsonObject { ["pitch"] = -1 }))),
            Item("Zoom to Fit", () => R("view.fit"), () => HasDoc, "Ctrl+Shift+F"),
            Sep(),
            Toggle("Orthographic", () => St.At("settings").At("camera").Bool("orthographic"),
                on => R("settings.setCamera", new JsonObject { ["orthographic"] = on })),
            Item("Field of View…", () => { if (Active != null) _ = Dialogs.ShowFieldOfViewAsync(Active); }, () => HasDoc));

        // MCP server
        _mcpToggle = new ToggleMenuFlyoutItem { Text = "MCP Server" };
        _mcpToggle.Click += (_, _) => McpService.SetEnabled(_mcpToggle.IsChecked);
        _mcpStatus = new MenuFlyoutItem { Text = McpService.Status, IsEnabled = false };
        Menu("MCP",
            _mcpToggle,
            _mcpStatus,
            Item("Copy Claude Code Setup Command", () =>
            {
                var package = new Windows.ApplicationModel.DataTransfer.DataPackage();
                package.SetText(McpService.SetupCommand);
                Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
                Active?.ShowMessage("Copied: " + McpService.SetupCommand);
            }, () => McpService.Running));
        _menuUpdaters.Add(() =>
        {
            _mcpToggle.IsChecked = AppSettings.Current.McpEnabled;
            _mcpStatus.Text = McpService.Status;
        });

        // Help
        Menu("Help",
            Item("Controls", () => _ = Dialogs.ShowControlsAsync(_root.XamlRoot)),
            Item("Export Formats…", () => { if (Active != null) _ = Dialogs.ShowExportAsync(Active, false); }, () => HasDoc),
            Sep(),
            Item("About Sculpt3D", () => _ = Dialogs.ShowAboutAsync(_root.XamlRoot)));
    }

    private ToggleMenuFlyoutItem ToggleWithTitle(Func<string> title, Func<bool> isOn, Action<bool> set)
    {
        var item = Toggle(title(), isOn, set);
        _menuUpdaters.Add(() => item.Text = title());
        return item;
    }

    private void SetFormat(string key, string value)
    {
        R("format.set", new JsonObject { [key] = value });
        if (key == "footInch") AppSettings.Current.FootInchFormat = value; else AppSettings.Current.DegreeFormat = value;
        AppSettings.Current.Save();
        foreach (var v in Views) v.Doc.RefreshState(force: true);
    }

    /// <summary>Restores the app-wide display formats into a new document's core state.</summary>
    public static void ApplyDisplayFormats(SculptDocument doc)
    {
        var args = new JsonObject();
        if (!string.IsNullOrEmpty(AppSettings.Current.FootInchFormat)) args["footInch"] = AppSettings.Current.FootInchFormat;
        if (!string.IsNullOrEmpty(AppSettings.Current.DegreeFormat)) args["degrees"] = AppSettings.Current.DegreeFormat;
        if (args.Count > 0) doc.Command("format.set", args);
    }

    /// <summary>Updates enabled/checked states and the dynamic menus.</summary>
    public void UpdateMenus()
    {
        foreach (var u in _menuUpdaters) u();

        // Dynamic submenus only rebuild when what they list changed.
        var recent = AppSettings.Current.ExistingRecentFiles().ToList();
        var measureRecent = St.At("measure").Arr("recent").Select(r => r.AsStr()).ToList();
        var kinds = St.At("export").At("kinds");
        string sig = string.Join("\n", recent) + "|" + string.Join("\n", measureRecent) + "|" + kinds.Sig() + "|" + HasDoc + "|" +
                     Act("measure.clear") + St.At("export").Int("bodyCount") + (St.At("sketch") == null) + AppSettings.Current.QuickExportFormats.Count;
        if (sig == _menuSignature) return;
        _menuSignature = sig;

        if (_recentMenu != null)
        {
            _recentMenu.Items.Clear();
            foreach (var path in recent)
            {
                string p = path;
                var item = new MenuFlyoutItem { Text = Path.GetFileName(p) };
                ToolTipService.SetToolTip(item, p);
                item.Click += (_, _) => OpenPath(p);
                _recentMenu.Items.Add(item);
            }
            if (recent.Count > 0)
            {
                _recentMenu.Items.Add(new MenuFlyoutSeparator());
                var clear = new MenuFlyoutItem { Text = "Clear Menu" };
                clear.Click += (_, _) =>
                {
                    AppSettings.Current.RecentFiles.Clear();
                    AppSettings.Current.Save();
                    UpdateMenus();
                };
                _recentMenu.Items.Add(clear);
            }
            _recentMenu.IsEnabled = recent.Count > 0;
        }

        if (_measureRecentMenu != null)
        {
            _measureRecentMenu.Items.Clear();
            foreach (var text in measureRecent)
            {
                string t = text;
                var item = new MenuFlyoutItem { Text = t };
                item.Click += (_, _) =>
                {
                    var package = new Windows.ApplicationModel.DataTransfer.DataPackage();
                    package.SetText(t);
                    Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
                };
                _measureRecentMenu.Items.Add(item);
            }
            if (measureRecent.Count == 0) _measureRecentMenu.Items.Add(new MenuFlyoutItem { Text = "No recent measurements", IsEnabled = false });
            _measureRecentMenu.Items.Add(new MenuFlyoutSeparator());
            var clear = new MenuFlyoutItem { Text = "Clear Measurements", IsEnabled = Act("measure.clear") };
            clear.Click += (_, _) => R("measure.clear");
            _measureRecentMenu.Items.Add(clear);
        }

        if (_exportKindsMenu != null)
        {
            _exportKindsMenu.Items.Clear();
            var exportAll = new MenuFlyoutItem { Text = "Export…", KeyboardAcceleratorTextOverride = "Ctrl+Shift+E" };
            exportAll.Click += (_, _) => { if (Active != null) _ = Dialogs.ShowExportAsync(Active, false); };
            _exportKindsMenu.Items.Add(exportAll);
            var slicer = new MenuFlyoutItem { Text = "Export to Slicer…", IsEnabled = St.At("export").Int("bodyCount") > 0 };
            slicer.Click += (_, _) => { if (Active != null) _ = Dialogs.ShowExportAsync(Active, true); };
            _exportKindsMenu.Items.Add(slicer);
            var quick = new MenuFlyoutItem { Text = "Quick Export to Favorite Formats", IsEnabled = AppSettings.Current.QuickExportFormats.Count > 0 };
            quick.Click += (_, _) => { if (Active != null) Dialogs.QuickExport(Active); };
            _exportKindsMenu.Items.Add(quick);
            var shot = new MenuFlyoutItem { Text = "Open Screenshot Tool…" };
            shot.Click += (_, _) => _ = Active?.SaveScreenshotAsync(2);
            _exportKindsMenu.Items.Add(shot);
            var viz = new MenuFlyoutItem { Text = "Open Visualization…", IsEnabled = St.At("sketch") == null };
            viz.Click += (_, _) => R("visualize.enter");
            _exportKindsMenu.Items.Add(viz);
            _exportKindsMenu.Items.Add(new MenuFlyoutSeparator());
            foreach (var k in kinds.Items())
            {
                string id = k.Str("id");
                var item = new MenuFlyoutItem { Text = k.Str("title") + "…", IsEnabled = k.Bool("available") && k.Bool("enabled") };
                item.Click += (_, _) => _ = Active?.ExportKindAsync(id);
                _exportKindsMenu.Items.Add(item);
            }
        }
    }
}
