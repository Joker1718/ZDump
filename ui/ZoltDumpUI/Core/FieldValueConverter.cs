using System.Buffers.Binary;
using System.Globalization;

namespace ZoltDumpUI.Core;

/// <summary>Converts user-entered string values to raw bytes for memory writes.</summary>
public static class FieldValueConverter
{
    public static byte[]? TryConvert(string typeName, string value)
    {
        if (string.IsNullOrWhiteSpace(value)) return null;
        var v = value.Trim();

        return typeName switch
        {
            "FLOAT" or "float" => TryWriteFloat(v),
            "DOUBLE" or "double" => TryWriteDouble(v),
            "INT" or "int" when v.StartsWith("0x", StringComparison.OrdinalIgnoreCase) => TryWriteHexInt64(v),
            "INT" or "int" => TryWriteInt64(v),
            "BOOL" or "bool" => TryWriteBool(v),
            _ => null
        };
    }

    private static byte[]? TryWriteFloat(string v)
    {
        if (!float.TryParse(v, CultureInfo.InvariantCulture, out var f)) return null;
        var buf = new byte[4];
        BinaryPrimitives.WriteSingleLittleEndian(buf, f);
        return buf;
    }

    private static byte[]? TryWriteDouble(string v)
    {
        if (!double.TryParse(v, CultureInfo.InvariantCulture, out var d)) return null;
        var buf = new byte[8];
        BinaryPrimitives.WriteDoubleLittleEndian(buf, d);
        return buf;
    }

    private static byte[]? TryWriteInt64(string v)
    {
        if (!long.TryParse(v, CultureInfo.InvariantCulture, out var i)) return null;
        var buf = new byte[8];
        BinaryPrimitives.WriteInt64LittleEndian(buf, i);
        return buf;
    }

    private static byte[]? TryWriteHexInt64(string v)
    {
        var hex = v[2..];
        if (!long.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var i)) return null;
        var buf = new byte[8];
        BinaryPrimitives.WriteInt64LittleEndian(buf, i);
        return buf;
    }

    private static byte[]? TryWriteBool(string v)
    {
        return v.ToLowerInvariant() switch
        {
            "true" or "1" => [1],
            "false" or "0" => [0],
            _ => null
        };
    }
}
