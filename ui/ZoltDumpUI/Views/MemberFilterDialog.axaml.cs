using Avalonia.Controls;
using Avalonia.Interactivity;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Views;

/// <summary>
/// Dialog for setting value criteria to disambiguate CMD_FIND_INSTANCE_WITH_MEMBER.
/// Returns a <see cref="MemberFilterCriteria"/> when Generate is clicked, or null on Cancel.
/// AOT-safe: no reflection, no data binding — direct control access.
/// </summary>
public partial class MemberFilterDialog : Window
{
    private readonly string _targetMember;
    private readonly List<string> _siblingMembers;

    /// <summary>The result criteria, or null if cancelled.</summary>
    public MemberFilterCriteria? Result { get; private set; }

    public MemberFilterDialog()
    {
        InitializeComponent();
        _targetMember = "";
        _siblingMembers = [];
    }

    /// <summary>
    /// Create a dialog for setting filter criteria.
    /// </summary>
    /// <param name="targetMember">The member to resolve (e.g. "gold")</param>
    /// <param name="siblingMembers">Other base-type script members on the same object</param>
    public MemberFilterDialog(string targetMember, List<string> siblingMembers) : this()
    {
        _targetMember = targetMember;
        _siblingMembers = siblingMembers;

        TargetLabel.Text = $"Target: {targetMember}";

        // Populate filter member dropdown
        FilterMemberCombo.Items.Clear();
        FilterMemberCombo.Items.Add(new ComboBoxItem { Content = "(no filter)", Tag = "" });
        foreach (var m in siblingMembers)
        {
            FilterMemberCombo.Items.Add(new ComboBoxItem { Content = m, Tag = m });
        }
        FilterMemberCombo.SelectedIndex = siblingMembers.Count > 0 ? 1 : 0;

        // Wire up buttons
        GenerateButton.Click += OnGenerate;
        CancelButton.Click += OnCancel;
    }

    private void OnGenerate(object? sender, RoutedEventArgs e)
    {
        int targetOp = GetSelectedOp(TargetOpCombo);
        long targetValue = ParseLong(TargetValueBox.Text);

        string filterMember = "";
        if (FilterMemberCombo.SelectedItem is ComboBoxItem filterItem)
        {
            filterMember = filterItem.Tag?.ToString() ?? "";
        }

        int filterOp = GetSelectedOp(FilterOpCombo);
        long filterValue = ParseLong(FilterValueBox.Text);

        Result = new MemberFilterCriteria
        {
            TargetMember = _targetMember,
            TargetOp = targetOp,
            TargetValue = targetValue,
            FilterMember = filterMember,
            FilterOp = filterOp,
            FilterValue = filterValue,
        };

        Close(true);
    }

    private void OnCancel(object? sender, RoutedEventArgs e)
    {
        Result = null;
        Close(false);
    }

    private static int GetSelectedOp(ComboBox combo)
    {
        if (combo.SelectedItem is ComboBoxItem item && item.Tag is string tagStr)
        {
            return int.TryParse(tagStr, out int val) ? val : CeLuaScriptService.FILTER_NONE;
        }
        return CeLuaScriptService.FILTER_NONE;
    }

    private static long ParseLong(string? text)
    {
        if (string.IsNullOrWhiteSpace(text)) return 0;
        text = text.Trim();
        // Support hex with 0x prefix
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            return long.TryParse(text.AsSpan(2), System.Globalization.NumberStyles.HexNumber,
                null, out long hexVal) ? hexVal : 0;
        }
        return long.TryParse(text, out long val) ? val : 0;
    }
}
