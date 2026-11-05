// Registers .sculpt3d (and legacy .o3d) files with this exe for the current
// user (HKCU\Software\Classes), as unpackaged desktop apps do, so Explorer
// opens them in Sculpt3D. Re-registers when the exe moved.
using System;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace Sculpt3D.Core;

public static class FileAssociation
{
    private const string ProgId = "Sculpt3D.Document";

    public static void EnsureRegistered()
    {
        try
        {
            string? exe = Environment.ProcessPath;
            if (string.IsNullOrEmpty(exe) || !File.Exists(exe)) return;
            string command = $"\"{exe}\" \"%1\"";
            using var classes = Registry.CurrentUser.CreateSubKey(@"Software\Classes");
            if (classes == null) return;
            using (var existing = classes.OpenSubKey($@"{ProgId}\shell\open\command"))
            {
                if (existing?.GetValue(null) as string == command) return;
            }
            using (var prog = classes.CreateSubKey(ProgId))
            {
                prog.SetValue(null, "Sculpt3D Project");
                using (var icon = prog.CreateSubKey("DefaultIcon")) icon.SetValue(null, $"\"{exe}\",0");
                using (var cmd = prog.CreateSubKey(@"shell\open\command")) cmd.SetValue(null, command);
            }
            foreach (var ext in new[] { ".sculpt3d", ".o3d" })
            {
                using var key = classes.CreateSubKey(ext);
                // Don't take over an extension another app owns; offer Sculpt3D in "Open with".
                var current = key.GetValue(null) as string;
                if (string.IsNullOrEmpty(current) || current == ProgId) key.SetValue(null, ProgId);
                using var openWith = key.CreateSubKey("OpenWithProgids");
                openWith.SetValue(ProgId, Array.Empty<byte>(), RegistryValueKind.None);
            }
            SHChangeNotify(0x08000000 /* SHCNE_ASSOCCHANGED */, 0, IntPtr.Zero, IntPtr.Zero);
        }
        catch (Exception e)
        {
            AppSettings.Log("File association: " + e.Message);
        }
    }

    [DllImport("shell32.dll")]
    private static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
}
