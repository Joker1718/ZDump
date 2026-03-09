using Xunit;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Tests;

public class KnownStructLayoutTests
{
    [Theory]
    [InlineData("Vector2", 2, 8)]
    [InlineData("Vector3", 3, 12)]
    [InlineData("Vector4", 4, 16)]
    [InlineData("Color", 4, 16)]
    [InlineData("Quaternion", 4, 16)]
    [InlineData("Rect2", 4, 16)]
    [InlineData("AABB", 6, 24)]
    [InlineData("Plane", 4, 16)]
    [InlineData("Basis", 9, 36)]
    [InlineData("Transform3D", 12, 48)]
    [InlineData("Transform2D", 6, 24)]
    public void Layout_HasCorrectFieldCount_And_TotalSize(string typeName, int expectedFields, int expectedSize)
    {
        Assert.True(KnownStructLayouts.Layouts.ContainsKey(typeName),
            $"Layout missing for {typeName}");

        var fields = KnownStructLayouts.Layouts[typeName];
        Assert.Equal(expectedFields, fields.Length);

        // Total size = last field offset + last field size
        var last = fields[^1];
        Assert.Equal(expectedSize, last.Offset + last.Size);
    }

    [Fact]
    public void Layout_FieldsAreContiguous()
    {
        foreach (var (typeName, fields) in KnownStructLayouts.Layouts)
        {
            Assert.Equal(0, fields[0].Offset);

            for (int i = 1; i < fields.Length; i++)
            {
                var prev = fields[i - 1];
                Assert.True(prev.Offset + prev.Size == fields[i].Offset,
                    $"{typeName}: gap between {prev.Name} and {fields[i].Name} " +
                    $"(expected {prev.Offset + prev.Size}, got {fields[i].Offset})");
            }
        }
    }

    [Fact]
    public void Layout_AllFieldsHaveNames()
    {
        foreach (var (typeName, fields) in KnownStructLayouts.Layouts)
        {
            foreach (var field in fields)
            {
                Assert.False(string.IsNullOrEmpty(field.Name),
                    $"{typeName}: field at offset {field.Offset} has no name");
            }
        }
    }

    [Fact]
    public void Layout_IntegerVariants_UseInt32()
    {
        var intTypes = new[] { "Vector2i", "Vector3i", "Vector4i", "Rect2i" };
        foreach (var typeName in intTypes)
        {
            Assert.True(KnownStructLayouts.Layouts.ContainsKey(typeName),
                $"Integer variant layout missing: {typeName}");

            foreach (var field in KnownStructLayouts.Layouts[typeName])
            {
                Assert.True(field.CppType == "int32_t",
                    $"{typeName}.{field.Name} should be int32_t, got {field.CppType}");
            }
        }
    }

    [Fact]
    public void Layout_FloatVariants_UseFloat()
    {
        var floatTypes = new[] { "Vector2", "Vector3", "Vector4", "Color", "Quaternion" };
        foreach (var typeName in floatTypes)
        {
            foreach (var field in KnownStructLayouts.Layouts[typeName])
            {
                Assert.True(field.CppType == "float",
                    $"{typeName}.{field.Name} should be float, got {field.CppType}");
            }
        }
    }
}
