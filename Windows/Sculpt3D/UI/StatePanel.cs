// Base for the panels around the viewport: each one reads the document state
// and rebuilds its content only when the part of the state it shows changed
// (its Signature), so text fields keep focus and typing isn't interrupted.
using System.Text.Json.Nodes;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Sculpt3D.Core;

namespace Sculpt3D.UI;

public abstract class StatePanel : UserControl
{
    protected readonly DocumentView V;
    private string _signature = "\u0000";

    protected StatePanel(DocumentView view)
    {
        V = view;
        IsTabStop = false;
    }

    protected JsonNode S => V.Doc.State;

    /// <summary>Rebuilds when the signature changed, else refreshes in place.</summary>
    public void Update()
    {
        string sig = Signature() + "|" + V.ThemeKey;
        if (sig != _signature && SliderBinding.Dragging > 0 && Content != null)
        {
            // Don't pull a slider out from under the pointer: rebuild when the drag ends.
            Refresh();
            return;
        }
        if (sig != _signature)
        {
            _signature = sig;
            var content = Build();
            Content = content;
            Visibility = content == null ? Visibility.Collapsed : Visibility.Visible;
        }
        else
        {
            Refresh();
        }
    }

    /// <summary>Forces the next Update to rebuild (theme change, UI flags).</summary>
    public void Invalidate() => _signature = "\u0000";

    /// <summary>What this panel shows; a change rebuilds it.</summary>
    protected abstract string Signature();

    /// <summary>The content, or null to hide the panel.</summary>
    protected abstract UIElement? Build();

    /// <summary>In-place updates while the structure is unchanged (field values).</summary>
    protected virtual void Refresh()
    {
    }
}
