// Metode — 梅特黛 (All-capable Analyst — .NET Reflection Bridge)
// Core reflection engine: enumerate assemblies, types, fields with offsets.
// Designed to run inside a Godot .NET game process where CoreCLR is already loaded.

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace ZoltReflector;

/// <summary>
/// Enumerates .NET types and fields with memory layout information.
/// Works by leveraging System.Reflection on already-loaded assemblies.
/// </summary>
public static class Reflector
{
    // Binding flags to capture all instance fields (public + private + inherited)
    private const BindingFlags AllInstance =
        BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;

    private const BindingFlags AllDeclared =
        BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    private const BindingFlags AllStatic =
        BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;

    /// <summary>
    /// Get all non-system assemblies currently loaded in the AppDomain.
    /// Filters out System.*, Microsoft.*, netstandard, mscorlib.
    /// </summary>
    public static Assembly[] GetGameAssemblies()
    {
        return AppDomain.CurrentDomain.GetAssemblies()
            .Where(a =>
            {
                var name = a.GetName().Name ?? "";
                if (name.StartsWith("System", StringComparison.Ordinal)) return false;
                if (name.StartsWith("Microsoft", StringComparison.Ordinal)) return false;
                if (name == "netstandard" || name == "mscorlib") return false;
                return true;
            })
            .ToArray();
    }

    /// <summary>
    /// Get all assemblies including a specific set of names (exact match).
    /// </summary>
    public static Assembly[] GetAssembliesByName(params string[] names)
    {
        var nameSet = new HashSet<string>(names, StringComparer.OrdinalIgnoreCase);
        return AppDomain.CurrentDomain.GetAssemblies()
            .Where(a => nameSet.Contains(a.GetName().Name ?? ""))
            .ToArray();
    }

    /// <summary>
    /// Enumerate all concrete (non-abstract, non-generic-open) types from given assemblies.
    /// </summary>
    public static TypeSummary[] EnumerateTypes(Assembly[] assemblies)
    {
        var results = new List<TypeSummary>();

        foreach (var asm in assemblies)
        {
            Type[] types;
            try { types = asm.GetTypes(); }
            catch (ReflectionTypeLoadException ex)
            {
                types = ex.Types.Where(t => t is not null).ToArray()!;
            }

            foreach (var t in types)
            {
                if (t.IsAbstract && !t.IsSealed) continue; // skip abstract (but allow static = abstract+sealed)
                if (t.IsGenericTypeDefinition) continue;    // skip open generics

                int fieldCount = 0;
                try { fieldCount = t.GetFields(AllInstance).Length; } catch { }

                nint mt = 0;
                try { mt = t.TypeHandle.Value; } catch { }

                results.Add(new TypeSummary
                {
                    FullName = t.FullName ?? t.Name,
                    AssemblyName = asm.GetName().Name ?? "",
                    FieldCount = fieldCount,
                    MethodTablePtr = (long)mt
                });
            }
        }

        return results.ToArray();
    }

    /// <summary>
    /// Get detailed field layout for a specific type (by name or Type object).
    /// Uses Marshal.ReadInt32 trick on a zeroed instance to discover field offsets,
    /// with fallback to FieldInfo metadata for value types.
    /// </summary>
    public static TypeLayoutInfo? GetTypeLayout(Type type)
    {
        if (type.IsGenericTypeDefinition) return null;

        var fields = new List<FieldLayoutInfo>();

        // Instance fields (all, including inherited from base classes)
        foreach (var fi in type.GetFields(AllInstance))
        {
            int offset = GetFieldOffset(fi);
            int size = GetFieldSize(fi.FieldType);

            fields.Add(new FieldLayoutInfo
            {
                Name = fi.Name,
                TypeName = fi.FieldType.FullName ?? fi.FieldType.Name,
                Offset = offset,
                Size = size,
                IsValueType = fi.FieldType.IsValueType,
                IsStatic = false
            });
        }

        // Static fields (declared only — statics don't inherit)
        foreach (var fi in type.GetFields(AllStatic))
        {
            int size = GetFieldSize(fi.FieldType);

            fields.Add(new FieldLayoutInfo
            {
                Name = fi.Name,
                TypeName = fi.FieldType.FullName ?? fi.FieldType.Name,
                Offset = -1, // static fields don't have instance offsets
                Size = size,
                IsValueType = fi.FieldType.IsValueType,
                IsStatic = true
            });
        }

        // Sort instance fields by offset for readability
        fields.Sort((a, b) =>
        {
            if (a.IsStatic != b.IsStatic) return a.IsStatic ? 1 : -1;
            return a.Offset.CompareTo(b.Offset);
        });

        int instanceSize = 0;
        try
        {
            if (!type.IsValueType && !type.IsAbstract)
                instanceSize = GetManagedObjectSize(type);
            else if (type.IsValueType)
                instanceSize = Marshal.SizeOf(type);
        }
        catch { }

        return new TypeLayoutInfo
        {
            FullName = type.FullName ?? type.Name,
            AssemblyName = type.Assembly.GetName().Name ?? "",
            BaseTypeName = type.BaseType?.FullName,
            InstanceSize = instanceSize,
            IsValueType = type.IsValueType,
            Fields = fields
        };
    }

