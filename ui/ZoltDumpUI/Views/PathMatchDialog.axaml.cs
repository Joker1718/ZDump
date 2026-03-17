using Avalonia.Controls;
using Avalonia.Interactivity;
using ZoltDumpUI.Models;

namespace ZoltDumpUI.Views;

/// <summary>
/// Dialog for setting path match criteria for CMD_FIND_INSTANCE_BY_PATH.
/// Returns a <see cref="PathMatchCriteria"/> when Generate is clicked, or null on Cancel.
/// AOT-safe: no reflection, no data binding — direct control access.
/// </summary>
public partial class PathMatchDialog : Window
{
    /// <summary>The result criteria, or null if cancelled.</summary>
    public PathMatchCriteria? Result { get; private set; }

    public PathMatchDialog()
    {
        InitializeComponent();
    }

    /// <summary>
    /// Create a dialog pre-populated with context from the current object.
    /// </summary>
    /// <param name="className">Current object's class name (e.g. "Button")</param>
    /// <param name="memberName">Member being resolved (e.g. "current_number")</param>
    /// <param name="nodeName">Current node name (e.g. "@InventoryElement@691")</param>
    /// <param name="objectMembers">List of Object-typed member names on the current instance (for path hints)</param>
    public PathMatchDialog(string className, string memberName, string nodeName,
        List<string> objectMembers) : this()
    {
        TitleLabel.Text = $"Find instance to resolve '{memberName}' — node name '{nodeName}' changes on restart";

        ClassNameBox.Text = className;

        // Auto-suggest: if there are Object-typed members, suggest first one as path prefix
        if (objectMembers.Count > 0)
        {
            MatchPathBox.Text = $"{objectMembers[0]}.";
            MatchPathBox.Watermark = $"e.g. {objectMembers[0]}.my_id";
        }

        GenerateButton.Click += OnGenerate;
        CancelButton.Click += OnCancel;
    }

    private void OnGenerate(object? sender, RoutedEventArgs e)
    {
        string matchPath = MatchPathBox.Text?.Trim() ?? "";
        string matchValue = MatchValueBox.Text?.Trim() ?? "";

        if (string.IsNullOrEmpty(matchPath) || string.IsNullOrEmpty(matchValue))
        {
            // Show inline error — don't close
            TitleLabel.Text = "Both match path and value are required";
            TitleLabel.Foreground = Avalonia.Media.Brushes.OrangeRed;
            return;
        }

        Result = new PathMatchCriteria
        {
            ClassName = ClassNameBox.Text?.Trim() ?? "",
            MatchPath = matchPath,
            MatchValue = matchValue,
        };

        Close(true);
    }

    private void OnCancel(object? sender, RoutedEventArgs e)
    {
        Result = null;
        Close(false);
    }
}
