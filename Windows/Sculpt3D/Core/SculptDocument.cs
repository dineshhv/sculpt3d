// One open document: the native SculptDoc, its JSON state, commands, file I/O
// and the change listener (marshalled to the UI thread).
using System;
using System.IO;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json.Nodes;
using System.Threading;
using Microsoft.UI.Dispatching;
using Sculpt3D.Native;

namespace Sculpt3D.Core;

public sealed unsafe class SculptDocument : IDisposable
{
    private IntPtr _doc;
    private GCHandle _self;
    private readonly DispatcherQueue _dispatcher;
    private int _notifyQueued;
    private ulong _stateRevision = ulong.MaxValue;
    private ulong _viewRevision = ulong.MaxValue;

    /// <summary>The latest state snapshot (AppCore/API.md "State").</summary>
    public JsonNode State { get; private set; } = new JsonObject();

    /// <summary>The latest annotations (AppCore/API.md "Annotations").</summary>
    public JsonNode Annotations { get; private set; } = new JsonObject();

    /// <summary>The file this document was opened from or saved to; null for a new document.</summary>
    public string? FilePath { get; private set; }

    /// <summary>Title shown in the tab: the file name without extension, or "Untitled".</summary>
    public string Name { get; private set; }

    /// <summary>Raised on the UI thread after the state was re-read.</summary>
    public event Action? StateChanged;

    /// <summary>Raised on the UI thread when the view needs a frame (camera, model or overlay changed).</summary>
    public event Action? FrameRequested;

    /// <summary>Raised on the UI thread after the annotations were re-read.</summary>
    public event Action? AnnotationsChanged;

    public IntPtr Handle => _doc;

    /// <summary>Device generation this document's renderer was initialised for (ViewportControl).</summary>
    internal ulong GlGeneration { get; set; }

    public SculptDocument(DispatcherQueue dispatcher, string name)
    {
        _dispatcher = dispatcher;
        Name = name;
        _doc = SculptNative.sculpt_doc_create(name);
        if (_doc == IntPtr.Zero) throw new InvalidOperationException("sculpt_doc_create failed");
        _self = GCHandle.Alloc(this);
        SculptNative.sculpt_doc_set_listener(_doc, (IntPtr)(delegate* unmanaged[Cdecl]<IntPtr, void>)&OnNativeChange, GCHandle.ToIntPtr(_self));
        RefreshState(force: true);
    }

    // ---- Listener -------------------------------------------------------

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void OnNativeChange(IntPtr context)
    {
        try
        {
            if (GCHandle.FromIntPtr(context).Target is SculptDocument doc) doc.QueueNotify();
        }
        catch
        {
            // Never let an exception cross into native code.
        }
    }

    private void QueueNotify()
    {
        // Coalesce: one queued UI callback at a time. Must only post (never block).
        if (Interlocked.Exchange(ref _notifyQueued, 1) == 1) return;
        _dispatcher.TryEnqueue(DispatcherQueuePriority.Normal, () =>
        {
            Interlocked.Exchange(ref _notifyQueued, 0);
            if (_doc == IntPtr.Zero) return;
            RefreshState(force: false);
            FrameRequested?.Invoke();
        });
    }

    /// <summary>Re-reads the state when the document revision moved (or always when forced).</summary>
    public void RefreshState(bool force)
    {
        if (_doc == IntPtr.Zero) return;
        ulong rev = SculptNative.sculpt_doc_revision(_doc);
        if (!force && rev == _stateRevision) return;
        var text = SculptNative.TakeString(SculptNative.sculpt_doc_state(_doc));
        var node = J.Parse(text);
        if (node == null) return;
        _stateRevision = rev;
        State = node;
        StateChanged?.Invoke();
    }

    /// <summary>Re-reads the annotations when the view revision moved (after a frame).</summary>
    public void RefreshAnnotations(bool force = false)
    {
        if (_doc == IntPtr.Zero) return;
        ulong rev = SculptNative.sculpt_view_revision(_doc);
        if (!force && rev == _viewRevision) return;
        var node = J.Parse(SculptNative.TakeString(SculptNative.sculpt_view_annotations(_doc)));
        if (node == null) return;
        _viewRevision = rev;
        Annotations = node;
        AnnotationsChanged?.Invoke();
    }

    // ---- Commands -------------------------------------------------------

    /// <summary>Runs {"cmd": name, ...args}. Returns the reply (never null).</summary>
    public JsonNode Command(string cmd, JsonObject? args = null)
    {
        var o = args ?? new JsonObject();
        o["cmd"] = cmd;
        return CommandJson(o.ToJsonString());
    }

