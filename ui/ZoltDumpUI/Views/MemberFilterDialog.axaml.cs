using Avalonia.Controls;
using Avalonia.Interactivity;
using ZoltDumpUI.Models;
using ZoltDumpUI.Services;

namespace ZoltDumpUI.Views;

/// <summary>
/// Dialog for setting value criteria to disambiguate CMD_FIND_INSTANCE_WITH_MEMBER
/// (GDScript mode) or CMD_FIND_MANAGED_INSTANCES (.NET managed mode).
/// Returns a <see cref="MemberFilterCriteria"/> when Generate is clicked, or null on Cancel.
/// AOT-safe: no reflection, no data binding — direct control access.
/// </summary>
public partial class MemberFilterDialog : Window
{
    private readonly string _targetMember;
    private readonly List<string> _siblingMembers;
    private readonly bool _isManagedMode;
    private readonly int _targetFieldOffset;
    private readonly List<ManagedFieldInfo> _managedSiblings;

    /// <summary>The result criteria, or null if cancelled.</summary>
    public MemberFilterCriteria? Result { get; private set; }

    /// <summary>Info about a managed field for the filter dropdown.</summary>
    public record ManagedFieldInfo(string Name, int Offset, string Type, string CurrentValue, int Size = 4);

    public MemberFilterDialog()
    {
        InitializeComponent();
        _targetMember = "";
        _siblingMembers = [];
        _managedSiblings = [];
    }

    /// <summary>
    /// Create a dialog for setting filter criteria (GDScript mode).
    /// </summary>
    /// <param name="targetMember">The member to resolve (e.g. "gold")</param>
    /// <param name="siblingMembers">Other base-type script members on the same object</param>
    public MemberFilterDialog(string targetMember, List<string> siblingMembers) : this()
    {
        _targetMember = targetMember;
        _siblingMembers = siblingMembers;
        _managedSiblings = [];

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

    /// <summary>
    /// Create a dialog for setting filter criteria (.NET managed mode).
    /// Shows managed field offsets in the filter dropdown and a "Find All" checkbox.
    /// </summary>
    /// <param name="targetMember">Target field display name (e.g. "+0x84")</param>
    /// <param name="targetFieldOffset">Byte offset of the target field in the managed object</param>
    /// <param name="managedSiblings">Other managed fields for the filter dropdown</param>
    public MemberFilterDialog(string targetMember, int targetFieldOffset,
        List<ManagedFieldInfo> managedSiblings) : this()
    {
        _targetMember = targetMember;
        _siblingMembers = [];
        _isManagedMode = true;
        _targetFieldOffset = targetFieldOffset;
        _managedSiblings = managedSiblings;

        // Show field name if Metode-resolved, otherwise raw offset
        bool hasFieldName = !targetMember.StartsWith("+0x", StringComparison.OrdinalIgnoreCase);
        TargetLabel.Text = hasFieldName
            ? $"Target: {targetMember} (+0x{targetFieldOffset:X})"
            : $"Target: +0x{targetFieldOffset:X}";

        // Show "Find all" checkbox in managed mode
        FindAllCheck.IsVisible = true;
        FindAllCheck.IsChecked = true; // default to find all for managed objects

        // Populate filter member dropdown with managed fields
        FilterMemberCombo.Items.Clear();
        FilterMemberCombo.Items.Add(new ComboBoxItem { Content = "(no filter)", Tag = "" });
        foreach (var f in managedSiblings)
        {
            // Show field name if Metode-resolved, otherwise raw offset
            bool namedField = !f.Name.StartsWith("+0x", StringComparison.OrdinalIgnoreCase);
            string label = namedField
                ? $"{f.Name} +0x{f.Offset:X} ({f.Type}, current={f.CurrentValue})"
                : $"+0x{f.Offset:X} ({f.Type}, current={f.CurrentValue})";
            FilterMemberCombo.Items.Add(new ComboBoxItem
            {
                Content = label,
                Tag = f.Offset.ToString()
            });
        }
        FilterMemberCombo.SelectedIndex = managedSiblings.Count > 0 ? 1 : 0;

        // Wire up buttons
        GenerateButton.Click += OnGenerate;
        CancelButton.Click += OnCancel;
    }

    private void OnGenerate(object? sender, RoutedEventArgs e)
    {
        int targetOp = GetSelectedOp(TargetOpCombo);
        long targetValue = ParseLong(TargetValueBox.Text);

        int filterOp = GetSelectedOp(FilterOpCombo);
        long filterValue = ParseLong(FilterValueBox.Text);

        if (_isManagedMode)
        {
            // Managed mode: filter by offset
            int filterOffset = -1;
            int filterSize = 4;
            string filterFieldName = "";
            if (FilterMemberCombo.SelectedItem is ComboBoxItem filterItem)
            {
                string? tag = filterItem.Tag?.ToString();
                if (!string.IsNullOrEmpty(tag) && int.TryParse(tag, out int off))
                {
                    filterOffset = off;
                    // Find size and field name from managed siblings
                    var match = _managedSiblings.Find(f => f.Offset == off);
                    if (match is not null)
                    {
                        filterSize = match.Size;
                        // Use field name if it's a Metode-resolved name (not raw offset)
                        if (!match.Name.StartsWith("+0x", StringComparison.OrdinalIgnoreCase))
                            filterFieldName = match.Name;
                    }
                }
            }

            Result = new MemberFilterCriteria
            {
                TargetMember = _targetMember,
                TargetOp = targetOp,
                TargetValue = targetValue,
                FilterOp = filterOp,
                FilterValue = filterValue,
                IsManagedMode = true,
                FindAll = FindAllCheck.IsChecked == true,
                TargetFieldOffset = _targetFieldOffset,
                FilterFieldOffset = filterOffset,
                FilterFieldSize = filterSize,
                FilterFieldName = filterFieldName,
            };
        }
        else
        {
            // GDScript mode: filter by member name
            string filterMember = "";
            if (FilterMemberCombo.SelectedItem is ComboBoxItem filterItem)
            {
                filterMember = filterItem.Tag?.ToString() ?? "";
            }

            Result = new MemberFilterCriteria
            {
                TargetMember = _targetMember,
                TargetOp = targetOp,
                TargetValue = targetValue,
                FilterMember = filterMember,
                FilterOp = filterOp,
                FilterValue = filterValue,
            };
        }

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
