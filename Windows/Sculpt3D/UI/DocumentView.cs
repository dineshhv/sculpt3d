// One document's window content (ContentView.swift): the 3D view with the
// annotations over it, the rail and flyouts on the left, banners and the
// bottom bar in the middle, view controls and the right-hand panels.
// It also performs the front-end "UI actions" the core asks for (file pickers,
// sheets, screenshots, exports; AppCore/API.md "UI actions").
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json.Nodes;
using System.Threading.Tasks;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Sculpt3D.Core;
using Sculpt3D.Native;
using Windows.System;

namespace Sculpt3D.UI;

public sealed class DocumentView : Grid
{
    public SculptDocument Doc { get; }
    public MainWindow Main { get; }
    public ViewportControl Viewport { get; }

    // ---- Window-level UI state (UIState in the Swift app) ----
    public bool ShowItems { get; private set; } = AppSettings.Current.ShowItems;
    public bool ShowHistory { get; private set; } = AppSettings.Current.ShowHistory;
    public bool ShowProperties { get; private set; } = AppSettings.Current.ShowProperties;
    public bool ShowSearch { get; private set; }
    public bool ShowProject { get; private set; }
    public string? OpenMenu { get; private set; }
    public int VisualizeTab { get; private set; }

    /// <summary>DrawingSettings for File ▸ New Drawing and drawing exports.</summary>
    public JsonObject Drawing { get; } = new()
    {
        ["paper"] = "a3",
        ["views"] = new JsonArray("front", "top", "right", "isometric"),
        ["showHidden"] = true,
        ["showDimensions"] = true,
        ["scale"] = null,
    };

    public string ThemeKey => ActualTheme.ToString();

    /// <summary>Raised after every refresh (MainWindow updates title, tab and menus).</summary>
    public event Action? Changed;

    private readonly AnnotationsOverlay _annotations;
    private readonly ToolRail _rail;
    private readonly ToolFlyout _flyout;
    private readonly ItemsPanel _items;
    private readonly PointPickBanner _pointPick;
    private readonly PinnedCard _pinned;
    private readonly SectionPanel _section;
    private readonly OperationPanel _operation;
    private readonly SketchBar _sketchBar;
    private readonly SelectionBar _selectionBar;
    private readonly ViewControlsColumn _viewControls;
    private readonly ConstraintColumn _constraints;
    private readonly VisualizationPanel _visualization;
    private readonly MeasureSidePanel _measure;
    private readonly PropertiesPanel _properties;
    private readonly HistoryPanel _history;
    private readonly List<StatePanel> _panels = new();
    private readonly InfoBar _toast;
    private readonly Grid _searchLayer;
    private readonly SearchPalette _search;
    private readonly Border _errorBanner;
    private readonly Flyout _projectFlyout = new() { Placement = FlyoutPlacementMode.RightEdgeAlignedTop };
    private FrameworkElement? _projectAnchor;
    private Microsoft.UI.Dispatching.DispatcherQueueTimer? _toastTimer;
    private string? _shownToast;
    private string? _localToast;
    private ulong _uiSerial;
    private bool _refreshQueued;

    /// <summary>Live dialogs that follow the state while open (Variables).</summary>
    internal readonly List<StatePanel> LivePanels = new();

