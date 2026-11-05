// P/Invoke declarations for sculpt.dll: the shared core's C API
// (AppCore/include/sculpt/sculpt_c.h) and the Windows ANGLE host
// (Windows/native/sculpt_win.h). Source-generated with [LibraryImport].
//
// Conventions (see AppCore/API.md):
// - Strings cross as UTF-8. Every char*/uint8_t* the core returns is owned by
//   the caller and released with sculpt_free; the helpers below do that.
// - Callbacks are [UnmanagedCallersOnly] function pointers; their context is a
//   GCHandle to the managed object (see SculptDocument and McpService).
using System;
using System.Runtime.InteropServices;

namespace Sculpt3D.Native;

internal static unsafe partial class SculptNative
{
    private const string Lib = "sculpt";

    // ---- Phases, pointer types, buttons and modifiers (sculpt_c.h) ----
    public const int PhaseDown = 0, PhaseMove = 1, PhaseUp = 2, PhaseCancel = 3, PhaseHover = 4, PhaseExit = 5;
    public const int PointerTouch = 0, PointerMouse = 1, PointerPen = 2;
    public const int ButtonPrimary = 0, ButtonSecondary = 1, ButtonMiddle = 2;
    public const int ModShift = 1, ModControl = 2, ModAlt = 4;

    // ---- Windows host status codes (sculpt_win.h) ----
    public const int WinOk = 0, WinError = 1, WinDeviceLost = 2;

    // ---- Memory ----
    [LibraryImport(Lib)]
    public static partial void sculpt_free(IntPtr p);

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_version();

    // ---- Documents ----
    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr sculpt_doc_create(string name);

    [LibraryImport(Lib)]
    public static partial void sculpt_doc_destroy(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_doc_load(IntPtr doc, byte* data, nuint length);

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_doc_save(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial int sculpt_doc_is_dirty(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial void sculpt_doc_mark_saved(IntPtr doc);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial void sculpt_doc_set_name(IntPtr doc, string name);

    // ---- State and commands ----
    [LibraryImport(Lib)]
    public static partial ulong sculpt_doc_revision(IntPtr doc);

    /// <summary>listener: delegate* unmanaged[Cdecl]&lt;IntPtr, void&gt;</summary>
    [LibraryImport(Lib)]
    public static partial void sculpt_doc_set_listener(IntPtr doc, IntPtr listener, IntPtr context);

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_doc_state(IntPtr doc);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr sculpt_doc_command(IntPtr doc, string json);

    // ---- Binary file exchange ----
    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr sculpt_doc_export(IntPtr doc, string format, string? optionsJson, out nuint length, out IntPtr error);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr sculpt_doc_import_path(IntPtr doc, string path);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial IntPtr sculpt_doc_import_image(IntPtr doc, byte* data, nuint length, string name, int mode);

    // ---- Viewport ----
    [LibraryImport(Lib)]
    public static partial void sculpt_view_gl_init(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_gl_release(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_resize(IntPtr doc, int widthPx, int heightPx, float scale);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_render(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial int sculpt_view_needs_redraw(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_view_snapshot_png(IntPtr doc, int widthPx, int heightPx, out nuint length);

    [LibraryImport(Lib)]
    public static partial ulong sculpt_view_revision(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_view_annotations(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_pointer(IntPtr doc, int id, int pointerType, int button, int phase, float x, float y, int modifiers);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_orbit(IntPtr doc, float dx, float dy);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_pan(IntPtr doc, float dx, float dy);

    [LibraryImport(Lib)]
    public static partial void sculpt_view_zoom(IntPtr doc, float factor, float x, float y);

    [LibraryImport(Lib, StringMarshalling = StringMarshalling.Utf8)]
    public static partial int sculpt_view_key(IntPtr doc, string key, int modifiers);

    // ---- MCP server ----
    /// <summary>post: delegate* unmanaged[Cdecl]&lt;IntPtr, void&gt;</summary>
    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_mcp_start(int port, IntPtr post, IntPtr context);

    [LibraryImport(Lib)]
    public static partial void sculpt_mcp_stop();

    [LibraryImport(Lib)]
    public static partial int sculpt_mcp_running();

    [LibraryImport(Lib)]
    public static partial void sculpt_mcp_drain();

    [LibraryImport(Lib)]
    public static partial void sculpt_mcp_register(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial void sculpt_mcp_unregister(IntPtr doc);

    [LibraryImport(Lib)]
    public static partial void sculpt_mcp_activate(IntPtr doc);

    /// <summary>handler: delegate* unmanaged[Cdecl]&lt;IntPtr, IntPtr, IntPtr&gt; (context, const char* name) → SculptDoc*</summary>
    [LibraryImport(Lib)]
    public static partial void sculpt_mcp_set_new_document_handler(IntPtr handler, IntPtr context);

    // ---- Windows ANGLE host (Windows/native/AngleHost.cpp) ----
    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_win_host_create(IntPtr swapChainPanel, out IntPtr error);

    [LibraryImport(Lib)]
    public static partial void sculpt_win_host_destroy(IntPtr host);

    [LibraryImport(Lib)]
    public static partial int sculpt_win_host_resize(IntPtr host, int widthPx, int heightPx, float scaleX, float scaleY);

    [LibraryImport(Lib)]
    public static partial int sculpt_win_host_make_current(IntPtr host);

    [LibraryImport(Lib)]
    public static partial int sculpt_win_host_render(IntPtr host, IntPtr doc);

    [LibraryImport(Lib)]
    public static partial int sculpt_win_make_current();

    [LibraryImport(Lib)]
    public static partial ulong sculpt_win_device_generation();

    [LibraryImport(Lib)]
    public static partial void sculpt_win_reset_device();

    [LibraryImport(Lib)]
    public static partial IntPtr sculpt_win_last_error();

    [LibraryImport(Lib)]
    public static partial void sculpt_win_set_flip_y(int flip);

    // ---- Helpers ----

    /// <summary>Reads a core-allocated UTF-8 string and frees it. NULL gives null.</summary>
    public static string? TakeString(IntPtr p)
    {
        if (p == IntPtr.Zero) return null;
        try { return Marshal.PtrToStringUTF8(p); }
        finally { sculpt_free(p); }
    }

    /// <summary>Copies a core-allocated buffer and frees it.</summary>
    public static byte[]? TakeBytes(IntPtr p, nuint length)
    {
        if (p == IntPtr.Zero) return null;
        try
        {
            var bytes = new byte[checked((int)length)];
            if (bytes.Length > 0) Marshal.Copy(p, bytes, 0, bytes.Length);
            return bytes;
        }
        finally { sculpt_free(p); }
    }

    public static string Version
    {
        get
        {
            IntPtr p = sculpt_version();
            return p == IntPtr.Zero ? "" : Marshal.PtrToStringUTF8(p) ?? "";
        }
    }

    public static string LastHostError => TakeString(sculpt_win_last_error()) ?? "";
}
