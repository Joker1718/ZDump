using Xunit;
using ZoltDumpUI.Core;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Tests;

public class CeXmlExportServiceTests
{
    // CeXmlExportService requires IDumpService in ctor but the tested methods
    // (GenerateHierarchicalXml, GenerateInstanceXml, GenerateFieldXml) don't use it.
    private readonly CeXmlExportService _service = new(null!);

    // ================================================================
    // MapVariantToCeType
    // ================================================================

    [Theory]
    [InlineData("BOOL", 1, "Byte")]
    [InlineData("bool", 1, "Byte")]
    [InlineData("INT", 1, "Byte")]
    [InlineData("INT", 2, "2 Bytes")]
    [InlineData("INT", 4, "4 Bytes")]
    [InlineData("INT", 8, "8 Bytes")]
    [InlineData("int", 8, "8 Bytes")]
    [InlineData("FLOAT", 4, "Float")]
    [InlineData("FLOAT", 8, "Double")]
    [InlineData("float", 4, "Float")]
    [InlineData("STRING", 8, "String")]
    [InlineData("STRING_NAME", 8, "String")]
    [InlineData("NODE_PATH", 8, "String")]
    [InlineData("OBJECT", 8, "Pointer")]
    [InlineData("VECTOR3", 12, "Group")]
    [InlineData("COLOR", 16, "Group")]
    [InlineData("UNKNOWN", 4, "4 Bytes")]
    // Godot 3.x type names
    [InlineData("real", 8, "Double")]
    [InlineData("Quat", 16, "Group")]
    [InlineData("Transform", 48, "Group")]
    public void MapVariantToCeType_CorrectMappings(string type, int size, string expected)
    {
        Assert.Equal(expected, CeXmlExportService.MapVariantToCeType(type, size));
    }

    // ================================================================
    // CompositeChildren
    // ================================================================

