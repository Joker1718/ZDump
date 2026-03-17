using System.Text.Json.Serialization;

namespace ZoltDumpUI.Models;

/// <summary>
/// Pipe message for AOBMaker CE Plugin bridge.
/// Protocol: 4-byte little-endian length prefix + UTF-8 JSON payload.
/// Field "type" dispatches to handler (PascalCase, e.g. "CreateAAScript").
/// Must match AOBMaker/plugins/CEPlugin/src/protocol.h type constants.
/// </summary>
public sealed class AobMakerMessage
{
    [JsonPropertyName("type")]
    public string Type { get; init; } = "";

    [JsonPropertyName("address")]
    public string? Address { get; init; }

    /// <summary>Description / label for the script entry in CE's address list.</summary>
    [JsonPropertyName("description")]
    public string? Description { get; init; }

    [JsonPropertyName("script")]
    public string? Script { get; init; }

    [JsonPropertyName("autoActivate")]
    public bool? AutoActivate { get; init; }

    // CreateSymbolScript fields
    [JsonPropertyName("name")]
    public string? Name { get; init; }

    [JsonPropertyName("aob")]
    public string? Aob { get; init; }

    [JsonPropertyName("pos")]
    public int? Pos { get; init; }

    [JsonPropertyName("aoblen")]
    public int? Aoblen { get; init; }

    [JsonPropertyName("symbol")]
    public string? Symbol { get; init; }

    [JsonPropertyName("module")]
    public string? Module { get; init; }

    // CreateMemoryRecord fields
    /// <summary>CE TVariableType: 0=byte,1=word,2=dword,3=qword,4=single,5=double,6=string,7=unicode,8=byteArray,9=binary</summary>
    [JsonPropertyName("valueType")]
    public int? ValueType { get; init; }

    [JsonPropertyName("isSigned")]
    public bool? IsSigned { get; init; }

    // Grouped entry support (parent-child nesting)
    /// <summary>CE CheatEntry Options string, e.g. "moHideChildren=\"1\" moDeactivateChildrenAsWell=\"1\""</summary>
    [JsonPropertyName("options")]
    public string? Options { get; init; }

    /// <summary>Child entries nested under this parent (for grouped CE entries).</summary>
    [JsonPropertyName("children")]
    public List<AobMakerMessage>? Children { get; init; }
}

[JsonSerializable(typeof(AobMakerMessage))]
[JsonSerializable(typeof(List<AobMakerMessage>))]
[JsonSourceGenerationOptions(DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
internal partial class AobMakerJsonContext : JsonSerializerContext;