    public JsonNode CommandJson(string json)
    {
        if (_doc == IntPtr.Zero) return new JsonObject { ["ok"] = false, ["error"] = "Document closed" };
        var text = SculptNative.TakeString(SculptNative.sculpt_doc_command(_doc, json));
        var reply = J.Parse(text) ?? new JsonObject { ["ok"] = false, ["error"] = "Bad reply" };
        // Commands change the state synchronously; the listener may also post.
        RefreshState(force: false);
        FrameRequested?.Invoke();
        return reply;
    }

    public bool ActionEnabled(string id) => State.At("actions").Bool(id);

    // ---- Files ----------------------------------------------------------

    public bool IsDirty => _doc != IntPtr.Zero && SculptNative.sculpt_doc_is_dirty(_doc) != 0;

    /// <summary>Loads a .sculpt3d / .o3d file. Returns an error message or null.</summary>
    public string? Load(string path)
    {
        byte[] bytes;
        try { bytes = File.ReadAllBytes(path); }
        catch (Exception e) { return e.Message; }
        string? error;
        fixed (byte* p = bytes)
        {
            error = SculptNative.TakeString(SculptNative.sculpt_doc_load(_doc, p, (nuint)bytes.Length));
        }
        if (error != null) return error;
        SetPath(path);
        RefreshState(force: true);
        return null;
    }

    /// <summary>Writes the document as .sculpt3d JSON. Returns an error message or null.</summary>
    public string? Save(string path)
    {
        var json = SculptNative.TakeString(SculptNative.sculpt_doc_save(_doc));
        if (json == null) return "The document couldn't be encoded";
        try
        {
            // Write next to the target, then replace, so a failed write keeps the old file.
            string tmp = path + ".saving";
            File.WriteAllText(tmp, json, new System.Text.UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(tmp, path, null);
            else File.Move(tmp, path);
        }
        catch (Exception e) { return e.Message; }
        SculptNative.sculpt_doc_mark_saved(_doc);
        SetPath(path);
        RefreshState(force: true);
        return null;
    }

    private void SetPath(string path)
    {
        FilePath = path;
        Name = System.IO.Path.GetFileNameWithoutExtension(path);
        SculptNative.sculpt_doc_set_name(_doc, Name);
        Command("document.setPath", new JsonObject { ["path"] = path });
    }

    public void Rename(string name)
    {
        Name = name;
        SculptNative.sculpt_doc_set_name(_doc, name);
        RefreshState(force: true);
    }

    /// <summary>Exports in a format id (see API.md). Returns bytes or sets error.</summary>
    public byte[]? Export(string format, string? optionsJson, out string? error)
    {
        IntPtr data = SculptNative.sculpt_doc_export(_doc, format, optionsJson, out nuint length, out IntPtr err);
        error = SculptNative.TakeString(err);
        var bytes = SculptNative.TakeBytes(data, length);
        if (bytes == null && error == null) error = "Nothing to export";
        RefreshState(force: false);
        return bytes;
    }

    public string? ImportPath(string path)
    {
        var e = SculptNative.TakeString(SculptNative.sculpt_doc_import_path(_doc, path));
        RefreshState(force: false);
        FrameRequested?.Invoke();
        return e;
    }

    /// <summary>mode 0 = reference image, 1 = decal, 2 = terrain height map.</summary>
    public string? ImportImage(byte[] bytes, string name, int mode)
    {
        string? e;
        fixed (byte* p = bytes)
        {
            e = SculptNative.TakeString(SculptNative.sculpt_doc_import_image(_doc, p, (nuint)bytes.Length, name, mode));
        }
        RefreshState(force: false);
        FrameRequested?.Invoke();
        return e;
    }

    // ---- MCP ----------------------------------------------------------------

    public void RegisterMcp() => SculptNative.sculpt_mcp_register(_doc);
    public void ActivateMcp() => SculptNative.sculpt_mcp_activate(_doc);

    public void Dispose()
    {
        if (_doc == IntPtr.Zero) return;
        SculptNative.sculpt_mcp_unregister(_doc);
        SculptNative.sculpt_doc_set_listener(_doc, IntPtr.Zero, IntPtr.Zero);
        SculptNative.sculpt_doc_destroy(_doc);
        _doc = IntPtr.Zero;
        if (_self.IsAllocated) _self.Free();
        StateChanged = null;
        FrameRequested = null;
        AnnotationsChanged = null;
    }
}