    [Fact]
    public void CompositeChildren_ContainsExpectedTypes()
    {
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("VECTOR2"));
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("VECTOR3"));
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("VECTOR4"));
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("COLOR"));
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("QUATERNION"));
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("TRANSFORM3D"));
    }

    [Fact]
    public void CompositeChildren_CaseInsensitive()
    {
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("vector3"));
        Assert.True(CeXmlExportService.CompositeChildren.ContainsKey("Vector3"));
    }

    [Fact]
    public void CompositeChildren_Vector3_HasXYZ()
    {
        var children = CeXmlExportService.CompositeChildren["VECTOR3"];
        Assert.Equal(["x", "y", "z"], children);
    }

    [Fact]
    public void CompositeChildren_Color_HasRGBA()
    {
        var children = CeXmlExportService.CompositeChildren["COLOR"];
        Assert.Equal(["r", "g", "b", "a"], children);
    }

    // ================================================================
    // CleanBreadcrumbs
    // ================================================================

    [Fact]
    public void CleanBreadcrumbs_NoCycle_ReturnsUnchanged()
    {
        var input = new List<BreadcrumbEntry>
        {
            new() { Address = "100", Name = "Root" },
            new() { Address = "200", Name = "Child" },
            new() { Address = "300", Name = "Leaf" },
        };

        var result = CeXmlExportService.CleanBreadcrumbs(input);
        Assert.Equal(3, result.Count);
    }

    [Fact]
    public void CleanBreadcrumbs_RemovesCycle()
    {
        var input = new List<BreadcrumbEntry>
        {
            new() { Address = "100", Name = "A" },
            new() { Address = "200", Name = "B" },
            new() { Address = "300", Name = "C" },
            new() { Address = "100", Name = "A_again" }, // cycle back to 100
            new() { Address = "400", Name = "D" },
        };

        var result = CeXmlExportService.CleanBreadcrumbs(input);
        // Cycle 100->200->300->100 should collapse, keeping first 100 and continuing with 400
        Assert.Equal(2, result.Count);
        Assert.Equal("100", result[0].Address);
        Assert.Equal("400", result[1].Address);
    }

    [Fact]
    public void CleanBreadcrumbs_Empty_ReturnsEmpty()
    {
        Assert.Empty(CeXmlExportService.CleanBreadcrumbs([]));
    }

    // ================================================================
    // GenerateInstanceXml
    // ================================================================

    [Fact]
    public void GenerateInstanceXml_EmptyFields_ReturnsEmpty()
    {
        var result = _service.GenerateInstanceXml("ABC", "Player", [], "game.exe");
        Assert.Equal("", result);
    }

    [Fact]
    public void GenerateInstanceXml_HasValidStructure()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "health", Type = "INT", Offset = 0x10, Size = 4 },
        };

        var result = _service.GenerateInstanceXml("ABC", "Player", fields, "game.exe");

        Assert.Contains("<?xml version=\"1.0\"", result);
        Assert.Contains("<CheatTable>", result);
        Assert.Contains("<CheatEntries>", result);
        Assert.Contains("\"Player\"", result);
        Assert.Contains("health (INT)", result);
        Assert.Contains("+10", result); // Offset 0x10
        Assert.Contains("4 Bytes", result);
        Assert.Contains("</CheatTable>", result);
    }

    [Fact]
    public void GenerateInstanceXml_CompositeField_ExpandsChildren()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "position", Type = "VECTOR3", Offset = 0x20, Size = 12 },
        };

        var result = _service.GenerateInstanceXml("ABC", "Node", fields, "");

        Assert.Contains("position (VECTOR3)", result);
        Assert.Contains("\"x\"", result);
        Assert.Contains("\"y\"", result);
        Assert.Contains("\"z\"", result);
        Assert.Contains("Float", result);
    }

    [Fact]
    public void GenerateInstanceXml_PointerField_HasOffsets()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "parent", Type = "OBJECT", Offset = 0x30, Size = 8, IsPointer = true },
        };

        var result = _service.GenerateInstanceXml("ABC", "Node", fields, "");

        Assert.Contains("<Offset>0</Offset>", result);
        Assert.Contains("ShowAsHex", result);
    }

    [Fact]
    public void GenerateInstanceXml_SkipsNegativeOffset()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "unknown", Type = "INT", Offset = -1, Size = 4 },
            new() { Name = "known", Type = "INT", Offset = 0x10, Size = 4 },
        };

        var result = _service.GenerateInstanceXml("ABC", "Node", fields, "");

        Assert.DoesNotContain("unknown", result);
        Assert.Contains("known", result);
    }

    [Fact]
    public void GenerateInstanceXml_ContainerField()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "items", Type = "ARRAY", Offset = 0x40, Size = 8, IsContainer = true, ElementCount = 5 },
        };

        var result = _service.GenerateInstanceXml("ABC", "Inventory", fields, "");

        Assert.Contains("items (ARRAY[5])", result);
        Assert.Contains("GroupHeader", result);
    }

    [Fact]
    public void GenerateInstanceXml_XmlEscaping()
    {
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "a<b>c&d", Type = "INT", Offset = 0, Size = 4 },
        };

        var result = _service.GenerateInstanceXml("ABC", "Test", fields, "");

        Assert.Contains("a&lt;b&gt;c&amp;d", result);
    }

    // ================================================================
    // GenerateFieldXml
    // ================================================================

    [Fact]
    public void GenerateFieldXml_LeafField()
    {
        var field = new LiveFieldValue { Name = "hp", Type = "INT", Offset = 0x10, Size = 4 };
        var result = _service.GenerateFieldXml(field);

        Assert.Contains("<CheatTable>", result);
        Assert.Contains("hp (INT)", result);
        Assert.Contains("4 Bytes", result);
    }

    [Fact]
    public void GenerateFieldXml_CompositeField()
    {
        var field = new LiveFieldValue { Name = "color", Type = "COLOR", Offset = 0x20, Size = 16 };
        var result = _service.GenerateFieldXml(field);

        Assert.Contains("color (COLOR)", result);
        Assert.Contains("\"r\"", result);
        Assert.Contains("\"g\"", result);
        Assert.Contains("\"b\"", result);
        Assert.Contains("\"a\"", result);
    }

    // ================================================================
    // GenerateHierarchicalXml
    // ================================================================

    [Fact]
    public void GenerateHierarchicalXml_EmptyBreadcrumbsAndFields_ReturnsEmpty()
    {
        var result = _service.GenerateHierarchicalXml([], [], "", AddressFormat.HexNoPrefix);
        Assert.Equal("", result);
    }

    [Fact]
    public void GenerateHierarchicalXml_WithBreadcrumbs()
    {
        var breadcrumbs = new List<BreadcrumbEntry>
        {
            new() { Address = "7FF742C84100", Name = "ObjectDB", ClassName = "", Offset = 0, ModuleRva = "1234" },
            new() { Address = "200", Name = "Player", ClassName = "CharacterBody2D", Offset = 0x50, IsPointer = true },
        };
        var fields = new List<LiveFieldValue>
        {
            new() { Name = "health", Type = "INT", Offset = 0x10, Size = 4 },
        };

        var result = _service.GenerateHierarchicalXml(breadcrumbs, fields, "game.exe", AddressFormat.HexNoPrefix);

        Assert.Contains("ObjectDB", result);
        Assert.Contains("Player (CharacterBody2D)", result);
        Assert.Contains("health (INT)", result);
        Assert.Contains("\"game.exe\"+1234", result); // ModuleRva used for root
    }
}
