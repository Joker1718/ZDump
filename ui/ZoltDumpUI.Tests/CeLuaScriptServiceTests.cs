using Xunit;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Tests;

public class CeLuaScriptServiceTests
{
    private readonly CeLuaScriptService _svc = new();

    // ================================================================
    // MakeSymbolName / SanitizeSymbolName
    // ================================================================

    [Theory]
    [InlineData("Main", "gold", "Main_gold")]
    [InlineData("Player", "HP", "Player_HP")]
    [InlineData("Wave-Timer", "time_left", "Wave_Timer_time_left")]
    public void MakeSymbolName_CombinesNodeAndMember(string node, string member, string expected)
    {
        Assert.Equal(expected, CeLuaScriptService.MakeSymbolName(node, member));
    }

    [Theory]
    [InlineData("abc_123", "abc_123")]
    [InlineData("my-node", "my_node")]
    [InlineData("Hello World!", "Hello_World_")]
    [InlineData("Node@2", "Node_2")]
    [InlineData("", "zolt_symbol")]
    public void SanitizeSymbolName_ReplacesInvalidChars(string input, string expected)
    {
        Assert.Equal(expected, CeLuaScriptService.SanitizeSymbolName(input));
    }

    // ================================================================
    // GenerateMemberScript — GDScript member on Node (two-step)
    // ================================================================

    [Fact]
    public void GenerateMemberScript_ContainsEnableDisable()
    {
        var script = _svc.GenerateMemberScript("Main", "gold");
        Assert.Contains("[ENABLE]", script);
        Assert.Contains("[DISABLE]", script);
    }

    [Fact]
    public void GenerateMemberScript_ContainsBothSteps()
    {
        var script = _svc.GenerateMemberScript("Player", "HP");
        // Step 1: FIND_NODE
        Assert.Contains("FIND_NODE", script);
        Assert.Contains("writeString(mb + 0x228, 'Player')", script);
        // Step 2: GET_MEMBER_ADDR
        Assert.Contains("GET_MEMBER_ADDR", script);
        Assert.Contains("writeString(mb + 0x128, 'HP')", script);
    }

    [Fact]
    public void GenerateMemberScript_RegistersSymbol()
    {
        var script = _svc.GenerateMemberScript("Main", "gold", "Main_gold");
        Assert.Contains("registerSymbol('Main_gold'", script);
        Assert.Contains("unregisterSymbol('Main_gold')", script);
    }

    [Fact]
    public void GenerateMemberScript_DefaultSymbolName()
    {
        var script = _svc.GenerateMemberScript("Main", "gold");
        Assert.Contains("registerSymbol('Main_gold'", script);
    }

    [Fact]
    public void GenerateMemberScript_SyntaxCheckGuard()
    {
        var script = _svc.GenerateMemberScript("X", "y");
        Assert.Contains("if syntaxcheck then return end", script);
    }

    [Fact]
    public void GenerateMemberScript_MailboxInit()
    {
        var script = _svc.GenerateMemberScript("X", "y");
        Assert.Contains("getAddress('g_zoltMailbox')", script);
        Assert.Contains("proxy DLL not loaded", script);
    }

    [Fact]
    public void GenerateMemberScript_NullTerminatesStrings()
    {
        var script = _svc.GenerateMemberScript("Main", "gold");
        // Node name null-terminator: offset 0x228 + len("Main") = 4
        Assert.Contains("writeByte(mb + 0x228 + 4, 0)", script);
        // Member name null-terminator: offset 0x128 + len("gold") = 4
        Assert.Contains("writeByte(mb + 0x128 + 4, 0)", script);
    }

    [Fact]
    public void GenerateMemberScript_EscapesSingleQuotes()
    {
        var script = _svc.GenerateMemberScript("it's", "member");
        Assert.Contains("it\\'s", script);
    }

    // ================================================================
    // GenerateNativeOffsetScript — native Node property (FIND_NODE + offset)
    // ================================================================

    [Fact]
    public void GenerateNativeOffsetScript_ContainsOffset()
    {
        var script = _svc.GenerateNativeOffsetScript("WaveTimer", "time_left", 0x1E0);
        Assert.Contains("native offset +0x1E0", script);
        Assert.Contains("local addr = objAddr + 0x1E0", script);
    }