    /// <summary>
    /// Resolve a type by its full name across all loaded assemblies.
    /// </summary>
    public static Type? FindType(string fullName)
    {
        // Try direct resolution first
        var t = Type.GetType(fullName);
        if (t is not null) return t;

        // Search all loaded assemblies
        foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
        {
            t = asm.GetType(fullName);
            if (t is not null) return t;
        }
        return null;
    }

    /// <summary>
    /// Get the MethodTable pointer for a given type.
    /// This is the first QWORD in any managed object header.
    /// </summary>
    public static nint GetMethodTable(Type type)
    {
        try { return type.TypeHandle.Value; }
        catch { return 0; }
    }

    // ================================================================
    // Internal helpers
    // ================================================================

    /// <summary>
    /// Get the offset of a field within its declaring type.
    /// Returns offset from object base (including 8-byte MethodTable pointer).
    /// Strategy:
    ///   1. Sequential/Explicit value types → Marshal.OffsetOf (exact)
    ///   2. FieldDesc internal structure (primary for reference types — most reliable)
    ///   3. Marker-based probe as fallback
    /// </summary>
    internal static int GetFieldOffset(FieldInfo fi)
    {
        var declType = fi.DeclaringType;

        // Strategy 1: Marshal.OffsetOf for struct types with explicit layout
        if (declType is not null && declType.IsValueType)
        {
            var layout = declType.StructLayoutAttribute;
            if (layout is not null &&
                (layout.Value == LayoutKind.Sequential || layout.Value == LayoutKind.Explicit))
            {
                try { return (int)Marshal.OffsetOf(declType, fi.Name); }
                catch { }
            }
        }

        // Strategy 2: FieldDesc (CoreCLR internal — most reliable for reference types)
        // FieldDesc stores offset from data area (after MethodTable*).
        // For reference types, add IntPtr.Size (8) to get offset from object base.
        try
        {
            int fdOffset = GetFieldOffsetFromHandle(fi.FieldHandle);
            if (fdOffset >= 0)
            {
                bool isRefType = declType is not null && !declType.IsValueType;
                return isRefType ? fdOffset + IntPtr.Size : fdOffset;
            }
        }
        catch { }

        // Strategy 3: Marker-based probe as fallback for concrete reference types
        if (declType is not null && !declType.IsValueType && !declType.IsAbstract)
        {
            try
            {
                int result = GetFieldOffsetByMarkerProbe(fi);
                if (result >= 0) return result;
            }
            catch { }
        }

        return -1;
    }

