// Small helpers for reading the core's JSON state (AppCore/API.md "State")
// through System.Text.Json.Nodes without exceptions on missing fields.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace Sculpt3D.Core;

public static class J
{
    public static JsonNode? Parse(string? text)
    {
        if (string.IsNullOrEmpty(text)) return null;
        try { return JsonNode.Parse(text); }
        catch (JsonException) { return null; }
    }

    public static JsonNode? At(this JsonNode? n, string key) => n is JsonObject o && o.TryGetPropertyValue(key, out var v) ? v : null;

    public static JsonNode? At(this JsonNode? n, int index) => n is JsonArray a && index >= 0 && index < a.Count ? a[index] : null;

    public static bool IsNull(this JsonNode? n) => n is null;

    public static string Str(this JsonNode? n, string key, string fallback = "") => n.At(key).AsStr(fallback);

    public static string AsStr(this JsonNode? n, string fallback = "")
    {
        if (n is JsonValue v)
        {
            if (v.TryGetValue(out string? s) && s != null) return s;
            if (v.TryGetValue(out double d)) return d.ToString(CultureInfo.InvariantCulture);
            if (v.TryGetValue(out bool b)) return b ? "true" : "false";
        }
        return fallback;
    }

    public static string? StrOrNull(this JsonNode? n, string key)
    {
        var v = n.At(key);
        return v is JsonValue jv && jv.TryGetValue(out string? s) ? s : null;
    }

    public static bool Bool(this JsonNode? n, string key, bool fallback = false) => n.At(key).AsBool(fallback);

    public static bool AsBool(this JsonNode? n, bool fallback = false)
    {
        if (n is JsonValue v)
        {
            if (v.TryGetValue(out bool b)) return b;
            if (v.TryGetValue(out double d)) return d != 0;
        }
        return fallback;
    }

    public static double Num(this JsonNode? n, string key, double fallback = 0) => n.At(key).AsNum(fallback);

    public static double AsNum(this JsonNode? n, double fallback = 0)
    {
        if (n is JsonValue v)
        {
            if (v.TryGetValue(out double d)) return d;
            if (v.TryGetValue(out long l)) return l;
            if (v.TryGetValue(out int i)) return i;
            if (v.TryGetValue(out float f)) return f;
        }
        return fallback;
    }

    public static int Int(this JsonNode? n, string key, int fallback = 0) => (int)Math.Round(n.Num(key, fallback));

    public static int AsInt(this JsonNode? n, int fallback = 0) => (int)Math.Round(n.AsNum(fallback));

    public static int? IntOrNull(this JsonNode? n, string key)
    {
        var v = n.At(key);
        if (v is JsonValue jv && jv.TryGetValue(out double d)) return (int)Math.Round(d);
        return null;
    }

    public static double? NumOrNull(this JsonNode? n, string key)
    {
        var v = n.At(key);
        if (v is JsonValue jv && jv.TryGetValue(out double d)) return d;
        return null;
    }

    public static ulong ULong(this JsonNode? n, string key)
    {
        var v = n.At(key);
        if (v is JsonValue jv)
        {
            if (jv.TryGetValue(out ulong u)) return u;
            if (jv.TryGetValue(out double d)) return (ulong)Math.Max(0, d);
        }
        return 0;
    }

    /// <summary>The array at key (empty when missing).</summary>
    public static IEnumerable<JsonNode?> Arr(this JsonNode? n, string key) => n.At(key).Items();

    public static IEnumerable<JsonNode?> Items(this JsonNode? n) => n is JsonArray a ? a : Enumerable.Empty<JsonNode?>();

    public static int Count(this JsonNode? n, string key) => n.At(key) is JsonArray a ? a.Count : 0;

    /// <summary>[r,g,b] 0…1 as a color.</summary>
    public static Windows.UI.Color Rgb(this JsonNode? n, byte alpha = 255)
    {
        var a = n as JsonArray;
        double r = a.At(0).AsNum(0.6), g = a.At(1).AsNum(0.6), b = a.At(2).AsNum(0.6);
        static byte C(double v) => (byte)Math.Clamp((int)Math.Round(v * 255), 0, 255);
        return Microsoft.UI.ColorHelper.FromArgb(alpha, C(r), C(g), C(b));
    }

    public static (double X, double Y, double Z) Vec3(this JsonNode? n) => (n.At(0).AsNum(), n.At(1).AsNum(), n.At(2).AsNum());

    public static string ToJson(this JsonNode n) => n.ToJsonString();

    /// <summary>JSON array of numbers for a colour [r,g,b] 0…1.</summary>
    public static JsonArray ColorArray(Windows.UI.Color c) => new JsonArray(c.R / 255.0, c.G / 255.0, c.B / 255.0);

    /// <summary>A stable, cheap signature of a node for "did this part change" checks.</summary>
    public static string Sig(this JsonNode? n) => n?.ToJsonString() ?? "null";

    public static string Fmt(double v) => v.ToString("0.###", CultureInfo.CurrentCulture);
}
