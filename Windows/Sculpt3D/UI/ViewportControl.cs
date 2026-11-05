// The 3D view: a SwapChainPanel the core renders into through ANGLE
// (Windows/native/AngleHost.cpp), fed with raw pointer events
// (AppCore/API.md "Viewport input").
//
// Rendering is on demand, on the UI thread, from CompositionTarget.Rendering:
// a frame is drawn when the document asks for one (listener / commands) or
// sculpt_view_needs_redraw is set; the handler unhooks itself when idle.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Sculpt3D.Core;
using Sculpt3D.Native;
using Windows.Foundation;
using Windows.System;

namespace Sculpt3D.UI;

public sealed class ViewportControl : UserControl
{
    private static readonly List<ViewportControl> All = new();

    private readonly SculptDocument _doc;
    private readonly SwapChainPanel _panel = new();
    private IntPtr _host;
    private bool _loaded, _hooked, _frameWanted, _disposed;
    private int _widthPx, _heightPx;
    private float _scaleX = 1, _scaleY = 1;

    // Touch: the first finger goes to the core; two fingers pan and pinch here.
    private readonly Dictionary<uint, Point> _touches = new();
    private uint? _primaryTouch;
    private bool _multiTouch;
    private double _lastDistance;
    private Point _lastCenter;

    // Mouse and pen: one button gesture at a time.
    private uint? _buttonPointer;
    private int _button;

    /// <summary>Raised with a message when the viewport can't render (no ANGLE, device errors).</summary>
    public event Action<string>? RenderFailed;

    /// <summary>Raised after each rendered frame (annotations were re-read).</summary>
    public event Action? FrameRendered;

    public string? Error { get; private set; }

    public ViewportControl(SculptDocument doc)
    {
        _doc = doc;
        IsTabStop = true;
        UseSystemFocusVisuals = false;
        TabFocusNavigation = KeyboardNavigationMode.Once;
        Content = _panel;
        // The panel must be hit-testable everywhere (even before the first frame).
        _panel.Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent);
        Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(this, "3D view");

        _panel.SizeChanged += (_, _) => UpdateSize();
        _panel.CompositionScaleChanged += (_, _) => UpdateSize();
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;

        _panel.PointerPressed += OnPointerPressed;
        _panel.PointerMoved += OnPointerMoved;
        _panel.PointerReleased += OnPointerReleased;
        _panel.PointerCanceled += OnPointerCanceled;
        _panel.PointerCaptureLost += OnPointerCanceled;
        _panel.PointerExited += OnPointerExited;
        _panel.PointerWheelChanged += OnPointerWheelChanged;