    /// <summary>
    /// Determine field offset by creating a zeroed instance, writing a known
    /// marker value via FieldInfo.SetValue, then scanning the object's raw memory
    /// to find where the marker landed.
    /// Works for all field types (value + reference) in reference type objects.
    /// </summary>
    private static unsafe int GetFieldOffsetByMarkerProbe(FieldInfo fi)
    {
        var declType = fi.DeclaringType!;
        object instance;
        try { instance = RuntimeHelpers.GetUninitializedObject(declType); }
        catch { return -1; }

        // Get pointer to the object's data area (after object header).
        // On .NET 8 we use __makeref to get the object pointer, then skip the MT*.
        var tr = __makeref(instance);
        nint objPtr = **(nint**)&tr;
        nint dataPtr = objPtr + IntPtr.Size; // skip MethodTable*

        // Scan range: up to 4KB from object start (generous for any class)
        const int scanRange = 4096;

        if (fi.FieldType == typeof(int) || fi.FieldType == typeof(uint))
        {
            const int marker = unchecked((int)0xDEAD_BF01);
            fi.SetValue(instance, fi.FieldType == typeof(uint) ? (object)unchecked((uint)marker) : (object)marker);
            for (int off = 0; off < scanRange - 4; off += 4)
            {
                if (*(int*)(dataPtr + off) == marker)
                {
                    // Verify by clearing and re-checking
                    fi.SetValue(instance, fi.FieldType == typeof(uint) ? (object)0u : (object)0);
                    if (*(int*)(dataPtr + off) == 0)
                        return off + IntPtr.Size; // +8 for MethodTable* (rawData starts after MT)
                }
            }
        }
        else if (fi.FieldType == typeof(long) || fi.FieldType == typeof(ulong))
        {
            const long marker = unchecked((long)0xDEAD_BF02_CAFE_0001);
            fi.SetValue(instance, fi.FieldType == typeof(ulong) ? (object)unchecked((ulong)marker) : (object)marker);
            for (int off = 0; off < scanRange - 8; off += 4)
            {
                if (*(long*)(dataPtr + off) == marker)
                {
                    fi.SetValue(instance, fi.FieldType == typeof(ulong) ? (object)0UL : (object)0L);
                    if (*(long*)(dataPtr + off) == 0)
                        return off + IntPtr.Size;
                }
            }
        }
        else if (fi.FieldType == typeof(short) || fi.FieldType == typeof(ushort) || fi.FieldType == typeof(char))
        {
            const short marker = unchecked((short)0x7F01);
            object boxed = fi.FieldType == typeof(char) ? (object)(char)marker
                         : fi.FieldType == typeof(ushort) ? (object)(ushort)marker
                         : (object)marker;
            fi.SetValue(instance, boxed);
            for (int off = 0; off < scanRange - 2; off += 2)
            {
                if (*(short*)(dataPtr + off) == marker)
                {
                    object zero = fi.FieldType == typeof(char) ? (object)(char)0
                                : fi.FieldType == typeof(ushort) ? (object)(ushort)0
                                : (object)(short)0;
                    fi.SetValue(instance, zero);
                    if (*(short*)(dataPtr + off) == 0)
                        return off + IntPtr.Size;
                }
            }
        }
        else if (fi.FieldType == typeof(byte) || fi.FieldType == typeof(sbyte) || fi.FieldType == typeof(bool))
        {
            const byte marker = 0xBF;
            object boxed = fi.FieldType == typeof(bool) ? (object)true
                         : fi.FieldType == typeof(sbyte) ? (object)unchecked((sbyte)marker)
                         : (object)marker;
            fi.SetValue(instance, boxed);

            byte expected = fi.FieldType == typeof(bool) ? (byte)1 : marker;
            for (int off = 0; off < scanRange; off++)
            {
                if (*(byte*)(dataPtr + off) == expected)
                {
                    object zero = fi.FieldType == typeof(bool) ? (object)false
                                : fi.FieldType == typeof(sbyte) ? (object)(sbyte)0
                                : (object)(byte)0;
                    fi.SetValue(instance, zero);
                    if (*(byte*)(dataPtr + off) == 0)
                        return off + IntPtr.Size;
                }
            }
        }
        else if (fi.FieldType == typeof(float))
        {
            const float marker = 1.23456789e+15f; // unlikely natural value
            fi.SetValue(instance, marker);
            for (int off = 0; off < scanRange - 4; off += 4)
            {
                if (*(float*)(dataPtr + off) == marker)
                {
                    fi.SetValue(instance, 0f);
                    if (*(float*)(dataPtr + off) == 0f)
                        return off + IntPtr.Size;
                }
            }
        }
        else if (fi.FieldType == typeof(double))
        {
            const double marker = 9.87654321e+100;
            fi.SetValue(instance, marker);
            for (int off = 0; off < scanRange - 8; off += 4)
            {
                if (*(double*)(dataPtr + off) == marker)
                {
                    fi.SetValue(instance, 0d);
                    if (*(double*)(dataPtr + off) == 0d)
                        return off + IntPtr.Size;
                }
            }
        }
        else if (!fi.FieldType.IsValueType)
        {
            // Reference type field: write a known object, find its pointer
            // Create a marker object with a recognizable pattern
            var marker = new object();
            nint markerPtr;
            {
                var markerRef = __makeref(marker);
                markerPtr = **(nint**)&markerRef;
            }

            fi.SetValue(instance, marker);
            for (int off = 0; off < scanRange - IntPtr.Size; off += IntPtr.Size)
            {
                if (*(nint*)(dataPtr + off) == markerPtr)
                {
                    fi.SetValue(instance, null);
                    if (*(nint*)(dataPtr + off) == 0)
                        return off + IntPtr.Size;
                }
            }
        }

        return -1;
    }