    [Fact]
    public void GenerateNativeOffsetScript_UsesFindNodeOnly()
    {
        var script = _svc.GenerateNativeOffsetScript("WaveTimer", "time_left", 0x1E0);
        // Step 1: FIND_NODE is present
        Assert.Contains("FIND_NODE", script);
        Assert.Contains("writeString(mb + 0x228, 'WaveTimer')", script);
        // Step 2: NO GET_MEMBER_ADDR — uses direct offset
        Assert.DoesNotContain("GET_MEMBER_ADDR", script);
        Assert.DoesNotContain("writeString(mb + 0x128", script);
    }

    [Fact]
    public void GenerateNativeOffsetScript_RegistersSymbol()
    {
        var script = _svc.GenerateNativeOffsetScript("WaveTimer", "time_left", 0x1E0, "WaveTimer_time_left");
        Assert.Contains("registerSymbol('WaveTimer_time_left'", script);
        Assert.Contains("unregisterSymbol('WaveTimer_time_left')", script);
    }

    [Fact]
    public void GenerateNativeOffsetScript_DefaultSymbolName()
    {
        var script = _svc.GenerateNativeOffsetScript("MyTimer", "wait_time", 0x200);
        Assert.Contains("registerSymbol('MyTimer_wait_time'", script);
    }

    [Fact]
    public void GenerateNativeOffsetScript_ZeroOffset()
    {
        var script = _svc.GenerateNativeOffsetScript("Node", "prop", 0);
        Assert.Contains("local addr = objAddr + 0x0", script);
    }

    // ================================================================
    // GenerateDirectMemberScript — non-Node Reference (direct address)
    // ================================================================

    [Fact]
    public void GenerateDirectMemberScript_WritesKnownAddress()
    {
        var script = _svc.GenerateDirectMemberScript("1CB6AF05DD0", "Reference", "money");
        Assert.Contains("writeQword(mb + 0x10, 0x1CB6AF05DD0)", script);
        Assert.Contains("session-specific", script);
    }

    [Fact]
    public void GenerateDirectMemberScript_NoFindNode()
    {
        var script = _svc.GenerateDirectMemberScript("ABC", "Reference", "gold");
        Assert.DoesNotContain("FIND_NODE", script);
        Assert.Contains("GET_MEMBER_ADDR", script);
    }

    [Fact]
    public void GenerateDirectMemberScript_DefaultSymbolFromClass()
    {
        var script = _svc.GenerateDirectMemberScript("123", "Reference", "gold");
        Assert.Contains("registerSymbol('Reference_gold'", script);
    }

    // ================================================================
    // GenerateFindInstanceMemberScript — class+member search
    // ================================================================

    [Fact]
    public void GenerateFindInstanceMemberScript_NoCriteria()
    {
        var script = _svc.GenerateFindInstanceMemberScript("Reference", "gold");
        Assert.Contains("FIND_INSTANCE_WITH_MEMBER", script);
        Assert.Contains("writeString(mb + 0x28, 'Reference')", script);
        Assert.Contains("writeString(mb + 0x128, 'gold')", script);
        Assert.Contains("local target_op    = 0", script);
        Assert.Contains("local filter_op    = 0", script);
    }

    [Fact]
    public void GenerateFindInstanceMemberScript_WithTargetCriteria()
    {
        var criteria = new MemberFilterCriteria
        {
            TargetMember = "gold",
            TargetOp = CeLuaScriptService.FILTER_GT,
            TargetValue = 100
        };
        var script = _svc.GenerateFindInstanceMemberScript("Reference", "gold", criteria);
        Assert.Contains("local target_op    = 1", script);
        Assert.Contains("local target_value = 100", script);
        Assert.Contains("GT (>)", script);
    }

