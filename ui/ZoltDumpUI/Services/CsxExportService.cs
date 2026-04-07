/*
 * Zolt-Dump - Real-time Godot Engine memory dumper.
 * Copyright (C) 2026  bbfox0703
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
using System.Text;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>
/// Generates Cheat Engine Structure Dissect (.CSX) XML export for Godot objects.
/// Produces flat field lists with composite type flattening and struct recursion.
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
        var sb = new StringBuilder();
        sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        sb.AppendLine("<Structures>");
        sb.AppendLine($"  <Structure Name=\"{EscapeXml(structName)}\" AutoFill=\"0\" AutoCreate=\"1\" DefaultHex=\"1\" AutoDestroy=\"0\" DoNotSaveLocal=\"0\" RLECompression=\"1\" AutoGuess=\"0\">");

        EmitCsxFields(sb, fields, prefix: "", depth: 0, maxDepth);

        sb.AppendLine("  </Structure>");
        sb.AppendLine("</Structures>");
        return await Task.FromResult(sb.ToString());
    }

    /// <summary>Recursively emit CSX elements with composite flattening and struct expansion.</summary>
    private static void EmitCsxFields(StringBuilder sb, IReadOnlyList<LiveFieldValue> fields,
        string prefix, int depth, int maxDepth)
    {
        foreach (var field in fields)
        {
            if (field.Offset < 0) continue;

            var fullName = string.IsNullOrEmpty(prefix) ? field.Name : $"{prefix}.{field.Name}";
            var upperType = field.Type.ToUpperInvariant();

            // Composite types: flatten with dot-notation child names
            if (CeXmlExportService.CompositeChildren.TryGetValue(upperType, out var children))
            {
                bool isIntType = upperType.EndsWith('I');
                string vartype = isIntType ? "4 Bytes" : "Float";
                int elemSize = 4;

                for (int i = 0; i < children.Length; i++)
                {
                    var childOffset = field.Offset + i * elemSize;
                    var displayMethod = isIntType ? "signed integer" : "";
                    sb.AppendLine($"    <Element Offset=\"{childOffset}\" Vartype=\"{vartype}\" Bytesize=\"{elemSize}\" Description=\"{EscapeXml($"{fullName}.{children[i]}")}\" DisplayMethod=\"{displayMethod}\" />");
                }
            }
            // Inline struct with sub-fields: recurse with depth limit
            else if (field.IsStruct && field.SubFields.Count > 0 && depth < maxDepth)
            {
                EmitCsxFields(sb, field.SubFields, fullName, depth + 1, maxDepth);
            }
            // Pointer fields: Vartype="Pointer"
            else if (field.IsPointer || upperType == "OBJECT")
            {
                sb.AppendLine($"    <Element Offset=\"{field.Offset}\" Vartype=\"Pointer\" Bytesize=\"8\" Description=\"{EscapeXml(fullName)}\" DisplayMethod=\"hexadecimal\" />");
            }
            // Regular scalar fields
            else
            {
                var vartype = CeXmlExportService.MapVariantToCeType(field.Type, field.Size);
                var bytesize = field.Size > 0 ? field.Size : GetDefaultSize(vartype);
                sb.AppendLine($"    <Element Offset=\"{field.Offset}\" Vartype=\"{vartype}\" Bytesize=\"{bytesize}\" Description=\"{EscapeXml(fullName)}\" DisplayMethod=\"unsigned integer\" />");
            }
        }
    }

    private static int GetDefaultSize(string vartype) => vartype switch
    {
        "Byte" => 1,
        "2 Bytes" => 2,
        "4 Bytes" or "Float" => 4,
        "8 Bytes" or "Double" or "Pointer" => 8,
        "String" => 8,
        _ => 4
    };

    private static string EscapeXml(string s) => s
        .Replace("&", "&amp;")
        .Replace("<", "&lt;")
        .Replace(">", "&gt;")
        .Replace("\"", "&quot;");
}
