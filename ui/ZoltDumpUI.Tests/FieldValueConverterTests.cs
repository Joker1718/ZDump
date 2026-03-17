using System.Buffers.Binary;
using Xunit;
using ZoltDumpUI.Core;

namespace ZoltDumpUI.Tests;

public class FieldValueConverterTests
{
    // ================================================================
    // Float types
    // ================================================================

    [Theory]
    [InlineData("FLOAT", "3.14")]
    [InlineData("float", "3.14")]
    public void Float_ProducesCorrectBytes(string typeName, string value)
    {
        var result = FieldValueConverter.TryConvert(typeName, value);
        Assert.NotNull(result);
        Assert.Equal(4, result.Length);
        var f = BinaryPrimitives.ReadSingleLittleEndian(result);
        Assert.Equal(3.14f, f, 0.001f);
    }

    [Theory]
    [InlineData("DOUBLE", "3.14")]
    [InlineData("double", "3.14")]
    public void Double_ProducesCorrectBytes(string typeName, string value)
    {
        var result = FieldValueConverter.TryConvert(typeName, value);
        Assert.NotNull(result);
        Assert.Equal(8, result.Length);
        var d = BinaryPrimitives.ReadDoubleLittleEndian(result);
        Assert.Equal(3.14, d, 0.0001);
    }

    // ================================================================
    // Godot 3.x "real" type → Double (8 bytes)
    // ================================================================

    [Fact]
    public void Real_MapsToDouble()
    {
        var result = FieldValueConverter.TryConvert("real", "2.5");
        Assert.NotNull(result);
        Assert.Equal(8, result.Length);
        var d = BinaryPrimitives.ReadDoubleLittleEndian(result);
        Assert.Equal(2.5, d);
    }

    [Fact]
    public void Real_NegativeValue()
    {
        var result = FieldValueConverter.TryConvert("real", "-1.5");
        Assert.NotNull(result);
        var d = BinaryPrimitives.ReadDoubleLittleEndian(result);
        Assert.Equal(-1.5, d);
    }

    [Fact]
    public void Real_Zero()
    {
        var result = FieldValueConverter.TryConvert("real", "0");
        Assert.NotNull(result);
        var d = BinaryPrimitives.ReadDoubleLittleEndian(result);
        Assert.Equal(0.0, d);
    }

    // ================================================================
    // Integer types
    // ================================================================

    [Theory]
    [InlineData("INT", "42")]
    [InlineData("int", "42")]
    public void Int_Decimal(string typeName, string value)
    {
        var result = FieldValueConverter.TryConvert(typeName, value);
        Assert.NotNull(result);
        Assert.Equal(8, result.Length);
        var i = BinaryPrimitives.ReadInt64LittleEndian(result);
        Assert.Equal(42L, i);
    }

    [Fact]
    public void Int_NegativeValue()
    {
        var result = FieldValueConverter.TryConvert("INT", "-100");
        Assert.NotNull(result);
        var i = BinaryPrimitives.ReadInt64LittleEndian(result);
        Assert.Equal(-100L, i);
    }

    [Theory]
    [InlineData("INT", "0xFF", 255)]
    [InlineData("int", "0x1A2B", 0x1A2B)]
    [InlineData("INT", "0x0", 0)]
    public void Int_HexPrefix(string typeName, string value, long expected)
    {
        var result = FieldValueConverter.TryConvert(typeName, value);
        Assert.NotNull(result);
        var i = BinaryPrimitives.ReadInt64LittleEndian(result);
        Assert.Equal(expected, i);
    }

    // ================================================================
    // Bool types
    // ================================================================

    [Theory]
    [InlineData("BOOL", "true", 1)]
    [InlineData("bool", "true", 1)]
    [InlineData("BOOL", "1", 1)]
    [InlineData("BOOL", "false", 0)]
    [InlineData("bool", "false", 0)]
    [InlineData("BOOL", "0", 0)]
    [InlineData("BOOL", "TRUE", 1)]
    [InlineData("BOOL", "False", 0)]
    public void Bool_CorrectMapping(string typeName, string value, byte expected)
    {
        var result = FieldValueConverter.TryConvert(typeName, value);
        Assert.NotNull(result);
        Assert.Single(result);
        Assert.Equal(expected, result[0]);
    }

    // ================================================================
    // Edge cases — null / empty / invalid
    // ================================================================

    [Theory]
    [InlineData("FLOAT", "")]
    [InlineData("INT", "")]
    [InlineData("BOOL", "")]
    [InlineData("real", "")]
    [InlineData("FLOAT", "  ")]
    public void EmptyOrWhitespace_ReturnsNull(string typeName, string value)
    {
        Assert.Null(FieldValueConverter.TryConvert(typeName, value));
    }

    [Theory]
    [InlineData("FLOAT", "abc")]
    [InlineData("INT", "xyz")]
    [InlineData("BOOL", "maybe")]
    [InlineData("real", "notanumber")]
    public void InvalidValue_ReturnsNull(string typeName, string value)
    {
        Assert.Null(FieldValueConverter.TryConvert(typeName, value));
    }

    [Theory]
    [InlineData("VECTOR3", "1,2,3")]
    [InlineData("COLOR", "red")]
    [InlineData("STRING", "hello")]
    [InlineData("OBJECT", "ptr")]
    [InlineData("UNKNOWN_TYPE", "42")]
    public void UnsupportedType_ReturnsNull(string typeName, string value)
    {
        Assert.Null(FieldValueConverter.TryConvert(typeName, value));
    }

    [Fact]
    public void NullValue_ReturnsNull()
    {
        Assert.Null(FieldValueConverter.TryConvert("INT", null!));
    }

    // ================================================================
    // Whitespace trimming
    // ================================================================

    [Fact]
    public void TrimsWhitespace()
    {
        var result = FieldValueConverter.TryConvert("INT", "  42  ");
        Assert.NotNull(result);
        var i = BinaryPrimitives.ReadInt64LittleEndian(result);
        Assert.Equal(42L, i);
    }

    [Fact]
    public void Float_TrimsWhitespace()
    {
        var result = FieldValueConverter.TryConvert("FLOAT", "  1.5  ");
        Assert.NotNull(result);
        var f = BinaryPrimitives.ReadSingleLittleEndian(result);
        Assert.Equal(1.5f, f);
    }
}
