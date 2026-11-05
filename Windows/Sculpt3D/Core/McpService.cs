// The Model Context Protocol server (AppCore/src/mcp) over every open
// document, on http://127.0.0.1:7317/mcp. Tool calls are posted from the HTTP
// thread; each post schedules sculpt_mcp_drain() on the UI thread.
using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.UI.Dispatching;
using Sculpt3D.Native;

namespace Sculpt3D.Core;

public static unsafe class McpService
{
    public const int Port = 7317;
    public static string Endpoint => $"http://127.0.0.1:{Port}/mcp";
    public static string SetupCommand => $"claude mcp add --transport http sculpt3d {Endpoint}";

    private static DispatcherQueue? _dispatcher;
    private static Func<string, SculptDocument?>? _newDocument;
    private static int _drainQueued;

    /// <summary>"Listening on 127.0.0.1:7317", "Off" or "Not running: …".</summary>
    public static string Status { get; private set; } = "Off";
    public static bool Running
    {
        get
        {
            try { return SculptNative.sculpt_mcp_running() != 0; }
            catch (Exception) { return false; } // sculpt.dll missing: reported at launch
        }
    }
    public static event Action? StatusChanged;

    /// <summary>Wires the UI thread and the handler that opens a window for new_document.</summary>
    public static void Initialize(DispatcherQueue dispatcher, Func<string, SculptDocument?> newDocument)
    {
        _dispatcher = dispatcher;
        _newDocument = newDocument;
        SculptNative.sculpt_mcp_set_new_document_handler((IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, IntPtr, IntPtr>)&OnNewDocument, IntPtr.Zero);
    }

    public static void Start()
    {
        var error = SculptNative.TakeString(SculptNative.sculpt_mcp_start(Port, (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, void>)&OnPost, IntPtr.Zero));
        Status = error == null ? $"Listening on 127.0.0.1:{Port}" : $"Not running: {error}";
        if (error != null) AppSettings.Log("MCP: " + error);
        StatusChanged?.Invoke();
    }

    public static void Stop()
    {
        if (_dispatcher == null) return; // never initialised (no core)
        SculptNative.sculpt_mcp_stop();
        Status = "Off";
        StatusChanged?.Invoke();
    }

    public static void SetEnabled(bool on)
    {
        AppSettings.Current.McpEnabled = on;
        AppSettings.Current.Save();
        if (on) Start(); else Stop();
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnPost(IntPtr context)
    {
        try
        {
            if (Interlocked.Exchange(ref _drainQueued, 1) == 1) return;
            _dispatcher?.TryEnqueue(DispatcherQueuePriority.Normal, Drain);
        }
        catch
        {
        }
    }

    private static void Drain()
    {
        Interlocked.Exchange(ref _drainQueued, 0);
        try
        {
            // A screenshot tool renders right here (the UI thread is the GL
            // thread): the shared GL context must be current.
            SculptNative.sculpt_win_make_current();
            SculptNative.sculpt_mcp_drain();
        }
        catch (Exception e)
        {
            AppSettings.Log("MCP drain: " + e);
        }
    }

    /// <summary>new_document: called inside sculpt_mcp_drain on the UI thread.</summary>
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static IntPtr OnNewDocument(IntPtr context, IntPtr name)
    {
        try
        {
            string title = Marshal.PtrToStringUTF8(name) ?? "Untitled";
            if (string.IsNullOrWhiteSpace(title)) title = "Untitled";
            var doc = _newDocument?.Invoke(title);
            return doc?.Handle ?? IntPtr.Zero;
        }
        catch (Exception e)
        {
            AppSettings.Log("MCP new_document: " + e);
            return IntPtr.Zero;
        }
    }
}
