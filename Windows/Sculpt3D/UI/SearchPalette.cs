// Ctrl+F palette that finds and runs any tool by name (SearchPalette in
// Panels.swift), backed by the core's tool.search.
using System.Linq;
using System.Text.Json.Nodes;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Sculpt3D.Core;
using Windows.System;

namespace Sculpt3D.UI;

public sealed class SearchPalette : UserControl
{
    private readonly DocumentView _view;
    private readonly TextBox _query = new() { PlaceholderText = "Search tools", FontSize = 18, BorderThickness = new Thickness(0) };
    private readonly StackPanel _results = new() { Spacing = 0 };
    private JsonNode? _first;

    public SearchPalette(DocumentView view)
    {
        _view = view;
        IsTabStop = false;
        _query.TextChanged += (_, _) => Search();
        _query.KeyDown += (_, e) =>
        {
            if (e.Key == VirtualKey.Enter)
            {
                e.Handled = true;
                if (_first != null) Run(_first.Str("id"));
            }
            else if (e.Key == VirtualKey.Escape)
            {
                e.Handled = true;
                _view.SetShowSearch(false);
            }
        };
        var header = Ui.HStack(8, Icons.Create("magnifyingglass", 16, Ui.Secondary), _query);
        _query.Width = 380;
        var stack = Ui.VStack(8, header, new ScrollViewer { Content = _results, MaxHeight = 360, VerticalScrollBarVisibility = ScrollBarVisibility.Auto });
        var card = Ui.Card(stack, 14, 18, opaque: true);
        card.Width = 440;
        card.Shadow = new ThemeShadow();
        card.Translation = new System.Numerics.Vector3(0, 0, 32);
        Content = card;
    }

    public void Open()
    {
        _query.Text = "";
        Search();
        _query.Focus(FocusState.Programmatic);
        // The palette may not be laid out yet the first time.
        DispatcherQueue.TryEnqueue(() => _query.Focus(FocusState.Programmatic));
    }

    private void Search()
    {
        var reply = _view.Doc.Command("tool.search", new JsonObject { ["query"] = _query.Text.Trim() });
        _results.Children.Clear();
        _first = null;
        var results = reply.Arr("results").ToList();
        foreach (var r in results)
        {
            bool enabled = r.Bool("enabled");
            if (enabled && _first == null) _first = r;
            string id = r.Str("id");
            var texts = Ui.VStack(1, Ui.Text(r.Str("title"), 14));
            if (r.Str("hint").Length > 0) texts.Children.Add(Ui.Text(r.Str("hint"), 11, null, Ui.Secondary));
            var icon = Icons.Create(r.Str("symbol"), 15);
            icon.Width = 22;
            var row = new Grid { ColumnSpacing = 10, Padding = new Thickness(6, 5, 6, 5) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.Children.Add(icon);
            Grid.SetColumn(texts, 1);
            row.Children.Add(texts);
            var right = Ui.HStack(6);
            if (r.StrOrNull("shortcut") is string sc) right.Children.Add(Ui.KeyCaps(sc));
            right.Children.Add(Ui.Text(r.Str("categoryTitle"), 11, null, Ui.Tertiary));
            Grid.SetColumn(right, 2);
            row.Children.Add(right);
            var button = Ui.Plain(row, () => Run(id), null, enabled);
            button.Opacity = enabled ? 1 : 0.45;
            _results.Children.Add(button);
        }
        if (results.Count == 0)
        {
            var none = Ui.Text($"No tool matches “{_query.Text}”", 13, null, Ui.Secondary);
            none.Margin = new Thickness(12);
            _results.Children.Add(none);
        }
    }

    private void Run(string id)
    {
        _view.SetShowSearch(false);
        _view.RunTool(id);
    }
}
