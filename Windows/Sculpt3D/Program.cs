// Entry point: single instance (a second launch, e.g. double-clicking a
// .sculpt3d file in Explorer, hands its file to the running window), then the
// WinUI application.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.AppLifecycle;

namespace Sculpt3D;

public static class Program
{
    private const string InstanceKey = "Sculpt3D-main";

    /// <summary>Files passed on the command line, opened once the window exists.</summary>
    internal static readonly List<string> StartupFiles = new();

    [STAThread]
    public static int Main(string[] args)
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        StartupFiles.AddRange(FilesFrom(args));
        if (RedirectToRunningInstance()) return 0;
        Application.Start(p =>
        {
            var context = new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread());
            SynchronizationContext.SetSynchronizationContext(context);
            _ = new App();
        });
        return 0;
    }

    internal static IEnumerable<string> FilesFrom(IEnumerable<string> args) =>
        args.Where(a => !string.IsNullOrWhiteSpace(a) && !a.StartsWith("-", StringComparison.Ordinal))
            .Where(a => a.EndsWith(".sculpt3d", StringComparison.OrdinalIgnoreCase) || a.EndsWith(".o3d", StringComparison.OrdinalIgnoreCase))
            .Where(System.IO.File.Exists);

    /// <summary>Splits a command line (as a redirected launch delivers it) into arguments.</summary>
    internal static IEnumerable<string> SplitCommandLine(string commandLine)
    {
        var parts = new List<string>();
        var current = new System.Text.StringBuilder();
        bool quoted = false;
        foreach (char c in commandLine)
        {
            if (c == '"') { quoted = !quoted; continue; }
            if (char.IsWhiteSpace(c) && !quoted)
            {
                if (current.Length > 0) { parts.Add(current.ToString()); current.Clear(); }
                continue;
            }
            current.Append(c);
        }
        if (current.Length > 0) parts.Add(current.ToString());
        return parts;
    }

    private static bool RedirectToRunningInstance()
    {
        try
        {
            var activation = AppInstance.GetCurrent().GetActivatedEventArgs();
            var main = AppInstance.FindOrRegisterForKey(InstanceKey);
            if (main.IsCurrent)
            {
                main.Activated += OnActivated;
                return false;
            }
            // Hand the activation (with its command line) to the running window,
            // pumping COM while waiting (the documented pattern for redirection).
            IntPtr done = CreateEvent(IntPtr.Zero, true, false, null);
            Task.Run(() =>
            {
                main.RedirectActivationToAsync(activation).AsTask().Wait();
                SetEvent(done);
            });
            _ = CoWaitForMultipleObjects(0, 0xFFFFFFFF, 1, new[] { done }, out _);
            try
            {
                var process = Process.GetProcessById((int)main.ProcessId);
                SetForegroundWindow(process.MainWindowHandle);
            }
            catch
            {
            }
            return true;
        }
        catch (Exception e)
        {
            // AppLifecycle unavailable: just run another instance.
            Core.AppSettings.Log("Single instance: " + e.Message);
            return false;
        }
    }

    private static void OnActivated(object? sender, AppActivationArguments args)
    {
        // Raised on a background thread when another launch redirected to us.
        var files = new List<string>();
        if (args.Kind == ExtendedActivationKind.Launch && args.Data is Windows.ApplicationModel.Activation.ILaunchActivatedEventArgs launch)
            files.AddRange(FilesFrom(SplitCommandLine(launch.Arguments ?? "")));
        else if (args.Kind == ExtendedActivationKind.File && args.Data is Windows.ApplicationModel.Activation.IFileActivatedEventArgs file)
            files.AddRange(file.Files.Select(f => f.Path).Where(p => !string.IsNullOrEmpty(p)));
        App.Current?.OnRedirectedActivation(files);
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateEvent(IntPtr attributes, bool manualReset, bool initialState, string? name);

    [DllImport("kernel32.dll")]
    private static extern bool SetEvent(IntPtr handle);

    [DllImport("ole32.dll")]
    private static extern uint CoWaitForMultipleObjects(uint flags, uint timeout, ulong count, IntPtr[] handles, out uint index);

    [DllImport("user32.dll")]
    private static extern bool SetForegroundWindow(IntPtr hwnd);
}
