namespace ZoltDumpUI.Models;

/// <summary>
/// Value-based filter criteria for CMD_FIND_INSTANCE_WITH_MEMBER.
/// Used to disambiguate when multiple objects share the same member name.
/// </summary>
public sealed class MemberFilterCriteria
{
    /// <summary>Target member name (the one to resolve address for).</summary>
    public string TargetMember { get; init; } = "";

    /// <summary>FilterOp for target member value (0=none).</summary>
    public int TargetOp { get; set; }

    /// <summary>Comparison value for target member.</summary>
    public long TargetValue { get; set; }

    /// <summary>Secondary filter member name (empty = no filter).</summary>
    public string FilterMember { get; set; } = "";

    /// <summary>FilterOp for filter member value (0=none).</summary>
    public int FilterOp { get; set; }

    /// <summary>Comparison value for filter member.</summary>
    public long FilterValue { get; set; }
}
