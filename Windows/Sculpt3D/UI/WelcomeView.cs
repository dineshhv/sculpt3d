// The first page: the logo with New and Open on the left, recent documents on
// the right (WelcomeView.swift). Shown at launch and whenever no document is open.
using System;
using System.IO;
using System.Linq;
using Microsoft.UI;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Sculpt3D.Core;

namespace Sculpt3D.UI;

public sealed class WelcomeView : Grid
{
    private readonly MainWindow _main;
    private readonly StackPanel _recent = new() { Spacing = 2 };

    public WelcomeView(MainWindow main)
    {
        _main = main;
        ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(300) });

        var brand = new LinearGradientBrush { StartPoint = new Windows.Foundation.Point(0.5, 0), EndPoint = new Windows.Foundation.Point(0.5, 1) };
        brand.GradientStops.Add(new GradientStop { Color = Colors.White, Offset = 0 });
        brand.GradientStops.Add(new GradientStop { Color = ColorHelper.FromArgb(255, 232, 237, 247), Offset = 1 });

        var mark = Icons.Create("sculpt3d.project", 88);
        mark.HorizontalAlignment = HorizontalAlignment.Center;
        var name = new TextBlock
        {
            Text = "Sculpt3D",
            FontSize = 46,
            FontWeight = FontWeights.Bold,
            Foreground = new SolidColorBrush(ColorHelper.FromArgb(255, 5, 26, 102)),
            HorizontalAlignment = HorizontalAlignment.Center,
        };
        var version = new TextBlock
        {
            Text = "Version " + (typeof(WelcomeView).Assembly.GetName().Version?.ToString(3) ?? "1.0.0"),
            Foreground = new SolidColorBrush(Colors.Gray),
            HorizontalAlignment = HorizontalAlignment.Center,
        };
        var newButton = Ui.Button("New Document", () => main.NewDocument(), "plus", accent: true, tooltip: "Ctrl+N");
        newButton.Width = 230;
        newButton.HorizontalContentAlignment = HorizontalAlignment.Center;
        var openButton = Ui.Button("Open…", () => _ = main.OpenAsync(), "folder", tooltip: "Ctrl+O");
        openButton.Width = 230;
        openButton.HorizontalContentAlignment = HorizontalAlignment.Center;
        var left = new StackPanel
        {
            Spacing = 18,
            VerticalAlignment = VerticalAlignment.Center,
            HorizontalAlignment = HorizontalAlignment.Center,
        };
        left.Children.Add(mark);
        left.Children.Add(name);
        left.Children.Add(version);
        left.Children.Add(Ui.VStack(10, newButton, openButton));
        var leftHost = new Grid { Background = brand, RequestedTheme = ElementTheme.Light };
        leftHost.Children.Add(left);
        Children.Add(leftHost);

        var title = Ui.Headline("Recent");
        title.Margin = new Thickness(16, 40, 16, 8);
        var right = new Grid { Background = Ui.Brush("SolidBackgroundFillColorBaseBrush") };
        right.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        right.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        right.Children.Add(title);
        var scroll = new ScrollViewer { Content = _recent, Padding = new Thickness(8, 0, 8, 8) };
        Grid.SetRow(scroll, 1);
        right.Children.Add(scroll);
        Grid.SetColumn(right, 1);
        Children.Add(right);
        Loaded += (_, _) => Reload();
    }

    public void Reload()
    {
        _recent.Children.Clear();
        var files = AppSettings.Current.ExistingRecentFiles().ToList();
        if (files.Count == 0)
        {
            var empty = Ui.Text("Documents you open will appear here.", 13, null, Ui.Secondary, wrap: true);
            empty.Margin = new Thickness(8, 0, 8, 0);
            _recent.Children.Add(empty);
            return;
        }
        string home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        foreach (var path in files)
        {
            string folder = Path.GetDirectoryName(path) ?? "";
            if (folder.StartsWith(home, StringComparison.OrdinalIgnoreCase)) folder = "~" + folder.Substring(home.Length);
            var texts = Ui.VStack(2, Ui.Text(Path.GetFileNameWithoutExtension(path), 13), Ui.Text(folder, 11, null, Ui.Secondary));
            var icon = Icons.Create("sculpt3d.project", 22);
            var row = Ui.HStack(10, icon, texts);
            row.Padding = new Thickness(8, 6, 8, 6);
            var button = Ui.Plain(row, () => _main.OpenPath(path), path);
            button.HorizontalContentAlignment = HorizontalAlignment.Left;
            var menu = new MenuFlyout();
            menu.Items.Add(ItemsPanel.MenuItem("Open", () => _main.OpenPath(path)));
            menu.Items.Add(ItemsPanel.MenuItem("Show in Explorer", () =>
            {
                try { System.Diagnostics.Process.Start("explorer.exe", $"/select,\"{path}\""); } catch { }
            }));
            menu.Items.Add(ItemsPanel.MenuItem("Remove from Recent", () =>
            {
                AppSettings.Current.RecentFiles.Remove(path);
                AppSettings.Current.Save();
                Reload();
            }));
            button.ContextFlyout = menu;
            _recent.Children.Add(button);
        }
    }
}
