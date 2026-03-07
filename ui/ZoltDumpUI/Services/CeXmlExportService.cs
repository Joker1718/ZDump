using System.Text;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Services;

/// <summary>
/// Generates Cheat Engine XML address records with hierarchical pointer chains.
/// Adapts UE5CEDumper's CE XML export for Godot's SceneTree/Node/Variant model.
/// </summary>
public sealed class CeXmlExportService
{
    private readonly IDumpService _dump;

    public CeXmlExportService(IDumpService dump)
    {
        _dump = dump;
    }

    /// <summary>Generate hierarchical CE XML from breadcrumb chain + fields.</summary>
    public string GenerateHierarchicalXml(
        List<BreadcrumbEntry> breadcrumbs,
        List<LiveFieldValue> fields,
        string moduleName,
        AddressFormat addrFormat)
    {
        // TODO: Implement full hierarchical CE XML generation
        // Rules:
        // - Root: absolute address, GroupHeader=1
        // - Pointer nodes: Address=+{offset}, Offsets=[0]
        // - Inline structs: Address=+{offset}, NO Offsets
        // - Leaf fields: Address=+{offset}, proper VariableType
        // - NEVER emit Address=+0, Offsets=[fieldOffset] (double-deref bug)
        var sb = new StringBuilder();
        sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        sb.AppendLine("<CheatTable>");
        sb.AppendLine("  <CheatEntries>");
        sb.AppendLine("    <!-- TODO: Generate entries -->");
        sb.AppendLine("  </CheatEntries>");
        sb.AppendLine("</CheatTable>");
        return sb.ToString();
    }

    /// <summary>Generate standalone instance CE XML.</summary>
    public string GenerateInstanceXml(
        string address,
        string name,
        List<LiveFieldValue> fields,
        string moduleName)
    {
        // TODO: Implement instance XML
        return $"<!-- Instance: {name} @ {address} -->";
    }

    /// <summary>Map Godot Variant type to CE VariableType string.</summary>
    public static string MapVariantToCeType(string godotType, int size) => godotType switch
    {
        "BOOL" or "bool" => "Byte",
        "INT" or "int" when size <= 1 => "Byte",
        "INT" or "int" when size <= 2 => "2 Bytes",
        "INT" or "int" when size <= 4 => "4 Bytes",
        "INT" or "int" => "8 Bytes",
        "FLOAT" or "float" when size <= 4 => "Float",
        "FLOAT" or "float" => "Double",
        "STRING" or "STRING_NAME" or "NODE_PATH" => "String",
        "OBJECT" => "Pointer",
        "VECTOR2" or "VECTOR3" or "VECTOR4" or "COLOR"
            or "TRANSFORM2D" or "TRANSFORM3D" or "BASIS"
            or "QUATERNION" or "AABB" or "PLANE" or "RECT2" => "Group",
        _ => "4 Bytes"
    };
}
