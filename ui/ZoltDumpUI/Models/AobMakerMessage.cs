using System.Text.Encodings.Web;
using System.Text.Json.Serialization;

namespace ZoltDumpUI.Models;

/// <summary>Pipe message for AOBMaker CE Plugin bridge.</summary>
public sealed class AobMakerMessage
{
    [JsonPropertyName("cmd")]
    public string Cmd { get; init; } = "";

    [JsonPropertyName("address")]
    public string? Address { get; init; }

    [JsonPropertyName("label")]
    public string? Label { get; init; }

    [JsonPropertyName("script")]
    public string? Script { get; init; }
}

[JsonSerializable(typeof(AobMakerMessage))]
[JsonSourceGenerationOptions(
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
internal partial class AobMakerJsonContext : JsonSerializerContext;