    public DocumentView(MainWindow main, SculptDocument doc)
    {
        Main = main;
        Doc = doc;
        Viewport = new ViewportControl(doc);
        _annotations = new AnnotationsOverlay(this);
        _rail = new ToolRail(this);
        _flyout = new ToolFlyout(this);
        _items = new ItemsPanel(this);
        _pointPick = new PointPickBanner(this);
        _pinned = new PinnedCard(this);
        _section = new SectionPanel(this);
        _operation = new OperationPanel(this);
        _sketchBar = new SketchBar(this);
        _selectionBar = new SelectionBar(this);
        _viewControls = new ViewControlsColumn(this);
        _constraints = new ConstraintColumn(this);
        _visualization = new VisualizationPanel(this);
        _measure = new MeasureSidePanel(this);
        _properties = new PropertiesPanel(this);
        _history = new HistoryPanel(this);
        _panels.AddRange(new StatePanel[]
        {
            _rail, _flyout, _items, _pointPick, _pinned, _section, _operation, _sketchBar, _selectionBar,
            _viewControls, _constraints, _visualization, _measure, _properties, _history,
        });

        _toast = new InfoBar { Severity = InfoBarSeverity.Warning, IsClosable = true, IsOpen = false, HorizontalAlignment = HorizontalAlignment.Center, MaxWidth = 560 };
        _toast.CloseButtonClick += (_, _) => DismissToast();

        _search = new SearchPalette(this);
        _searchLayer = new Grid { Background = new SolidColorBrush(ColorHelper.FromArgb(20, 0, 0, 0)), Visibility = Visibility.Collapsed };
        _searchLayer.Tapped += (_, e) =>
        {
            if (ReferenceEquals(e.OriginalSource, _searchLayer)) SetShowSearch(false);
        };
        _search.HorizontalAlignment = HorizontalAlignment.Center;
        _search.VerticalAlignment = VerticalAlignment.Top;
        _search.Margin = new Thickness(0, 80, 0, 0);
        _searchLayer.Children.Add(_search);

        _errorBanner = new Border
        {
            Visibility = Visibility.Collapsed,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
            MaxWidth = 520,
            Padding = new Thickness(18),
            CornerRadius = new CornerRadius(12),
            Background = Ui.OpaquePanelBackground,
            BorderBrush = Ui.Stroke,
            BorderThickness = new Thickness(1),
        };

        BuildLayout();

        _uiSerial = Doc.State.At("uiRequest").ULong("serial");
        Doc.StateChanged += QueueRefresh;
        Doc.AnnotationsChanged += () =>
        {
            _annotations.Update();
            _viewControls.UpdateCube();
        };
        Viewport.RenderFailed += message =>
        {
            _errorBanner.Child = Ui.VStack(8, Ui.Headline("The 3D view can't start"), Ui.Text(message, 13, null, Ui.Secondary, wrap: true),
                Ui.Caption("See Windows\\README.md ▸ First build troubleshooting. The log is in %LOCALAPPDATA%\\Sculpt3D\\log.txt."));
            _errorBanner.Visibility = string.IsNullOrEmpty(message) ? Visibility.Collapsed : Visibility.Visible;
        };
        KeyDown += OnKeyDown;
        SliderBinding.DragEnded += OnSliderDragEnded;
        SizeChanged += (_, e) =>
        {
            // The right panels give way to the constraint column on narrow windows.
            if ((e.PreviousSize.Width < 1200) != (e.NewSize.Width < 1200)) Refresh();
        };
        ActualThemeChanged += (_, _) =>
        {
            foreach (var p in _panels) p.Invalidate();
            Refresh();
        };
        Loaded += (_, _) =>
        {
            Refresh();
            FocusViewport();
        };
    }

