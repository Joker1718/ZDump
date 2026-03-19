using System.Diagnostics.CodeAnalysis;

namespace ZoltDumpUI.Models;

/// <summary>Single live field value for instance inspection.</summary>
[DynamicallyAccessedMembers(DynamicallyAccessedMemberTypes.PublicProperties)]
public sealed class LiveFieldValue
{
    public string Name { get; init; } = "";
    public string Type { get; init; } = "";
    public string Value { get; init; } = "";
    public string Hex { get; init; } = "";
    public string Address { get; init; } = "";
    public int Offset { get; init; }

    /// <summary>Formatted offset for display: "0xNN" for native, "(script)" for GDScript/managed members.</summary>
    public string OffsetDisplay => Offset < 0 ? "(script)" : $"0x{Offset:X}";

    /// <summary>Numeric sort key for the Offs column. Script members sort to top (-1), native/managed by offset.</summary>
    public int SortOffset => Offset;

    public int Size { get; init; }
    public int BitShift { get; init; }
    public int BitMask { get; init; }

    // Navigation hints
    public string PtrValue { get; init; } = "";
    public string PtrClass { get; init; } = "";
    public bool IsPointer { get; init; }
    public bool IsStruct { get; init; }
    public bool IsContainer { get; init; }
    public bool IsEditable { get; init; }

    // Container info
    public int ElementCount { get; init; }
    public string ElementType { get; init; } = "";
    public string KeyType { get; init; } = "";

    // Variant info
    public int VariantType { get; init; }

    // PropertyInfo metadata
    public int Hint { get; init; }
    public string HintString { get; init; } = "";
    public int Usage { get; init; }

    // Enum info
    public string EnumName { get; init; } = "";
    public List<string> EnumValues { get; init; } = [];

    // Struct sub-fields (inline expansion)
    public List<LiveFieldValue> SubFields { get; init; } = [];

    // For LiveWalker UI state (not from DLL)
    public bool IsSearchMatch { get; set; }
}
