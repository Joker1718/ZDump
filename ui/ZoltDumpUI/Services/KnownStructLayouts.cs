namespace ZoltDumpUI.Services;

/// <summary>
/// Hardcoded field layouts for common Godot struct types.
/// Used for CSX/SDK export when DLL doesn't provide sub-field info.
/// </summary>
public static class KnownStructLayouts
{
    public record StructField(string Name, string CppType, int Offset, int Size);

    public static readonly Dictionary<string, StructField[]> Layouts = new()
    {
        ["Vector2"] =
        [
            new("x", "float", 0, 4),
            new("y", "float", 4, 4)
        ],
        ["Vector2i"] =
        [
            new("x", "int32_t", 0, 4),
            new("y", "int32_t", 4, 4)
        ],
        ["Vector3"] =
        [
            new("x", "float", 0, 4),
            new("y", "float", 4, 4),
            new("z", "float", 8, 4)
        ],
        ["Vector3i"] =
        [
            new("x", "int32_t", 0, 4),
            new("y", "int32_t", 4, 4),
            new("z", "int32_t", 8, 4)
        ],
        ["Vector4"] =
        [
            new("x", "float", 0, 4),
            new("y", "float", 4, 4),
            new("z", "float", 8, 4),
            new("w", "float", 12, 4)
        ],
        ["Vector4i"] =
        [
            new("x", "int32_t", 0, 4),
            new("y", "int32_t", 4, 4),
            new("z", "int32_t", 8, 4),
            new("w", "int32_t", 12, 4)
        ],
        ["Color"] =
        [
            new("r", "float", 0, 4),
            new("g", "float", 4, 4),
            new("b", "float", 8, 4),
            new("a", "float", 12, 4)
        ],
        ["Rect2"] =
        [
            new("position.x", "float", 0, 4),
            new("position.y", "float", 4, 4),
            new("size.x", "float", 8, 4),
            new("size.y", "float", 12, 4)
        ],
        ["Rect2i"] =
        [
            new("position.x", "int32_t", 0, 4),
            new("position.y", "int32_t", 4, 4),
            new("size.x", "int32_t", 8, 4),
            new("size.y", "int32_t", 12, 4)
        ],
        ["AABB"] =
        [
            new("position.x", "float", 0, 4),
            new("position.y", "float", 4, 4),
            new("position.z", "float", 8, 4),
            new("size.x", "float", 12, 4),
            new("size.y", "float", 16, 4),
            new("size.z", "float", 20, 4)
        ],
        ["Plane"] =
        [
            new("normal.x", "float", 0, 4),
            new("normal.y", "float", 4, 4),
            new("normal.z", "float", 8, 4),
            new("d", "float", 12, 4)
        ],
        ["Quaternion"] =
        [
            new("x", "float", 0, 4),
            new("y", "float", 4, 4),
            new("z", "float", 8, 4),
            new("w", "float", 12, 4)
        ],
        ["Basis"] =
        [
            new("rows[0].x", "float", 0, 4),
            new("rows[0].y", "float", 4, 4),
            new("rows[0].z", "float", 8, 4),
            new("rows[1].x", "float", 12, 4),
            new("rows[1].y", "float", 16, 4),
            new("rows[1].z", "float", 20, 4),
            new("rows[2].x", "float", 24, 4),
            new("rows[2].y", "float", 28, 4),
            new("rows[2].z", "float", 32, 4)
        ],
        ["Transform3D"] =
        [
            new("basis.rows[0].x", "float", 0, 4),
            new("basis.rows[0].y", "float", 4, 4),
            new("basis.rows[0].z", "float", 8, 4),
            new("basis.rows[1].x", "float", 12, 4),
            new("basis.rows[1].y", "float", 16, 4),
            new("basis.rows[1].z", "float", 20, 4),
            new("basis.rows[2].x", "float", 24, 4),
            new("basis.rows[2].y", "float", 28, 4),
            new("basis.rows[2].z", "float", 32, 4),
            new("origin.x", "float", 36, 4),
            new("origin.y", "float", 40, 4),
            new("origin.z", "float", 44, 4)
        ],
        ["Transform2D"] =
        [
            new("columns[0].x", "float", 0, 4),
            new("columns[0].y", "float", 4, 4),
            new("columns[1].x", "float", 8, 4),
            new("columns[1].y", "float", 12, 4),
            new("origin.x", "float", 16, 4),
            new("origin.y", "float", 20, 4)
        ]
    };
}
