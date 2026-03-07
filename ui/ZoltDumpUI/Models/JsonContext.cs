using System.Text.Json.Serialization;

namespace ZoltDumpUI.Models;

/// <summary>
/// Source-generated JSON serialization context for pipe protocol models.
/// Used for AOT-compatible JSON serialization.
/// </summary>
[JsonSerializable(typeof(EngineState))]
[JsonSerializable(typeof(PointerState))]
[JsonSerializable(typeof(ObjectListResult))]
[JsonSerializable(typeof(ObjectEntry))]
[JsonSerializable(typeof(ObjectNode))]
[JsonSerializable(typeof(ClassInfoModel))]
[JsonSerializable(typeof(FieldInfoModel))]
[JsonSerializable(typeof(MethodInfoEntry))]
[JsonSerializable(typeof(MethodArgEntry))]
[JsonSerializable(typeof(TreeWalkResult))]
[JsonSerializable(typeof(TreeNodeInfo))]
[JsonSerializable(typeof(InstanceWalkResult))]
[JsonSerializable(typeof(LiveFieldValue))]
[JsonSerializable(typeof(FindInstancesResult))]
[JsonSerializable(typeof(AddressLookupResult))]
[JsonSerializable(typeof(CePointerInfo))]
[JsonSerializable(typeof(BreadcrumbEntry))]
[JsonSerializable(typeof(PropertySearchResult))]
[JsonSerializable(typeof(PropertySearchHit))]
[JsonSerializable(typeof(ClassListItem))]
[JsonSerializable(typeof(ScanStatusResult))]
[JsonSourceGenerationOptions(
    PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase,
    DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull)]
internal partial class ZoltJsonContext : JsonSerializerContext;