        _doc.FrameRequested += RequestFrame;
        All.Add(this);
    }

    // ---- Lifetime ---------------------------------------------------------------

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _loaded = true;
        if (_host == IntPtr.Zero && Error == null) CreateHost();
        UpdateSize();
        RequestFrame();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _loaded = false;
        Unhook();
    }

    private void CreateHost()
    {
        IntPtr unknown = IntPtr.Zero;
        try
        {
            unknown = WinRT.MarshalInspectable<object>.FromManaged(_panel);
            _host = SculptNative.sculpt_win_host_create(unknown, out IntPtr error);
            if (_host == IntPtr.Zero) Fail(SculptNative.TakeString(error) ?? "Couldn't create the OpenGL ES (ANGLE) view");
        }
        catch (DllNotFoundException ex)
        {
            Fail("sculpt.dll or the ANGLE DLLs (libEGL.dll, libGLESv2.dll) are missing next to Sculpt3D.exe: " + ex.Message);
        }
        catch (EntryPointNotFoundException ex)
        {
            Fail("sculpt.dll is out of date (missing the Windows viewport host): " + ex.Message);
        }
        finally
        {
            if (unknown != IntPtr.Zero) Marshal.Release(unknown);
        }
    }

    private void Fail(string message)
    {
        if (Error == message) return;
        Error = message;
        AppSettings.Log("Viewport: " + message);
        RenderFailed?.Invoke(message);
    }

    /// <summary>Releases the GL resources and the swap chain (the tab closed).</summary>
    public void Close()
    {
        if (_disposed) return;
        _disposed = true;
        Unhook();
        _doc.FrameRequested -= RequestFrame;
        All.Remove(this);
        if (_host != IntPtr.Zero)
        {
            if (_doc.GlGeneration != 0 && SculptNative.sculpt_win_host_make_current(_host) == SculptNative.WinOk)
                SculptNative.sculpt_view_gl_release(_doc.Handle);
            _doc.GlGeneration = 0;
            SculptNative.sculpt_win_host_destroy(_host);
            _host = IntPtr.Zero;
        }
    }

    // ---- Size -------------------------------------------------------------------

    private void UpdateSize()
    {
        if (_disposed) return;
        float sx = _panel.CompositionScaleX > 0 ? _panel.CompositionScaleX : 1;
        float sy = _panel.CompositionScaleY > 0 ? _panel.CompositionScaleY : 1;
        int w = Math.Max(0, (int)Math.Round(_panel.ActualWidth * sx));
        int h = Math.Max(0, (int)Math.Round(_panel.ActualHeight * sy));
        if (w == _widthPx && h == _heightPx && sx == _scaleX && sy == _scaleY) return;
        _widthPx = w;
        _heightPx = h;
        _scaleX = sx;
        _scaleY = sy;
        if (w > 0 && h > 0) SculptNative.sculpt_view_resize(_doc.Handle, w, h, sx);
        if (_host != IntPtr.Zero)
        {
            int st = SculptNative.sculpt_win_host_resize(_host, w, h, sx, sy);
            if (st == SculptNative.WinDeviceLost) HandleDeviceLost();
            else if (st != SculptNative.WinOk) Fail(SculptNative.LastHostError);
        }
        RequestFrame();
    }

    // ---- Rendering --------------------------------------------------------------

    public void RequestFrame()
    {
        _frameWanted = true;
        if (_loaded && !_hooked && !_disposed)
        {
            CompositionTarget.Rendering += OnRendering;
            _hooked = true;
        }
    }

    private void Unhook()
    {
        if (!_hooked) return;
        CompositionTarget.Rendering -= OnRendering;
        _hooked = false;
    }

    private void OnRendering(object? sender, object e)
    {
        if (_disposed || !_loaded) { Unhook(); return; }
        bool needed = _frameWanted || SculptNative.sculpt_view_needs_redraw(_doc.Handle) != 0;
        if (!needed)
        {
            Unhook();
            return;
        }
        _frameWanted = false;
        Render();
    }

    private void Render()
    {
        if (_host == IntPtr.Zero || _widthPx <= 0 || _heightPx <= 0) return;
        try
        {
            int st = SculptNative.sculpt_win_make_current();
            if (st == SculptNative.WinOk)
            {
                ulong generation = SculptNative.sculpt_win_device_generation();
                if (_doc.GlGeneration != generation)
                {
                    st = SculptNative.sculpt_win_host_make_current(_host);
                    if (st == SculptNative.WinOk)
                    {
                        SculptNative.sculpt_view_gl_init(_doc.Handle);
                        SculptNative.sculpt_view_resize(_doc.Handle, _widthPx, _heightPx, _scaleX);
                        _doc.GlGeneration = generation;
                        _doc.RefreshState(force: true);
                        if (!_doc.State.At("gl").Bool("ready")) Fail("OpenGL ES setup failed: " + _doc.State.At("gl").Str("error"));
                    }
                }
                if (st == SculptNative.WinOk) st = SculptNative.sculpt_win_host_render(_host, _doc.Handle);
            }
            if (st == SculptNative.WinDeviceLost)
            {
                HandleDeviceLost();
                return;
            }
            if (st != SculptNative.WinOk)
            {
                Fail(SculptNative.LastHostError);
                return;
            }
            if (Error != null && _doc.State.At("gl").Bool("ready"))
            {
                Error = null;
                RenderFailed?.Invoke("");
            }
        }
        catch (Exception ex)
        {
            Fail(ex.Message);
            return;
        }
        _doc.RefreshAnnotations();
        FrameRendered?.Invoke();
    }

    /// <summary>Renders a PNG of the current view at `scale` × the view size (screenshot, capture).</summary>
    public byte[]? Snapshot(double scale, out string? error)
    {
        error = null;
        if (_host == IntPtr.Zero || _widthPx <= 0)
        {
            error = Error ?? "The viewport isn't showing yet";
            return null;
        }
        int w = Math.Max(1, (int)Math.Round(_widthPx / _scaleX * scale));
        int h = Math.Max(1, (int)Math.Round(_heightPx / _scaleY * scale));
        if (SculptNative.sculpt_win_host_make_current(_host) != SculptNative.WinOk)
        {
            error = SculptNative.LastHostError;
            return null;
        }
        if (_doc.GlGeneration != SculptNative.sculpt_win_device_generation())
        {
            error = "The viewport hasn't rendered yet";
            return null;
        }
        var bytes = SculptNative.TakeBytes(SculptNative.sculpt_view_snapshot_png(_doc.Handle, w, h, out nuint len), len);
        if (bytes == null) error = "Rendering the image failed";
        RequestFrame();
        return bytes;
    }

    /// <summary>After DXGI_ERROR_DEVICE_REMOVED/RESET: drop every document's GL
    /// objects, then the device; the next frames recreate them.</summary>
    private static void HandleDeviceLost()
    {
        AppSettings.Log("Graphics device lost; recreating the viewport device");
        foreach (var v in All)
        {
            if (v._host != IntPtr.Zero && v._doc.GlGeneration != 0)
            {
                SculptNative.sculpt_win_host_make_current(v._host);
                SculptNative.sculpt_view_gl_release(v._doc.Handle);
            }
            v._doc.GlGeneration = 0;
        }
        SculptNative.sculpt_win_reset_device();
        foreach (var v in All)
        {
            if (v._host != IntPtr.Zero) SculptNative.sculpt_win_host_resize(v._host, v._widthPx, v._heightPx, v._scaleX, v._scaleY);
            v.RequestFrame();
        }
    }

    // ---- Pointer input ------------------------------------------------------------

    private static int Modifiers(VirtualKeyModifiers m)
    {
        int mods = 0;
        if (m.HasFlag(VirtualKeyModifiers.Shift)) mods |= SculptNative.ModShift;
        if (m.HasFlag(VirtualKeyModifiers.Control)) mods |= SculptNative.ModControl;
        if (m.HasFlag(VirtualKeyModifiers.Menu)) mods |= SculptNative.ModAlt;
        return mods;
    }

    private void Send(uint id, int type, int button, int phase, Point p, int mods)
    {
        SculptNative.sculpt_view_pointer(_doc.Handle, unchecked((int)id), type, button, phase, (float)p.X, (float)p.Y, mods);
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        Focus(FocusState.Pointer);
        var pt = e.GetCurrentPoint(_panel);
        int mods = Modifiers(e.KeyModifiers);
        uint id = e.Pointer.PointerId;
        switch (e.Pointer.PointerDeviceType)
        {
            case PointerDeviceType.Touch:
                TouchDown(id, pt.Position, mods);
                _panel.CapturePointer(e.Pointer);
                break;
            case PointerDeviceType.Pen:
                if (_buttonPointer != null) break;
                _buttonPointer = id;
                _button = pt.Properties.IsBarrelButtonPressed ? SculptNative.ButtonSecondary : SculptNative.ButtonPrimary;
                _panel.CapturePointer(e.Pointer);
                Send(id, SculptNative.PointerPen, _button, SculptNative.PhaseDown, pt.Position, mods);
                break;
            default: // mouse, touchpad
                if (_buttonPointer != null) break; // a second button while one is held: ignore
                int button = pt.Properties.PointerUpdateKind switch
                {
                    PointerUpdateKind.RightButtonPressed => SculptNative.ButtonSecondary,
                    PointerUpdateKind.MiddleButtonPressed => SculptNative.ButtonMiddle,
                    _ => SculptNative.ButtonPrimary,
                };
                _buttonPointer = id;
                _button = button;
                _panel.CapturePointer(e.Pointer);
                Send(id, SculptNative.PointerMouse, button, SculptNative.PhaseDown, pt.Position, mods);
                break;
        }
        e.Handled = true;
    }

    private void OnPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(_panel);
        int mods = Modifiers(e.KeyModifiers);
        uint id = e.Pointer.PointerId;
        switch (e.Pointer.PointerDeviceType)
        {
            case PointerDeviceType.Touch:
                TouchMove(id, pt.Position, mods);
                break;
            case PointerDeviceType.Pen:
                if (_buttonPointer == id) Send(id, SculptNative.PointerPen, _button, SculptNative.PhaseMove, pt.Position, mods);
                else if (_buttonPointer == null) Send(id, SculptNative.PointerPen, 0, SculptNative.PhaseHover, pt.Position, mods);
                break;
            default:
                if (_buttonPointer == id) Send(id, SculptNative.PointerMouse, _button, SculptNative.PhaseMove, pt.Position, mods);
                else if (_buttonPointer == null) Send(id, SculptNative.PointerMouse, 0, SculptNative.PhaseHover, pt.Position, mods);
                break;
        }
        e.Handled = true;
    }

    private void OnPointerReleased(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(_panel);
        int mods = Modifiers(e.KeyModifiers);
        uint id = e.Pointer.PointerId;
        switch (e.Pointer.PointerDeviceType)
        {
            case PointerDeviceType.Touch:
                TouchUp(id, pt.Position, mods, cancelled: false);
                break;
            case PointerDeviceType.Pen:
                if (_buttonPointer == id)
                {
                    Send(id, SculptNative.PointerPen, _button, SculptNative.PhaseUp, pt.Position, mods);
                    _buttonPointer = null;
                }
                break;
            default:
                if (_buttonPointer == id)
                {
                    Send(id, SculptNative.PointerMouse, _button, SculptNative.PhaseUp, pt.Position, mods);
                    _buttonPointer = null;
                }
                break;
        }
        _panel.ReleasePointerCapture(e.Pointer);
        e.Handled = true;
    }

    private void OnPointerCanceled(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(_panel);
        uint id = e.Pointer.PointerId;
        int mods = Modifiers(e.KeyModifiers);
        if (e.Pointer.PointerDeviceType == PointerDeviceType.Touch)
        {
            TouchUp(id, pt.Position, mods, cancelled: true);
        }
        else if (_buttonPointer == id)
        {
            int type = e.Pointer.PointerDeviceType == PointerDeviceType.Pen ? SculptNative.PointerPen : SculptNative.PointerMouse;
            Send(id, type, _button, SculptNative.PhaseCancel, pt.Position, mods);
            _buttonPointer = null;
        }
    }

    private void OnPointerExited(object sender, PointerRoutedEventArgs e)
    {
        if (e.Pointer.PointerDeviceType == PointerDeviceType.Touch || _buttonPointer != null) return;
        var pt = e.GetCurrentPoint(_panel);
        int type = e.Pointer.PointerDeviceType == PointerDeviceType.Pen ? SculptNative.PointerPen : SculptNative.PointerMouse;
        Send(e.Pointer.PointerId, type, 0, SculptNative.PhaseExit, pt.Position, Modifiers(e.KeyModifiers));
    }

    private void TouchDown(uint id, Point p, int mods)
    {
        _touches[id] = p;
        if (_touches.Count == 1)
        {
            _primaryTouch = id;
            _multiTouch = false;
            Send(id, SculptNative.PointerTouch, SculptNative.ButtonPrimary, SculptNative.PhaseDown, p, mods);
        }
        else if (_touches.Count == 2 && !_multiTouch)
        {
            // A second finger cancels the one-finger gesture in the core (a
            // drawing drag ends); from here on the host pans and pinches.
            Send(id, SculptNative.PointerTouch, SculptNative.ButtonPrimary, SculptNative.PhaseDown, p, mods);
            Send(id, SculptNative.PointerTouch, SculptNative.ButtonPrimary, SculptNative.PhaseCancel, p, mods);
            _multiTouch = true;
            (_lastCenter, _lastDistance) = TwoFingers();
        }
    }

    private void TouchMove(uint id, Point p, int mods)
    {
        if (!_touches.ContainsKey(id)) return;
        _touches[id] = p;
        if (_multiTouch)
        {
            if (_touches.Count < 2) return;
            var (center, distance) = TwoFingers();
            double dx = center.X - _lastCenter.X, dy = center.Y - _lastCenter.Y;
            if (dx != 0 || dy != 0) SculptNative.sculpt_view_pan(_doc.Handle, (float)dx, (float)dy);
            if (_lastDistance > 1 && distance > 1)
            {
                double factor = distance / _lastDistance;
                if (Math.Abs(factor - 1) > 1e-4) SculptNative.sculpt_view_zoom(_doc.Handle, (float)factor, (float)center.X, (float)center.Y);
            }
            _lastCenter = center;
            _lastDistance = distance;
        }
        else if (id == _primaryTouch)
        {
            Send(id, SculptNative.PointerTouch, SculptNative.ButtonPrimary, SculptNative.PhaseMove, p, mods);
        }
    }

    private void TouchUp(uint id, Point p, int mods, bool cancelled)
    {
        if (!_touches.Remove(id)) return;
        if (!_multiTouch && id == _primaryTouch)
            Send(id, SculptNative.PointerTouch, SculptNative.ButtonPrimary, cancelled ? SculptNative.PhaseCancel : SculptNative.PhaseUp, p, mods);
        if (_multiTouch && _touches.Count >= 2) (_lastCenter, _lastDistance) = TwoFingers();
        if (_touches.Count == 0)
        {
            _multiTouch = false;
            _primaryTouch = null;
        }
    }

    private (Point Center, double Distance) TwoFingers()
    {
        var pts = _touches.Values.Take(2).ToArray();
        if (pts.Length < 2) return (pts.Length == 1 ? pts[0] : new Point(), 0);
        var c = new Point((pts[0].X + pts[1].X) / 2, (pts[0].Y + pts[1].Y) / 2);
        double d = Math.Sqrt(Math.Pow(pts[0].X - pts[1].X, 2) + Math.Pow(pts[0].Y - pts[1].Y, 2));
        return (c, d);
    }

    /// <summary>Mouse wheel zooms (wheel up zooms in). On a precision touchpad
    /// two-finger scrolling orbits like the Mac trackpad, Shift pans, and a
    /// pinch (delivered as Ctrl+wheel) zooms.</summary>
    private void OnPointerWheelChanged(object sender, PointerRoutedEventArgs e)
    {
        var pt = e.GetCurrentPoint(_panel);
        int delta = pt.Properties.MouseWheelDelta;
        if (delta == 0) return;
        bool horizontal = pt.Properties.IsHorizontalMouseWheel;
        bool touchpad = e.Pointer.PointerDeviceType == PointerDeviceType.Touchpad;
        var m = e.KeyModifiers;
        const double pointsPerNotch = 60;
        double d = delta / 120.0 * pointsPerNotch;
        var doc = _doc.Handle;
        if (m.HasFlag(VirtualKeyModifiers.Control))
        {
            SculptNative.sculpt_view_zoom(doc, (float)Math.Pow(1.1, delta / 120.0), (float)pt.Position.X, (float)pt.Position.Y);
        }
        else if (m.HasFlag(VirtualKeyModifiers.Shift))
        {
            if (horizontal) SculptNative.sculpt_view_pan(doc, (float)-d, 0);
            else SculptNative.sculpt_view_pan(doc, 0, (float)d);
        }
        else if (horizontal)
        {
            SculptNative.sculpt_view_orbit(doc, (float)-d, 0);
        }
        else if (touchpad || m.HasFlag(VirtualKeyModifiers.Menu))
        {
            SculptNative.sculpt_view_orbit(doc, 0, (float)d);
        }
        else
        {
            SculptNative.sculpt_view_zoom(doc, (float)Math.Pow(1.1, delta / 120.0), (float)pt.Position.X, (float)pt.Position.Y);
        }
        RequestFrame();
        e.Handled = true;
    }
}
