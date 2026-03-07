using System.Text.Json.Serialization;

namespace ZoltDumpUI.Models;

/// <summary>Per-game AOB scan record for usage tracking.</summary>
public sealed class AobUsageRecord
{
    [JsonPropertyName("gameName")]
    public string GameName { get; init; } = "";

    [JsonPropertyName("peHash")]
    public string PeHash { get; init; } = "";

    [JsonPropertyName("godotVersion")]
    public string GodotVersion { get; init; } = "";

    [JsonPropertyName("scanDate")]
    public DateTime ScanDate { get; init; }

    [JsonPropertyName("patternsUsed")]
    public List<string> PatternsUsed { get; init; } = [];
}

[JsonSerializable(typeof(List<AobUsageRecord>))]
[JsonSourceGenerationOptions(
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    WriteIndented = true)]
internal partial class AobUsageJsonContext : JsonSerializerContext;
