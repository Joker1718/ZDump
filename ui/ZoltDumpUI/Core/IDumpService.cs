using ZoltDumpUI.Models;

namespace ZoltDumpUI.Core;

/// <summary>
/// Ausetiese — 服從之秤 (Aura's Scales)
/// Main business logic interface for Godot object inspection via pipe.
/// </summary>
public interface IDumpService
{
    Task<EngineState> InitAsync(CancellationToken ct = default);
    Task<PointerState> GetPointersAsync(CancellationToken ct = default);
    Task<int> GetObjectCountAsync(CancellationToken ct = default);
    Task<ObjectListResult> GetObjectListAsync(int offset = 0, int limit = 500, CancellationToken ct = default);
    Task<ObjectNode> GetObjectAsync(string address, CancellationToken ct = default);
    Task<ObjectNode?> FindObjectAsync(string nodePath, CancellationToken ct = default);
    Task<AddressLookupResult> FindByAddressAsync(string address, CancellationToken ct = default);
    Task<ClassInfoModel> WalkClassAsync(string className, CancellationToken ct = default);
    Task<InstanceWalkResult> WalkInstanceAsync(string address, CancellationToken ct = default);
    Task<ContainerWalkResult> WalkContainerAsync(string address, int variantType,
        int offset = 0, int limit = 100, CancellationToken ct = default);
    Task<TreeWalkResult> WalkTreeAsync(int depth = -1, CancellationToken ct = default);
    Task<FindInstancesResult> FindInstancesAsync(string className, bool exactMatch = false, CancellationToken ct = default);
    Task<List<ClassListItem>> ListClassesAsync(bool gameOnly = false, CancellationToken ct = default);
    Task<PropertySearchResult> SearchPropertiesAsync(string query, bool gameOnly = false, CancellationToken ct = default);
    Task<byte[]> ReadMemAsync(string address, int size, CancellationToken ct = default);
    Task<bool> WriteMemAsync(string address, byte[] data, CancellationToken ct = default);
    Task<bool> WatchAsync(string address, CancellationToken ct = default);
    Task<bool> UnwatchAsync(string address, CancellationToken ct = default);
    Task<CePointerInfo> GetCePointerInfoAsync(string address, CancellationToken ct = default);
    Task<ScanStatusResult> GetScanStatusAsync(CancellationToken ct = default);
    Task TriggerScanAsync(CancellationToken ct = default);
}
