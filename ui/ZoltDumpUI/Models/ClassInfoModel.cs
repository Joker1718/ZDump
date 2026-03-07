namespace ZoltDumpUI.Models;

/// <summary>Class definition from ClassDB via "walk_class" command.</summary>
public sealed class ClassInfoModel
{
    public string ClassName { get; init; } = "";
    public string SuperName { get; init; } = "";
    public string ClassAddr { get; init; } = "";
    public int PropertiesCount { get; init; }
    public int MethodCount { get; init; }
    public List<FieldInfoModel> Properties { get; init; } = [];
    public List<MethodInfoEntry> Methods { get; init; } = [];
}

/// <summary>Single property in a class definition.</summary>
public sealed class FieldInfoModel
{
    public string Name { get; init; } = "";
    public string Type { get; init; } = "";
    public int Offset { get; init; }
    public int Size { get; init; }
    public string Hint { get; init; } = "";
    public string HintString { get; init; } = "";
    public int Usage { get; init; }
}

/// <summary>Single method in a class definition.</summary>
public sealed class MethodInfoEntry
{
    public string Name { get; init; } = "";
    public string ReturnType { get; init; } = "";
    public string Address { get; init; } = "";
    public List<MethodArgEntry> Args { get; init; } = [];
    public int Flags { get; init; }
}

/// <summary>Single method argument.</summary>
public sealed class MethodArgEntry
{
    public string Name { get; init; } = "";
    public string Type { get; init; } = "";
}
