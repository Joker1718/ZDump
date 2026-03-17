namespace ZoltDumpUI.Core;

/// <summary>AOBMaker CE Plugin bridge interface.</summary>
public interface IAobMakerBridge
{
    Task<bool> CheckAvailabilityAsync(CancellationToken ct = default);
    Task NavigateHexViewAsync(string address, CancellationToken ct = default);
    Task NavigateDisassemblerAsync(string address, CancellationToken ct = default);
    Task CreateAAScriptAsync(string label, string script, CancellationToken ct = default);
    Task CreateSymbolScriptAsync(string label, string address, CancellationToken ct = default);
    Task CreateMemoryRecordAsync(string description, string address, CeValueType valueType,
        bool isSigned = false, CancellationToken ct = default);
}

/// <summary>
/// CE TVariableType — must match CE internal enum (commontypedefs.pas).
/// NOT the SDK header order — CE internal order.
/// </summary>
public enum CeValueType
{
    Byte = 0,
    Word = 1,       // 2 bytes
    Dword = 2,      // 4 bytes
    Qword = 3,      // 8 bytes
    Float = 4,      // single
    Double = 5,
    String = 6,
    UnicodeString = 7,
    ByteArray = 8,
    Binary = 9
}