    /// <summary>
    /// Extract field offset from RuntimeFieldHandle (fallback).
    /// In CoreCLR, FieldHandle.Value → FieldDesc, where offset is at +0x0C (DWORD).
    /// This is an implementation detail but stable across .NET 6-10.
    /// </summary>
    private static unsafe int GetFieldOffsetFromHandle(RuntimeFieldHandle handle)
    {
        nint fdPtr = handle.Value;
        if (fdPtr == 0) return -1;

        // FieldDesc layout (CoreCLR x64):
        //   +0x00: MethodTable* (QWORD)
        //   +0x08: DWORD m_dword1 (token bits)
        //   +0x0C: DWORD m_dword2 (offset in low 27 bits)
        try
        {
            uint dword2 = *(uint*)(fdPtr + 0x0C);
            int offset = (int)(dword2 & 0x07FFFFFF); // low 27 bits = offset

            // Sanity check: offset should be reasonable (< 64KB for most objects)
            if (offset is > 0 and < 0x10000)
                return offset;
        }
        catch (AccessViolationException) { }
        catch (NullReferenceException) { }

        return -1;
    }

    /// <summary>
    /// Get the size of a field's type in bytes.
    /// </summary>
    internal static int GetFieldSize(Type fieldType)
    {
        if (fieldType == typeof(byte) || fieldType == typeof(sbyte) || fieldType == typeof(bool))
            return 1;
        if (fieldType == typeof(short) || fieldType == typeof(ushort) || fieldType == typeof(char))
            return 2;
        if (fieldType == typeof(int) || fieldType == typeof(uint) || fieldType == typeof(float))
            return 4;
        if (fieldType == typeof(long) || fieldType == typeof(ulong) || fieldType == typeof(double))
            return 8;
        if (fieldType == typeof(nint) || fieldType == typeof(nuint))
            return IntPtr.Size;
        if (fieldType == typeof(decimal))
            return 16;

        // Reference types are pointer-sized
        if (!fieldType.IsValueType)
            return IntPtr.Size;

        // Value types: try Marshal.SizeOf
        try { return Marshal.SizeOf(fieldType); }
        catch { return -1; }
    }

    /// <summary>
    /// Estimate the managed object size (header + fields).
    /// </summary>
    private static int GetManagedObjectSize(Type type)
    {
        // Managed objects: ObjHeader(8) + MT*(8) + fields
        // The actual size is aligned to pointer size
        var fields = type.GetFields(AllInstance);
        if (fields.Length == 0) return IntPtr.Size * 2; // just header + MT

        int maxEnd = 0;
        foreach (var fi in fields)
        {
            int off = GetFieldOffset(fi);
            if (off < 0) continue;
            int size = GetFieldSize(fi.FieldType);
            if (size < 0) size = IntPtr.Size;
            int end = off + size;
            if (end > maxEnd) maxEnd = end;
        }

        // Align to pointer size
        int aligned = (maxEnd + IntPtr.Size - 1) & ~(IntPtr.Size - 1);
        return aligned;
    }
}
