// Denken — 頓肯 (Analytical Magic — .NET Reflection Bridge)
// Native entry points called from C++ DLL via hostfxr.
// All functions use [UnmanagedCallersOnly] for zero-overhead interop.

using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace ZoltReflector;

/// <summary>
/// C++ DLL calls these entry points via hostfxr load_assembly_and_get_function_pointer.
/// Communication: C++ passes a UTF-8 JSON request buffer, managed side writes
/// JSON response into a shared buffer and returns byte count.
/// </summary>
public static class NativeEntry
{
    // Shared response buffer (4 MB — large games may have thousands of types)
    private const int MaxResponseSize = 4 * 1024 * 1024;
    private static readonly byte[] s_responseBuffer = new byte[MaxResponseSize];

    /// <summary>
    /// Main entry point: receives a JSON command, returns JSON response.
    ///
    /// Called from C++ as:
    ///   int Reflect(RequestBuffer* req, int reqLen)
    ///
    /// Request format: { "cmd": "enumerate_types", "assembly": "GameAssembly" }
    /// Response is written to the request buffer (reused as output).
    /// Return value: byte count of response, or negative on error.
    /// </summary>
    [UnmanagedCallersOnly(EntryPoint = "Reflect")]
    public static unsafe int Reflect(byte* requestBuf, int requestLen)
    {
        try
        {
            var requestSpan = new ReadOnlySpan<byte>(requestBuf, requestLen);
            var request = JsonSerializer.Deserialize(requestSpan, ReflectJsonCtx.Default.ReflectRequest);
            if (request is null) return -1;

            string responseJson = HandleCommand(request);
            int byteCount = Encoding.UTF8.GetByteCount(responseJson);

            if (byteCount > MaxResponseSize) return -2; // response too large

            Encoding.UTF8.GetBytes(responseJson, s_responseBuffer);

            // Copy response to the request buffer (caller allocated, must be large enough)
            fixed (byte* src = s_responseBuffer)
            {
                Buffer.MemoryCopy(src, requestBuf, byteCount, byteCount);
            }

            return byteCount;
        }
        catch (Exception ex)
        {
            // Write error JSON with exception details (truncated to fit buffer)
            string msg = ex.ToString();
            if (msg.Length > 4000) msg = msg[..4000];
            string error = $"{{\"error\":\"{EscapeJson(msg)}\"}}";
            int len = Encoding.UTF8.GetByteCount(error);
            if (len > MaxResponseSize) len = MaxResponseSize;

            var bytes = Encoding.UTF8.GetBytes(error);
            int copyLen = Math.Min(bytes.Length, MaxResponseSize);
            fixed (byte* src = bytes)
            {
                Buffer.MemoryCopy(src, requestBuf, copyLen, copyLen);
            }
            return copyLen;
        }
    }

    /// <summary>
    /// Simple ping to verify the managed bridge is alive.
    /// Returns 0x44454E4B ("DENK" in ASCII) on success.
    /// </summary>
    [UnmanagedCallersOnly(EntryPoint = "DenkenPing")]
    public static int DenkenPing()
    {
        return 0x44454E4B; // "DENK"
    }

    // ================================================================
    // Command dispatch
    // ================================================================

    private static string HandleCommand(ReflectRequest request)
    {
        return request.Cmd switch
        {
            "enumerate_types" => HandleEnumerateTypes(request),
            "get_type_layout" => HandleGetTypeLayout(request),
            "get_method_table" => HandleGetMethodTable(request),
            "list_assemblies" => HandleListAssemblies(),
            _ => $"{{\"error\":\"unknown command: {EscapeJson(request.Cmd)}\"}}"
        };
    }

    private static string HandleEnumerateTypes(ReflectRequest req)
    {
        var assemblies = string.IsNullOrEmpty(req.Assembly)
            ? Reflector.GetGameAssemblies()
            : Reflector.GetAssembliesByName(req.Assembly);

        var types = Reflector.EnumerateTypes(assemblies);
        return JsonSerializer.Serialize(types, ReflectJsonCtx.Default.TypeSummaryArray);
    }

    private static string HandleGetTypeLayout(ReflectRequest req)
    {
        if (string.IsNullOrEmpty(req.TypeName))
            return "{\"error\":\"typeName required\"}";

        var type = Reflector.FindType(req.TypeName);
        if (type is null)
            return $"{{\"error\":\"type not found: {EscapeJson(req.TypeName)}\"}}";

        var layout = Reflector.GetTypeLayout(type);
        if (layout is null)
            return $"{{\"error\":\"cannot get layout for: {EscapeJson(req.TypeName)}\"}}";

        return JsonSerializer.Serialize(layout, ReflectJsonCtx.Default.TypeLayoutInfo);
    }

    private static string HandleGetMethodTable(ReflectRequest req)
    {
        if (string.IsNullOrEmpty(req.TypeName))
            return "{\"error\":\"typeName required\"}";

        var type = Reflector.FindType(req.TypeName);
        if (type is null)
            return $"{{\"error\":\"type not found: {EscapeJson(req.TypeName)}\"}}";

        nint mt = Reflector.GetMethodTable(type);
        return $"{{\"methodTable\":\"0x{mt:X}\"}}";
    }

    private static string HandleListAssemblies()
    {
        var assemblies = Reflector.GetGameAssemblies();
        var names = assemblies.Select(a => a.GetName().Name ?? "").ToArray();
        return JsonSerializer.Serialize(names, ReflectJsonCtx.Default.StringArray);
    }

    private static string EscapeJson(string s)
    {
        return s.Replace("\\", "\\\\").Replace("\"", "\\\"");
    }
}

// ================================================================
// JSON serialization context (AOT-safe, no reflection)
// ================================================================

public sealed class ReflectRequest
{
    [JsonPropertyName("cmd")]
    public string Cmd { get; set; } = "";

    [JsonPropertyName("assembly")]
    public string? Assembly { get; set; }

    [JsonPropertyName("typeName")]
    public string? TypeName { get; set; }
}

[JsonSourceGenerationOptions(PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase)]
[JsonSerializable(typeof(ReflectRequest))]
[JsonSerializable(typeof(TypeSummary[]))]
[JsonSerializable(typeof(TypeLayoutInfo))]
[JsonSerializable(typeof(FieldLayoutInfo))]
[JsonSerializable(typeof(string[]))]
internal partial class ReflectJsonCtx : JsonSerializerContext
{
}
