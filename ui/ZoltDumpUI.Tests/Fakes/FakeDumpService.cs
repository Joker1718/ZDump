using ZoltDumpUI.Core;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Tests.Fakes;

/// <summary>
/// Fake IDumpService for ViewModel tests.
/// Returns pre-configured data without pipe communication.
/// </summary>
public sealed class FakeDumpService : IDumpService
{
    public List<ClassListItem> ClassList { get; set; } = [];
    public PropertySearchResult SearchResult { get; set; } = new();
    public FindInstancesResult InstancesResult { get; set; } = new();
    public InstanceWalkResult WalkResult { get; set; } = new();
    public ClassInfoModel ClassInfo { get; set; } = new();

    /// <summary>Track which methods were called and with what arguments.</summary>
    public List<(string Method, object?[] Args)> Calls { get; } = [];

    public Task<EngineState> InitAsync(CancellationToken ct = default)
        => Task.FromResult(new EngineState { Ok = true, GodotVersion = "4.3" });

    public Task<PointerState> GetPointersAsync(CancellationToken ct = default)
        => Task.FromResult(new PointerState());

    public Task<int> GetObjectCountAsync(CancellationToken ct = default)
        => Task.FromResult(0);

    public Task<ObjectListResult> GetObjectListAsync(int offset = 0, int limit = 500, CancellationToken ct = default)
        => Task.FromResult(new ObjectListResult());

    public Task<ObjectNode> GetObjectAsync(string address, CancellationToken ct = default)
        => Task.FromResult(new ObjectNode { Address = address });

    public Task<ObjectNode?> FindObjectAsync(string nodePath, CancellationToken ct = default)
        => Task.FromResult<ObjectNode?>(null);

    public Task<AddressLookupResult> FindByAddressAsync(string address, CancellationToken ct = default)
        => Task.FromResult(new AddressLookupResult());

    public Task<ClassInfoModel> WalkClassAsync(string className, CancellationToken ct = default)
    {
        Calls.Add(("WalkClassAsync", [className]));
        return Task.FromResult(ClassInfo);
    }

    public Task<InstanceWalkResult> WalkInstanceAsync(string address, CancellationToken ct = default)
    {
        Calls.Add(("WalkInstanceAsync", [address]));
        return Task.FromResult(WalkResult);
    }

    public Task<ContainerWalkResult> WalkContainerAsync(string address, int variantType,
        int offset = 0, int limit = 100, CancellationToken ct = default)
        => Task.FromResult(new ContainerWalkResult());

    public Task<TreeWalkResult> WalkTreeAsync(int depth = -1, CancellationToken ct = default)
        => Task.FromResult(new TreeWalkResult());

    public Task<FindInstancesResult> FindInstancesAsync(string className, bool exactMatch = false, CancellationToken ct = default)
    {
        Calls.Add(("FindInstancesAsync", [className, exactMatch]));
        return Task.FromResult(InstancesResult);
    }

    public Task<List<ClassListItem>> ListClassesAsync(bool gameOnly = false, CancellationToken ct = default)
    {
        Calls.Add(("ListClassesAsync", [gameOnly]));
        return Task.FromResult(ClassList);
    }

    public Task<PropertySearchResult> SearchPropertiesAsync(string query, bool gameOnly = false, CancellationToken ct = default)
    {
        Calls.Add(("SearchPropertiesAsync", [query, gameOnly]));
        return Task.FromResult(SearchResult);
    }

    public Task<byte[]> ReadMemAsync(string address, int size, CancellationToken ct = default)
        => Task.FromResult(Array.Empty<byte>());

    public Task<bool> WriteMemAsync(string address, byte[] data, CancellationToken ct = default)
        => Task.FromResult(true);

    public Task<bool> WatchAsync(string address, CancellationToken ct = default)
        => Task.FromResult(true);

    public Task<bool> UnwatchAsync(string address, CancellationToken ct = default)
        => Task.FromResult(true);

    public Task<CePointerInfo> GetCePointerInfoAsync(string address, CancellationToken ct = default)
        => Task.FromResult(new CePointerInfo());

    public Task<ScanStatusResult> GetScanStatusAsync(CancellationToken ct = default)
        => Task.FromResult(new ScanStatusResult { IsComplete = true });

    public Task TriggerScanAsync(CancellationToken ct = default)
        => Task.CompletedTask;
}
