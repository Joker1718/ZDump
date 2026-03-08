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
    public int Size { get; init; }

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
