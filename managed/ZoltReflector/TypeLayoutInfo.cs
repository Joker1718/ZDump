// Metode — 梅特黛 (All-capable Analyst — .NET Reflection Bridge)
// Data types returned by the reflection engine.

using System.Runtime.InteropServices;

namespace ZoltReflector;

/// <summary>
/// Describes a single field within a .NET type.
/// </summary>
public sealed class FieldLayoutInfo
{
    public required string Name { get; init; }
    public required string TypeName { get; init; }
    public required int Offset { get; init; }
    public required int Size { get; init; }
    public required bool IsValueType { get; init; }
    public required bool IsStatic { get; init; }
}

/// <summary>
/// Describes the full layout of a .NET type (fields + metadata).
/// </summary>
public sealed class TypeLayoutInfo
{
    public required string FullName { get; init; }
    public required string AssemblyName { get; init; }
    public required string? BaseTypeName { get; init; }
    public required int InstanceSize { get; init; }
    public required bool IsValueType { get; init; }
    public required IReadOnlyList<FieldLayoutInfo> Fields { get; init; }
}

/// <summary>
/// Brief entry for type enumeration (no field details).
/// </summary>
public sealed class TypeSummary
{
    public required string FullName { get; init; }
    public required string AssemblyName { get; init; }
    public required int FieldCount { get; init; }
    public required long MethodTablePtr { get; init; }
}