    private void BuildLayout()
    {
        Children.Add(Viewport);
        Children.Add(_annotations);

        var overlay = new Grid { Margin = new Thickness(10), ColumnSpacing = 10 };
        overlay.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        overlay.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        overlay.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star), MinWidth = 0 });
        overlay.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        overlay.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });

        _rail.VerticalAlignment = VerticalAlignment.Stretch;
        overlay.Children.Add(_rail);

        var leftSecond = new Grid();
        _flyout.VerticalAlignment = VerticalAlignment.Top;
        _items.VerticalAlignment = VerticalAlignment.Top;
        leftSecond.Children.Add(_flyout);
        leftSecond.Children.Add(_items);
        Grid.SetColumn(leftSecond, 1);
        overlay.Children.Add(leftSecond);

        var center = new Grid { RowSpacing = 10 };
        center.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        center.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        center.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var top = Ui.VStack(10, _pointPick, _pinned, _toast);
        top.HorizontalAlignment = HorizontalAlignment.Center;
        center.Children.Add(top);
        var bottom = Ui.VStack(8, _section, _operation, _sketchBar, _selectionBar);
        bottom.HorizontalAlignment = HorizontalAlignment.Center;
        Grid.SetRow(bottom, 2);
        center.Children.Add(bottom);
        Grid.SetColumn(center, 2);
        overlay.Children.Add(center);

        var rightControls = new Grid { RowSpacing = 10 };
        rightControls.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        rightControls.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        _viewControls.HorizontalAlignment = HorizontalAlignment.Right;
        rightControls.Children.Add(_viewControls);
        _constraints.HorizontalAlignment = HorizontalAlignment.Right;
        _constraints.VerticalAlignment = VerticalAlignment.Top;
        Grid.SetRow(_constraints, 1);
        rightControls.Children.Add(_constraints);
        Grid.SetColumn(rightControls, 3);
        overlay.Children.Add(rightControls);

        var right = new Grid { RowSpacing = 10 };
        right.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        right.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        Grid.SetRowSpan(_visualization, 2);
        _visualization.VerticalAlignment = VerticalAlignment.Stretch;
        right.Children.Add(_visualization);
        _measure.VerticalAlignment = VerticalAlignment.Top;
        Grid.SetRowSpan(_measure, 2);
        right.Children.Add(_measure);
        _properties.VerticalAlignment = VerticalAlignment.Top;
        right.Children.Add(_properties);
        _history.VerticalAlignment = VerticalAlignment.Stretch;
        Grid.SetRow(_history, 1);
        right.Children.Add(_history);
        Grid.SetColumn(right, 4);
        overlay.Children.Add(right);

        Children.Add(overlay);
        Children.Add(_errorBanner);
        Children.Add(_searchLayer);
    }

    // ---- Refresh ----------------------------------------------------------------------

    private void QueueRefresh()
    {
        if (_refreshQueued) return;
        _refreshQueued = true;
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Normal, () =>
        {
            _refreshQueued = false;
            Refresh();
        });
    }

    /// <summary>Brings every panel up to date with the state.</summary>
    public void Refresh()
    {
        CheckUiRequest();
        foreach (var p in _panels) p.Update();
        foreach (var p in LivePanels.ToArray()) p.Update();
        _annotations.Update();
        UpdateToast();
        if (S.Str("mode") != "modeling" && OpenMenu != null)
        {
            OpenMenu = null;
            _rail.Update();
            _flyout.Update();
        }
        AlignFlyout();
        Changed?.Invoke();
    }

    private JsonNode S => Doc.State;

    private void AlignFlyout()
    {
        // The open group's tools sit beside the rail's icons, level with them.
        if (OpenMenu == null || _rail.MiddleGroup == null) return;
        DispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                if (_rail.MiddleGroup == null || _flyout.Parent is not FrameworkElement host) return;
                var p = _rail.MiddleGroup.TransformToVisual(host).TransformPoint(new Windows.Foundation.Point(0, 0));
                _flyout.Measure(new Windows.Foundation.Size(double.PositiveInfinity, double.PositiveInfinity));
                double available = host.ActualHeight;
                double top = Math.Max(0, Math.Min(p.Y, available - _flyout.DesiredSize.Height));
                _flyout.Margin = new Thickness(0, top, 0, 0);
            }
            catch (ArgumentException)
            {
                // Not in the same tree yet; the next refresh aligns it.
            }
        });
    }

    // ---- UI flags ----------------------------------------------------------------------

    public void SetShowItems(bool on)
    {
        ShowItems = on;
        if (on) OpenMenu = null;
        AppSettings.Current.ShowItems = on;
        AppSettings.Current.Save();
        Refresh();
    }

    public void SetShowHistory(bool on)
    {
        ShowHistory = on;
        AppSettings.Current.ShowHistory = on;
        AppSettings.Current.Save();
        Refresh();
    }

    public void SetShowProperties(bool on)
    {
        ShowProperties = on;
        AppSettings.Current.ShowProperties = on;
        AppSettings.Current.Save();
        Refresh();
    }

    public void CollapseAll()
    {
        ShowHistory = ShowItems = ShowProperties = false;
        OpenMenu = null;
        Refresh();
    }

    public void SetOpenMenu(string? category)
    {
        OpenMenu = category;
        Refresh();
    }

    public void SetVisualizeTab(int tab)
    {
        VisualizeTab = tab;
        Refresh();
    }

    public void SetShowSearch(bool on)
    {
        ShowSearch = on;
        _searchLayer.Visibility = on ? Visibility.Visible : Visibility.Collapsed;
        if (on) _search.Open();
        else FocusViewport();
        _rail.Update();
    }

    public void AttachProjectFlyout(FrameworkElement anchor) => _projectAnchor = anchor;

    public void ToggleProjectPanel()
    {
        if (ShowProject)
        {
            _projectFlyout.Hide();
            return;
        }
        if (_projectAnchor == null) return;
        _projectFlyout.Content = Panels.ProjectPanel(this);
        _projectFlyout.Opened += OnProjectOpened;
        _projectFlyout.Closed += OnProjectClosed;
        _projectFlyout.ShowAt(_projectAnchor);
    }

    private void OnProjectOpened(object? sender, object e)
    {
        ShowProject = true;
        _rail.Update();
    }

    private void OnProjectClosed(object? sender, object e)
    {
        _projectFlyout.Opened -= OnProjectOpened;
        _projectFlyout.Closed -= OnProjectClosed;
        ShowProject = false;
        _rail.Update();
    }

    public void FocusViewport() => Viewport.Focus(FocusState.Programmatic);

    // ---- Commands -----------------------------------------------------------------------

    /// <summary>Runs a core command; shows its error and performs any UI action it asks for.</summary>
    public JsonNode Run(string cmd, JsonObject? args = null)
    {
        var reply = Doc.Command(cmd, args);
        if (!reply.Bool("ok"))
        {
            string error = reply.Str("error");
            if (!string.IsNullOrEmpty(error)) ShowMessage(error);
        }
        string? ui = reply.StrOrNull("ui");
        if (!string.IsNullOrEmpty(ui)) _ = HandleUiActionAsync(ui);
        Refresh();
        return reply;
    }

    public void RunTool(string id) => Run("tool.run", new JsonObject { ["id"] = id });

    /// <summary>A key shortcut (in the core) asked for front-end UI.</summary>
    private void CheckUiRequest()
    {
        var req = S.At("uiRequest");
        ulong serial = req.ULong("serial");
        if (serial <= _uiSerial) return;
        _uiSerial = serial;
        string action = req.Str("action");
        if (string.IsNullOrEmpty(action)) return;
        Doc.Command("ui.clearRequest");
        _ = HandleUiActionAsync(action);
    }

    /// <summary>Front-end work the core hands back (AppCore/API.md "UI actions").</summary>
    public async Task HandleUiActionAsync(string action)
    {
        try
        {
            switch (action)
            {
                case "ui.showVariables":
                    await Dialogs.ShowVariablesAsync(this);
                    break;
                case "ui.importImage":
                    await ImportImageAsync(0);
                    break;
                case "ui.importDecal":
                    await ImportImageAsync(1);
                    break;
                case "ui.importHeightMap":
                    await ImportImageAsync(2);
                    break;
                case "ui.importFile":
                    await ImportFilesAsync(Pickers.CadFiles);
                    break;
                case "ui.importProject":
                    await ImportFilesAsync(Pickers.ProjectFiles);
                    break;
                case "ui.importMaterial":
                    await ImportMaterialAsync();
                    break;
                case "ui.sketchText":
                    await Dialogs.ShowSketchTextAsync(this);
                    break;
                case "view.screenshot":
                    await SaveScreenshotAsync(2);
                    break;
                default:
                    if (action.StartsWith("export.", StringComparison.Ordinal)) await ExportKindAsync(action.Substring(7));
                    else AppSettings.Log("Unhandled UI action " + action);
                    break;
            }
        }
        catch (Exception e)
        {
            AppSettings.Log($"UI action {action}: {e}");
            ShowMessage(e.Message);
        }
    }

    // ---- Toasts -----------------------------------------------------------------------

    /// <summary>A transient message (command errors), shown like the core's toasts.</summary>
    public void ShowMessage(string text)
    {
        _localToast = text;
        UpdateToast();
    }

    private void UpdateToast()
    {
        string? toast = S.StrOrNull("toast") ?? _localToast;
        if (toast == _shownToast) return;
        _shownToast = toast;
        _toast.Message = toast ?? "";
        _toast.IsOpen = toast != null;
        _toastTimer ??= CreateToastTimer();
        _toastTimer.Stop();
        if (toast != null) _toastTimer.Start();
    }

    private Microsoft.UI.Dispatching.DispatcherQueueTimer CreateToastTimer()
    {
        var t = DispatcherQueue.CreateTimer();
        t.Interval = TimeSpan.FromSeconds(4);
        t.IsRepeating = false;
        t.Tick += (_, _) => DismissToast();
        return t;
    }

    private void DismissToast()
    {
        _localToast = null;
        if (S.StrOrNull("toast") != null) Doc.Command("toast.dismiss");
        UpdateToast();
    }

    // ---- Keys -----------------------------------------------------------------------

    /// <summary>Plain and Shift keys go to the core's key handler (tool letters,
    /// Esc, Return, Delete); Ctrl/Alt shortcuts are the menu bar's accelerators.</summary>
    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.OriginalSource is TextBox || e.OriginalSource is PasswordBox || e.OriginalSource is RichEditBox || e.OriginalSource is AutoSuggestBox) return;
        var ctrl = Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Control).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
        var alt = Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Menu).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
        var shift = Microsoft.UI.Input.InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Shift).HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);
        if (ctrl || alt) return;
        if (e.Key == VirtualKey.Escape)
        {
            if (ShowSearch) { SetShowSearch(false); e.Handled = true; return; }
            if (OpenMenu != null) { SetOpenMenu(null); e.Handled = true; return; }
        }
        string? key = KeyName(e.Key);
        if (key == null) return;
        int mods = shift ? SculptNative.ModShift : 0;
        if (SculptNative.sculpt_view_key(Doc.Handle, key, mods) != 0)
        {
            e.Handled = true;
            Doc.RefreshState(force: false);
            Viewport.RequestFrame();
        }
    }

    private static string? KeyName(VirtualKey key)
    {
        if (key >= VirtualKey.A && key <= VirtualKey.Z) return ((char)('a' + (key - VirtualKey.A))).ToString();
        if (key >= VirtualKey.Number0 && key <= VirtualKey.Number9) return ((char)('0' + (key - VirtualKey.Number0))).ToString();
        if (key >= VirtualKey.NumberPad0 && key <= VirtualKey.NumberPad9) return ((char)('0' + (key - VirtualKey.NumberPad0))).ToString();
        return key switch
        {
            VirtualKey.Escape => "Escape",
            VirtualKey.Delete => "Delete",
            VirtualKey.Back => "Backspace",
            VirtualKey.Enter => "Enter",
            VirtualKey.Tab => "Tab",
            VirtualKey.Up => "Up",
            VirtualKey.Down => "Down",
            VirtualKey.Left => "Left",
            VirtualKey.Right => "Right",
            _ => null,
        };
    }

    // ---- Files: import -----------------------------------------------------------

    private async Task ImportFilesAsync(IReadOnlyList<string> extensions)
    {
        var paths = await Pickers.OpenFilesAsync(Main, extensions, multiple: true);
        if (paths.Count == 0) return;
        foreach (var path in paths)
        {
            var error = Doc.ImportPath(path);
            if (error != null) ShowMessage($"{Path.GetFileName(path)}: {error}");
        }
        Run("view.fit");
    }

    private async Task ImportImageAsync(int mode)
    {
        var paths = await Pickers.OpenFilesAsync(Main, Pickers.ImageFiles, multiple: false);
        if (paths.Count == 0) return;
        var path = paths[0];
        var error = Doc.ImportImage(await File.ReadAllBytesAsync(path), Path.GetFileNameWithoutExtension(path), mode);
        if (error != null) ShowMessage(error);
        Refresh();
    }

    private async Task ImportMaterialAsync()
    {
        var paths = await Pickers.OpenFilesAsync(Main, new[] { ".json" }, multiple: false);
        if (paths.Count == 0) return;
        Run("import.material", new JsonObject { ["path"] = paths[0] });
    }

    // ---- Files: export ------------------------------------------------------------

    /// <summary>Exports one kind (ExportKind id, or a raw format like "brep") through the save dialog.</summary>
    public async Task ExportKindAsync(string kindOrFormat, bool drawing = false)
    {
        var kind = S.At("export").Arr("kinds").FirstOrDefault(k => k.Str("id") == kindOrFormat || k.Str("extension") == kindOrFormat);
        string id = kind?.Str("id") ?? kindOrFormat;
        string ext = kind?.Str("extension") ?? kindOrFormat;
        string title = kind?.Str("title") ?? kindOrFormat.ToUpperInvariant();
        string? options = drawing || (kind != null && kind.Bool("fromDrawing")) ? new JsonObject { ["drawing"] = DrawingSettingsJson() }.ToJsonString() : null;
        var data = Doc.Export(id, options, out string? error);
        if (data == null)
        {
            ShowMessage(error ?? "Nothing to export");
            return;
        }
        var path = await Pickers.SaveFileAsync(Main, $"{Doc.Name}.{ext}", title, "." + ext);
        if (path == null) return;
        await File.WriteAllBytesAsync(path, data);
        ShowMessage($"Exported {Path.GetFileName(path)}");
    }

    public JsonObject DrawingSettingsJson()
    {
        var d = (JsonObject)Drawing.DeepClone();
        if (!d.ContainsKey("title")) d["title"] = Doc.Name;
        return d;
    }

    public async Task SaveScreenshotAsync(double scale, string name = "Sculpt3D Screenshot")
    {
        var png = Viewport.Snapshot(scale, out string? error);
        if (png == null)
        {
            ShowMessage(error ?? "Couldn't render the view");
            return;
        }
        var path = await Pickers.SaveFileAsync(Main, name + ".png", "PNG image", ".png");
        if (path == null) return;
        await File.WriteAllBytesAsync(path, png);
        ShowMessage($"Saved {Path.GetFileName(path)}");
    }

    /// <summary>File ▸ View in Browser / Publish Current Version.</summary>
    public async Task WebPageAsync(bool publish)
    {
        var reply = Doc.Command("export.webPage", new JsonObject { ["title"] = Doc.Name });
        if (!reply.Bool("ok"))
        {
            ShowMessage(reply.Str("error"));
            return;
        }
        string html = reply.Str("html");
        if (publish)
        {
            var path = await Pickers.SaveFileAsync(Main, reply.Str("defaultName", Doc.Name) + ".html", "Web page", ".html");
            if (path == null) return;
            await File.WriteAllTextAsync(path, html);
            ShowMessage($"Published {Path.GetFileName(path)}");
        }
        else
        {
            string dir = Path.Combine(Path.GetTempPath(), "Sculpt3D");
            Directory.CreateDirectory(dir);
            string path = Path.Combine(dir, Pickers.SafeFileName(Doc.Name) + ".html");
            await File.WriteAllTextAsync(path, html);
            OpenWithShell(path);
        }
    }

    public static void OpenWithShell(string path)
    {
        try
        {
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true });
        }
        catch (Exception e)
        {
            AppSettings.Log("Open " + path + ": " + e.Message);
        }
    }

    public void CopyLink()
    {
        if (Doc.FilePath == null) return;
        var package = new Windows.ApplicationModel.DataTransfer.DataPackage();
        package.SetText(new Uri(Doc.FilePath).AbsoluteUri);
        Windows.ApplicationModel.DataTransfer.Clipboard.SetContent(package);
        ShowMessage("Copied the project's link");
    }

    private void OnSliderDragEnded() => QueueRefresh();

    public void Close()
    {
        SliderBinding.DragEnded -= OnSliderDragEnded;
        Viewport.Close();
        LivePanels.Clear();
    }
}
