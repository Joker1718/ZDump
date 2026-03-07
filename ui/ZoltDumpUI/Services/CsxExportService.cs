using System.Text;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>
/// Generates Cheat Engine Structure Dissect (.CSX) XML export for Godot objects.
/// </summary>
public sealed class CsxExportService
{
    private readonly IDumpService _dump;

    public CsxExportService(IDumpService dump)
    {
        _dump = dump;
    }

    /// <summary>Generate CSX from live walker fields.</summary>
    public async Task<string> GenerateCsxAsync(
        string structName,
        List<LiveFieldValue> fields,
        int maxDepth = 3,
        CancellationToken ct = default)
    {
        // TODO: Implement CSX generation with:
        // - Inline struct flattening
        // - Pointer drilldown (depth-limited)
        // - Container element expansion
        // - Cycle detection
        var sb = new StringBuilder();
        sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        sb.AppendLine("<Structures>");
        sb.AppendLine($"  <Structure Name=\"{EscapeXml(structName)}\" AutoFill=\"0\" AutoCreate=\"1\" DefaultHex=\"1\" AutoDestroy=\"0\" DoNotSaveLocal=\"0\" RLECompression=\"1\" AutoGuess=\"0\">");
        foreach (var field in fields)
        {
            sb.AppendLine($"    <Element Offset=\"{field.Offset}\" Vartype=\"{CeXmlExportService.MapVariantToCeType(field.Type, field.Size)}\" Bytesize=\"{field.Size}\" Description=\"{EscapeXml(field.Name)}\" DisplayMethod=\"unsigned integer\" />");
        }
        sb.AppendLine("  </Structure>");
        sb.AppendLine("</Structures>");
        return await Task.FromResult(sb.ToString());
    }

    private static string EscapeXml(string s) => s
        .Replace("&", "&amp;")
        .Replace("<", "&lt;")
        .Replace(">", "&gt;")
        .Replace("\"", "&quot;");
}
