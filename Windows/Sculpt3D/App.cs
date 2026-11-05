// The WinUI application, built without App.xaml: it supplies the XAML type
// metadata of the WinUI controls itself and merges XamlControlsResources.
using System;
using System.Collections.Generic;
using System.Linq;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.XamlTypeInfo;
using Sculpt3D.Core;
using Sculpt3D.UI;

namespace Sculpt3D;

public sealed class App : Application, IXamlMetadataProvider
{
    private readonly XamlControlsXamlMetaDataProvider _metadata = new();
    private MainWindow? _window;

    public static new App? Current => Application.Current as App;

    public App()
    {
        UnhandledException += (_, e) =>
        {
            AppSettings.Log("Unhandled: " + e.Exception);
            e.Handled = true;
        };
    }

    public IXamlType GetXamlType(Type type) => _metadata.GetXamlType(type);
    public IXamlType GetXamlType(string fullName) => _metadata.GetXamlType(fullName);
    public XmlnsDefinition[] GetXmlnsDefinitions() => _metadata.GetXmlnsDefinitions();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Resources.MergedDictionaries.Add(new XamlControlsResources());
        FileAssociation.EnsureRegistered();

        _window = new MainWindow();
        _window.Activate();

        // sculpt.dll (and the ANGLE DLLs it imports) must sit next to the exe.
        string? nativeError = null;
        try
        {
            _ = Sculpt3D.Native.SculptNative.Version;
            var window = _window;
            McpService.Initialize(window.DispatcherQueue, name =>
            {
                // MCP new_document: open a window (tab) for it.
                var view = window.NewDocument(name);
                return view.Doc;
            });
            if (AppSettings.Current.McpEnabled) McpService.Start();
        }
        catch (Exception e) when (e is DllNotFoundException || e is EntryPointNotFoundException || e is BadImageFormatException)
        {
            nativeError = e.Message;
            AppSettings.Log("Native core: " + e);
        }
        if (nativeError != null)
        {
            var window = _window;
            ((FrameworkElement)window.Content).Loaded += async (_, _) =>
                await Dialogs.MessageAsync(window.Content.XamlRoot, "Sculpt3D can't start its core",
                    "sculpt.dll, libEGL.dll and libGLESv2.dll must be next to Sculpt3D.exe (scripts\\build_windows.ps1 copies them).\n\n" + nativeError);
            return;
        }
        foreach (var file in Program.StartupFiles) _window.OpenPath(file);
    }

    /// <summary>Another launch handed us its files (on a background thread).</summary>
    internal void OnRedirectedActivation(IReadOnlyList<string> files)
    {
        var window = _window;
        window?.DispatcherQueue.TryEnqueue(() =>
        {
            foreach (var f in files) window.OpenPath(f);
            window.Activate();
        });
    }
}
