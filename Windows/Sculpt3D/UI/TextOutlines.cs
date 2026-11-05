// Sketch ▸ Text: glyph outlines from DirectWrite (through Win2D's
// CanvasGeometry.CreateText), sent to the core as sketch.addOutlines paths in
// millimetres at the sketch origin, baseline on y = 0 (y up).
using System.Numerics;
using System.Text.Json.Nodes;
using Microsoft.Graphics.Canvas;
using Microsoft.Graphics.Canvas.Geometry;
using Microsoft.Graphics.Canvas.Text;

namespace Sculpt3D.UI;

public static class TextOutlines
{
    /// <summary>Lays out `text` with the font at `height` mm (the font size, as
    /// on the Mac) and returns the outline path for sketch.addOutlines.</summary>
    public static JsonArray Build(string text, string fontFamily, bool bold, bool italic, double height)
    {
        var device = CanvasDevice.GetSharedDevice();
        using var format = new CanvasTextFormat
        {
            FontFamily = fontFamily,
            FontSize = (float)height,
            FontWeight = bold ? Microsoft.UI.Text.FontWeights.Bold : Microsoft.UI.Text.FontWeights.Normal,
            FontStyle = italic ? Windows.UI.Text.FontStyle.Italic : Windows.UI.Text.FontStyle.Normal,
            WordWrapping = CanvasWordWrapping.NoWrap,
        };
        using var layout = new CanvasTextLayout(device, text, format, 100000f, 100000f);
        float baseline = layout.LineMetrics.Length > 0 ? layout.LineMetrics[0].Baseline : (float)height * 0.8f;
        using var geometry = CanvasGeometry.CreateText(layout);
        var receiver = new OutlineReceiver(baseline);
        geometry.SendPathTo(receiver);
        return receiver.Path;
    }

    /// <summary>Collects the path, flipping y so the baseline sits on y = 0.</summary>
    private sealed partial class OutlineReceiver : ICanvasPathReceiver
    {
        private readonly float _baseline;
        private Vector2 _current;
        public JsonArray Path { get; } = new();

        public OutlineReceiver(float baseline) => _baseline = baseline;

        private JsonArray P(Vector2 v) => new(Round(v.X), Round(_baseline - v.Y));
        private static double Round(float v) => System.Math.Round(v, 4);

        private void Add(string op, params Vector2[] points)
        {
            var pts = new JsonArray();
            foreach (var p in points) pts.Add(P(p));
            var e = new JsonObject { ["op"] = op };
            if (points.Length > 0) e["points"] = pts;
            Path.Add(e);
        }

        public void BeginFigure(Vector2 startPoint, CanvasFigureFill figureFill)
        {
            _current = startPoint;
            Add("move", startPoint);
        }

        public void AddArc(Vector2 endPoint, float radiusX, float radiusY, float rotationAngle, CanvasSweepDirection sweepDirection, CanvasArcSize arcSize)
        {
            // Glyph outlines don't contain arcs; a chord keeps the path valid.
            _current = endPoint;
            Add("line", endPoint);
        }

        public void AddCubicBezier(Vector2 controlPoint1, Vector2 controlPoint2, Vector2 endPoint)
        {
            _current = endPoint;
            Add("cubic", controlPoint1, controlPoint2, endPoint);
        }

        public void AddLine(Vector2 endPoint)
        {
            _current = endPoint;
            Add("line", endPoint);
        }

        public void AddQuadraticBezier(Vector2 controlPoint, Vector2 endPoint)
        {
            _current = endPoint;
            Add("quad", controlPoint, endPoint);
        }

        public void SetFilledRegionDetermination(CanvasFilledRegionDetermination filledRegionDetermination)
        {
        }

        public void SetSegmentOptions(CanvasFigureSegmentOptions figureSegmentOptions)
        {
        }

        public void EndFigure(CanvasFigureLoop figureLoop)
        {
            Add("close");
        }
    }
}
