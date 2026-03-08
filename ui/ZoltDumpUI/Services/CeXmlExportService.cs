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

    // ============================================================
    // Composite type child expansion map
    // ============================================================

    /// <summary>Maps Godot composite Variant types to their child field names (all float).</summary>
    internal static readonly Dictionary<string, string[]> CompositeChildren = new(StringComparer.OrdinalIgnoreCase)
    {
        ["VECTOR2"] = ["x", "y"],
        ["VECTOR2I"] = ["x", "y"],
        ["VECTOR3"] = ["x", "y", "z"],
        ["VECTOR3I"] = ["x", "y", "z"],
        ["VECTOR4"] = ["x", "y", "z", "w"],
        ["VECTOR4I"] = ["x", "y", "z", "w"],
        ["COLOR"] = ["r", "g", "b", "a"],
        ["QUATERNION"] = ["x", "y", "z", "w"],
        ["RECT2"] = ["x", "y", "w", "h"],
        ["RECT2I"] = ["x", "y", "w", "h"],
        ["AABB"] = ["pos.x", "pos.y", "pos.z", "size.x", "size.y", "size.z"],
        ["PLANE"] = ["normal.x", "normal.y", "normal.z", "d"],
        ["TRANSFORM2D"] = ["x.x", "x.y", "y.x", "y.y", "origin.x", "origin.y"],
        ["BASIS"] = ["x.x", "x.y", "x.z", "y.x", "y.y", "y.z", "z.x", "z.y", "z.z"],
        ["TRANSFORM3D"] = ["basis.x.x", "basis.x.y", "basis.x.z",
                           "basis.y.x", "basis.y.y", "basis.y.z",
                           "basis.z.x", "basis.z.y", "basis.z.z",
                           "origin.x", "origin.y", "origin.z"],
    };

    // ============================================================
    // XML helpers
    // ============================================================

    private static string EscapeXml(string s) => s
        .Replace("&", "&amp;")
        .Replace("<", "&lt;")
        .Replace(">", "&gt;")
        .Replace("\"", "&quot;")
        .Replace("'", "&apos;");

    private static string Indent(int depth) => new(' ', depth * 2);

    /// <summary>Remove navigation cycles from breadcrumb list.</summary>
    internal static List<BreadcrumbEntry> CleanBreadcrumbs(List<BreadcrumbEntry> raw)
    {
        var result = new List<BreadcrumbEntry>(raw);
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (int i = 0; i < result.Count; i++)
            {
                for (int j = i + 1; j < result.Count; j++)
                {
                    if (string.Equals(result[i].Address, result[j].Address, StringComparison.OrdinalIgnoreCase))
                    {
                        // Cycle detected: remove entries between i+1 and j (keep i, remove up to j)
                        result.RemoveRange(i + 1, j - i);
                        changed = true;
                        break;
                    }
                }
                if (changed) break;
            }
        }
        return result;
    }

    // ============================================================
    // XML emission helpers
    // ============================================================

    private static void EmitGroupOpen(StringBuilder sb, int depth, string description,
        string address, bool hasOffsets)
    {
        var ind = Indent(depth);
        sb.AppendLine($"{ind}<CheatEntry>");
        sb.AppendLine($"{ind}  <Description>\"{EscapeXml(description)}\"</Description>");
        sb.AppendLine($"{ind}  <Address>{address}</Address>");
        if (hasOffsets)
        {
            sb.AppendLine($"{ind}  <Offsets>");
            sb.AppendLine($"{ind}    <Offset>0</Offset>");
            sb.AppendLine($"{ind}  </Offsets>");
        }
        sb.AppendLine($"{ind}  <VariableType>8 Bytes</VariableType>");
        sb.AppendLine($"{ind}  <ShowAsHex>1</ShowAsHex>");
        sb.AppendLine($"{ind}  <GroupHeader>1</GroupHeader>");
        sb.AppendLine($"{ind}  <CheatEntries>");
    }

    private static void EmitGroupClose(StringBuilder sb, int depth)
    {
        var ind = Indent(depth);
        sb.AppendLine($"{ind}  </CheatEntries>");
        sb.AppendLine($"{ind}</CheatEntry>");
    }

    /// <summary>Emit a single leaf/scalar field.</summary>
    private static void EmitLeafField(StringBuilder sb, int depth, LiveFieldValue field)
    {
        if (field.Offset < 0) return;

        var ind = Indent(depth);
        var ceType = MapVariantToCeType(field.Type, field.Size);

        sb.AppendLine($"{ind}<CheatEntry>");
        sb.AppendLine($"{ind}  <Description>\"{EscapeXml(field.Name)} ({field.Type})\"</Description>");
        sb.AppendLine($"{ind}  <Address>+{field.Offset:X}</Address>");
        sb.AppendLine($"{ind}  <VariableType>{ceType}</VariableType>");
        if (ceType is "8 Bytes" or "Pointer")
            sb.AppendLine($"{ind}  <ShowAsHex>1</ShowAsHex>");
        sb.AppendLine($"{ind}</CheatEntry>");
    }

    /// <summary>Emit a composite type (Vector3, Color, etc.) as a Group with float children.</summary>
    private static void EmitCompositeField(StringBuilder sb, int depth, LiveFieldValue field)
    {
        if (field.Offset < 0) return;

        var upperType = field.Type.ToUpperInvariant();
        if (!CompositeChildren.TryGetValue(upperType, out var children))
        {
            EmitLeafField(sb, depth, field);
            return;
        }

        // Determine child element size and CE type
        bool isIntType = upperType.EndsWith('I'); // VECTOR2I, VECTOR3I, etc.
        int elemSize = isIntType ? 4 : 4; // both int32 and float are 4 bytes
        string elemCeType = isIntType ? "4 Bytes" : "Float";

        // Inline struct: NO Offsets (no pointer dereference)
        var ind = Indent(depth);
        sb.AppendLine($"{ind}<CheatEntry>");
        sb.AppendLine($"{ind}  <Description>\"{EscapeXml(field.Name)} ({field.Type})\"</Description>");
        sb.AppendLine($"{ind}  <Address>+{field.Offset:X}</Address>");
        sb.AppendLine($"{ind}  <GroupHeader>1</GroupHeader>");
        sb.AppendLine($"{ind}  <CheatEntries>");

        for (int i = 0; i < children.Length; i++)
        {
            var cind = Indent(depth + 1);
            sb.AppendLine($"{cind}<CheatEntry>");
            sb.AppendLine($"{cind}  <Description>\"{children[i]}\"</Description>");
            sb.AppendLine($"{cind}  <Address>+{i * elemSize:X}</Address>");
            sb.AppendLine($"{cind}  <VariableType>{elemCeType}</VariableType>");
            if (isIntType)
                sb.AppendLine($"{cind}  <ShowAsSigned>1</ShowAsSigned>");
            sb.AppendLine($"{cind}</CheatEntry>");
        }

        sb.AppendLine($"{ind}  </CheatEntries>");
        sb.AppendLine($"{ind}</CheatEntry>");
    }

    /// <summary>Emit an OBJECT pointer field with Offsets=[0] dereference.</summary>
    private static void EmitPointerField(StringBuilder sb, int depth, LiveFieldValue field)
    {
        if (field.Offset < 0) return;

        var ind = Indent(depth);
        var className = !string.IsNullOrEmpty(field.PtrClass)
            ? $" ({field.PtrClass})" : "";

        sb.AppendLine($"{ind}<CheatEntry>");
        sb.AppendLine($"{ind}  <Description>\"{EscapeXml(field.Name)}{EscapeXml(className)} *\"</Description>");
        sb.AppendLine($"{ind}  <Address>+{field.Offset:X}</Address>");
        sb.AppendLine($"{ind}  <Offsets>");
        sb.AppendLine($"{ind}    <Offset>0</Offset>");
        sb.AppendLine($"{ind}  </Offsets>");
        sb.AppendLine($"{ind}  <VariableType>8 Bytes</VariableType>");
        sb.AppendLine($"{ind}  <ShowAsHex>1</ShowAsHex>");
        sb.AppendLine($"{ind}  <GroupHeader>1</GroupHeader>");
        sb.AppendLine($"{ind}</CheatEntry>");
    }

    /// <summary>Emit a container (Array/Dictionary) as a Group with Offsets=[0] data pointer deref.</summary>
    private static void EmitContainerField(StringBuilder sb, int depth, LiveFieldValue field)
    {
        if (field.Offset < 0) return;

        var countStr = field.ElementCount > 0 ? $"[{field.ElementCount}]" : "";
        var ind = Indent(depth);

        sb.AppendLine($"{ind}<CheatEntry>");
        sb.AppendLine($"{ind}  <Description>\"{EscapeXml(field.Name)} ({field.Type}{countStr})\"</Description>");
        sb.AppendLine($"{ind}  <Address>+{field.Offset:X}</Address>");
        sb.AppendLine($"{ind}  <Offsets>");
        sb.AppendLine($"{ind}    <Offset>0</Offset>");
        sb.AppendLine($"{ind}  </Offsets>");
        sb.AppendLine($"{ind}  <VariableType>8 Bytes</VariableType>");
        sb.AppendLine($"{ind}  <ShowAsHex>1</ShowAsHex>");
        sb.AppendLine($"{ind}  <GroupHeader>1</GroupHeader>");

        // Emit sub-fields if available
        if (field.SubFields.Count > 0)
        {
            sb.AppendLine($"{ind}  <CheatEntries>");
            EmitFields(sb, depth + 1, field.SubFields);
            sb.AppendLine($"{ind}  </CheatEntries>");
        }

        sb.AppendLine($"{ind}</CheatEntry>");
    }

    /// <summary>Dispatch each field to the correct emitter based on type.</summary>
    private static void EmitFields(StringBuilder sb, int depth, IReadOnlyList<LiveFieldValue> fields)
    {
        foreach (var field in fields)
        {
            if (field.Offset < 0) continue; // Unknown offset, skip

            var upperType = field.Type.ToUpperInvariant();

            if (CompositeChildren.ContainsKey(upperType))
            {
                EmitCompositeField(sb, depth, field);
            }
            else if (field.IsPointer || upperType == "OBJECT")
            {
                EmitPointerField(sb, depth, field);
            }
            else if (field.IsContainer)
            {
                EmitContainerField(sb, depth, field);
            }
            else if (field.IsStruct && field.SubFields.Count > 0)
            {
                // Inline struct with known sub-fields: emit as group, NO pointer deref
                var ind = Indent(depth);
                sb.AppendLine($"{ind}<CheatEntry>");
                sb.AppendLine($"{ind}  <Description>\"{EscapeXml(field.Name)} ({field.Type})\"</Description>");
                sb.AppendLine($"{ind}  <Address>+{field.Offset:X}</Address>");
                sb.AppendLine($"{ind}  <GroupHeader>1</GroupHeader>");
                sb.AppendLine($"{ind}  <CheatEntries>");
                EmitFields(sb, depth + 1, field.SubFields);
                sb.AppendLine($"{ind}  </CheatEntries>");
                sb.AppendLine($"{ind}</CheatEntry>");
            }
            else
            {
                EmitLeafField(sb, depth, field);
            }
        }
    }

    // ============================================================
    // Public generation methods
    // ============================================================

    /// <summary>Generate hierarchical CE XML from breadcrumb chain + fields.</summary>
    public string GenerateHierarchicalXml(
        List<BreadcrumbEntry> breadcrumbs,
        List<LiveFieldValue> fields,
        string moduleName,
        AddressFormat addrFormat)
    {
        if (breadcrumbs.Count == 0 && fields.Count == 0)
            return "";

        var clean = CleanBreadcrumbs(breadcrumbs);
        var sb = new StringBuilder();

        sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        sb.AppendLine("<CheatTable>");
        sb.AppendLine("  <CheatEntries>");

        int depth = 2; // starting XML indent depth (inside <CheatEntries>)

        if (clean.Count > 0)
        {
            // Emit breadcrumb chain as nested groups
            for (int i = 0; i < clean.Count; i++)
            {
                var crumb = clean[i];
                string address;
                bool hasOffsets;

                if (i == 0)
                {
                    // Root: absolute address
                    address = FormatRootAddress(crumb, moduleName, addrFormat);
                    hasOffsets = false;
                }
                else
                {
                    // Intermediate: relative offset from parent
                    address = $"+{crumb.Offset:X}";
                    hasOffsets = crumb.IsPointer; // Pointer breadcrumbs need dereference
                }

                string desc = !string.IsNullOrEmpty(crumb.ClassName)
                    ? $"{crumb.Name} ({crumb.ClassName})"
                    : crumb.Name;

                EmitGroupOpen(sb, depth, desc, address, hasOffsets);
                depth++;
            }
        }

        // Emit leaf fields at current depth
        EmitFields(sb, depth, fields);

        // Close all breadcrumb groups in reverse order
        for (int i = clean.Count - 1; i >= 0; i--)
        {
            depth--;
            EmitGroupClose(sb, depth);
        }

        sb.AppendLine("  </CheatEntries>");
        sb.AppendLine("</CheatTable>");

        return sb.ToString();
    }

    /// <summary>Generate standalone instance CE XML (no breadcrumbs, just address + fields).</summary>
    public string GenerateInstanceXml(
        string address,
        string name,
        List<LiveFieldValue> fields,
        string moduleName)
    {
        if (fields.Count == 0) return "";

        var sb = new StringBuilder();
        sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        sb.AppendLine("<CheatTable>");
        sb.AppendLine("  <CheatEntries>");

        // Single root group with absolute address
        var formattedAddr = AddressHelper.FormatAddress(address, AddressFormat.HexNoPrefix, moduleName);
        EmitGroupOpen(sb, 2, name, formattedAddr, hasOffsets: false);
        EmitFields(sb, 3, fields);
        EmitGroupClose(sb, 2);

        sb.AppendLine("  </CheatEntries>");
        sb.AppendLine("</CheatTable>");

        return sb.ToString();
    }

    /// <summary>Generate CE XML for a single field (for "Copy CE Field" context action).</summary>
    public string GenerateFieldXml(LiveFieldValue field)
    {
        var sb = new StringBuilder();
        sb.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
        sb.AppendLine("<CheatTable>");
        sb.AppendLine("  <CheatEntries>");

        var upperType = field.Type.ToUpperInvariant();
        if (CompositeChildren.ContainsKey(upperType))
            EmitCompositeField(sb, 2, field);
        else if (field.IsPointer || upperType == "OBJECT")
            EmitPointerField(sb, 2, field);
        else if (field.IsContainer)
            EmitContainerField(sb, 2, field);
        else
            EmitLeafField(sb, 2, field);

        sb.AppendLine("  </CheatEntries>");
        sb.AppendLine("</CheatTable>");
        return sb.ToString();
    }

    // ============================================================
    // Address formatting
    // ============================================================

    private static string FormatRootAddress(BreadcrumbEntry root, string moduleName,
        AddressFormat format)
    {
        // If ModuleRva is available, use "module.exe"+RVA format
        if (!string.IsNullOrEmpty(root.ModuleRva) && !string.IsNullOrEmpty(moduleName))
            return $"\"{moduleName}\"+{root.ModuleRva}";

        return AddressHelper.FormatAddress(root.Address, format, moduleName);
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
