// Per-user app preferences (the Mac's UserDefaults): recent files, MCP server
// on/off, Quick Export favourites and folder, display formats. Stored as JSON
// in %LOCALAPPDATA%\Sculpt3D\settings.json (the app is unpackaged, so there is
// no ApplicationData container).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace Sculpt3D.Core;

public sealed class AppSettings
{
    public List<string> RecentFiles { get; set; } = new();
    public bool McpEnabled { get; set; } = true;
    public List<string> QuickExportFormats { get; set; } = new() { "threeMF", "step" };
    public string QuickExportFolder { get; set; } = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
    public string FootInchFormat { get; set; } = "";
    public string DegreeFormat { get; set; } = "";
    public bool ShowHistory { get; set; } = true;
    public bool ShowItems { get; set; }
    public bool ShowProperties { get; set; }
    public double WindowWidth { get; set; } = 1280;
    public double WindowHeight { get; set; } = 820;

    public static string Folder => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Sculpt3D");
    private static string FilePath => Path.Combine(Folder, "settings.json");

    public static AppSettings Current { get; } = Load();

    private static AppSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(FilePath)) ?? new AppSettings();
        }
        catch
        {
            // A broken settings file falls back to defaults.
        }
        return new AppSettings();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Preferences are best effort.
        }
    }

    public void AddRecent(string path)
    {
        RecentFiles.RemoveAll(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
        RecentFiles.Insert(0, path);
        if (RecentFiles.Count > 12) RecentFiles.RemoveRange(12, RecentFiles.Count - 12);
        Save();
    }

    public IEnumerable<string> ExistingRecentFiles() => RecentFiles.Where(File.Exists);

    public static void Log(string message)
    {
        try
        {
            Directory.CreateDirectory(Folder);
            File.AppendAllText(Path.Combine(Folder, "log.txt"), $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {message}{Environment.NewLine}");
        }
        catch
        {
        }
    }
}
