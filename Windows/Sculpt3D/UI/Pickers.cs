// File open/save and folder pickers for an unpackaged desktop app: the WinRT
// pickers need the window handle (InitializeWithWindow).
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Windows.Storage.Pickers;

namespace Sculpt3D.UI;

public static class Pickers
{
    public static readonly string[] CadFiles = { ".step", ".stp", ".iges", ".igs", ".stl", ".brep", ".brp" };
    public static readonly string[] ProjectFiles = { ".sculpt3d", ".o3d" };
    public static readonly string[] ImageFiles = { ".png", ".jpg", ".jpeg" };

    public static async Task<IReadOnlyList<string>> OpenFilesAsync(MainWindow window, IEnumerable<string> extensions, bool multiple)
    {
        var picker = new FileOpenPicker { ViewMode = PickerViewMode.List, SuggestedStartLocation = PickerLocationId.DocumentsLibrary };
        foreach (var e in extensions) picker.FileTypeFilter.Add(e);
        WinRT.Interop.InitializeWithWindow.Initialize(picker, window.Hwnd);
        if (multiple)
        {
            var files = await picker.PickMultipleFilesAsync();
            return files?.Select(f => f.Path).Where(p => !string.IsNullOrEmpty(p)).ToList() ?? new List<string>();
        }
        var file = await picker.PickSingleFileAsync();
        return file != null && !string.IsNullOrEmpty(file.Path) ? new List<string> { file.Path } : new List<string>();
    }

    /// <summary>Returns the chosen path (the picker creates an empty file there), or null.</summary>
    public static async Task<string?> SaveFileAsync(MainWindow window, string suggestedName, string typeName, string extension)
    {
        var picker = new FileSavePicker { SuggestedStartLocation = PickerLocationId.DocumentsLibrary };
        picker.FileTypeChoices.Add(typeName, new List<string> { extension });
        picker.SuggestedFileName = Path.GetFileNameWithoutExtension(SafeFileName(suggestedName));
        picker.DefaultFileExtension = extension;
        WinRT.Interop.InitializeWithWindow.Initialize(picker, window.Hwnd);
        var file = await picker.PickSaveFileAsync();
        return file?.Path;
    }

    public static async Task<string?> PickFolderAsync(MainWindow window)
    {
        var picker = new FolderPicker { SuggestedStartLocation = PickerLocationId.DocumentsLibrary };
        picker.FileTypeFilter.Add("*");
        WinRT.Interop.InitializeWithWindow.Initialize(picker, window.Hwnd);
        var folder = await picker.PickSingleFolderAsync();
        return folder?.Path;
    }

    public static string SafeFileName(string name)
    {
        foreach (char c in Path.GetInvalidFileNameChars()) name = name.Replace(c, '-');
        return string.IsNullOrWhiteSpace(name) ? "Untitled" : name;
    }
}