    [Fact]
    public void GenerateFindInstanceMemberScript_WithFilterCriteria()
    {
        var criteria = new MemberFilterCriteria
        {
            TargetMember = "gold",
            TargetOp = CeLuaScriptService.FILTER_NONE,
            FilterMember = "level",
            FilterOp = CeLuaScriptService.FILTER_EQ,
            FilterValue = 5
        };
        var script = _svc.GenerateFindInstanceMemberScript("Reference", "gold", criteria);
        Assert.Contains("local filter_op    = 4", script);
        Assert.Contains("local filter_value = 5", script);
        // Secondary member written to nodeName field
        Assert.Contains("writeString(mb + 0x228, 'level')", script);
    }

    [Fact]
    public void GenerateFindInstanceMemberScript_NoFilterMember_ClearsNodeName()
    {
        var script = _svc.GenerateFindInstanceMemberScript("Reference", "gold");
        Assert.Contains("writeByte(mb + 0x228, 0)  -- no secondary filter member", script);
    }

    // ================================================================
    // GenerateFindNodeScript — node address only
    // ================================================================

    [Fact]
    public void GenerateFindNodeScript_Basic()
    {
        var script = _svc.GenerateFindNodeScript("Player");
        Assert.Contains("FIND_NODE", script);
        Assert.Contains("writeString(mb + 0x228, 'Player')", script);
        Assert.Contains("registerSymbol('Player'", script);
        Assert.DoesNotContain("GET_MEMBER_ADDR", script);
    }

    [Fact]
    public void GenerateFindNodeScript_CustomSymbol()
    {
        var script = _svc.GenerateFindNodeScript("Enemy", "p_Enemy");
        Assert.Contains("registerSymbol('p_Enemy'", script);
    }

    // ================================================================
    // GenerateFindInstanceScript — class instance by name
    // ================================================================

    [Fact]
    public void GenerateFindInstanceScript_Basic()
    {
        var script = _svc.GenerateFindInstanceScript("Timer");
        Assert.Contains("FIND_INSTANCE", script);
        Assert.Contains("writeString(mb + 0x28, 'Timer')", script);
        Assert.Contains("registerSymbol('Timer'", script);
    }

    // ================================================================
    // Shared structure: all scripts must have mailbox init + poll
    // ================================================================

    [Theory]
    [InlineData("member")]
    [InlineData("native")]
    [InlineData("direct")]
    [InlineData("findInstance")]
    [InlineData("findNode")]
    [InlineData("findInstanceMember")]
    public void AllScripts_HaveMailboxInitAndPoll(string type)
    {
        var script = type switch
        {
            "member" => _svc.GenerateMemberScript("N", "m"),
            "native" => _svc.GenerateNativeOffsetScript("N", "m", 0x10),
            "direct" => _svc.GenerateDirectMemberScript("123", "C", "m"),
            "findInstance" => _svc.GenerateFindInstanceScript("C"),
            "findNode" => _svc.GenerateFindNodeScript("N"),
            "findInstanceMember" => _svc.GenerateFindInstanceMemberScript("C", "m"),
            _ => throw new ArgumentException(type)
        };

        Assert.Contains("g_zoltMailbox", script);
        Assert.Contains("[ENABLE]", script);
        Assert.Contains("[DISABLE]", script);
        Assert.Contains("if syntaxcheck then return end", script);
        // Poll loop exists
        Assert.Contains("for i = 1,", script);
        Assert.Contains("sleep(", script);
    }

    // ================================================================
    // Mailbox offset constants — verify alignment with Qual.h
    // ================================================================

    [Fact]
    public void MailboxOffsets_AreExpectedValues()
    {
        // All scripts use these offsets; verify they're consistent
        var script = _svc.GenerateMemberScript("TestNode", "testMember");
        // CMD at 0x000 (writeInteger)
        Assert.Contains("mb + 0x0,", script);
        // STATUS at 0x004
        Assert.Contains("mb + 0x4,", script);
        // RESULT at 0x008
        Assert.Contains("mb + 0x8,", script);
        // OBJECT_ADDR at 0x010
        Assert.Contains("mb + 0x10,", script);
        // MEMBER_ADDR at 0x018
        Assert.Contains("mb + 0x18,", script);
        // MEMBER_NAME at 0x128
        Assert.Contains("mb + 0x128,", script);
        // NODE_NAME at 0x228
        Assert.Contains("mb + 0x228,", script);
    }
}
