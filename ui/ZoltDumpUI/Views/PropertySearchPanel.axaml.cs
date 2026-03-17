using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using ZoltDumpUI.Models;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class PropertySearchPanel : UserControl
{
    /// <summary>Tracks the hit under the pointer when context menu opens.</summary>
    private PropertySearchHit? _contextHit;

    public PropertySearchPanel()
    {
        InitializeComponent();
        BuildContextMenu();
        WireDoubleClick();
    }

    /// <summary>Enter key in search TextBox triggers Search.</summary>
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.Enter && SearchBox.IsFocused && DataContext is PropertySearchViewModel vm)
        {
            if (vm.SearchCommand.CanExecute(null))
                vm.SearchCommand.Execute(null);
            e.Handled = true;
        }
    }

    /// <summary>Double-click a row to open its class in Live Walker.</summary>
    private void WireDoubleClick()
    {
        ResultsGrid.DoubleTapped += async (_, e) =>
        {
            if (DataContext is not PropertySearchViewModel vm) return;
            if (ResultsGrid.SelectedItem is not PropertySearchHit hit) return;
            await vm.OpenInLiveWalkerCommand.ExecuteAsync(hit);
            e.Handled = true;
        };
    }

    /// <summary>
    /// Build ContextMenu in code-behind for AOT compatibility.
    /// </summary>
    private void BuildContextMenu()
    {
        var menu = new ContextMenu();

        PropertySearchViewModel? GetVm() => DataContext as PropertySearchViewModel;

        // Capture the right-clicked row
        ResultsGrid.AddHandler(Control.ContextRequestedEvent, (object? sender, ContextRequestedEventArgs e) =>
        {
            if (e.Source is Visual visual)
            {
                var row = visual.FindAncestorOfType<DataGridRow>();
                if (row?.DataContext is PropertySearchHit hit)
                {
                    _contextHit = hit;
                    ResultsGrid.SelectedItem = hit;
                    return;
                }
            }
            _contextHit = ResultsGrid.SelectedItem as PropertySearchHit;
        }, RoutingStrategies.Tunnel);

        // ── Open in Live Walker ──
        var openWalker = new MenuItem { Header = "Open in Live Walker" };
        openWalker.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextHit is { } hit)
                await vm.OpenInLiveWalkerCommand.ExecuteAsync(hit);
        };
        menu.Items.Add(openWalker);

        // ── Find Instances ──
        var findInstances = new MenuItem { Header = "Find Instances" };
        findInstances.Click += (_, _) =>
        {
            if (GetVm() is { } vm && _contextHit is { } hit)
            {
                if (vm.FindInstancesCommand.CanExecute(hit))
                    vm.FindInstancesCommand.Execute(hit);
            }
        };
        menu.Items.Add(findInstances);

        menu.Items.Add(new Separator());

        // ── Copy Offset ──
        var copyOffset = new MenuItem { Header = "Copy Offset" };
        copyOffset.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextHit is { } hit)
                await vm.CopyOffsetCommand.ExecuteAsync(hit);
        };
        menu.Items.Add(copyOffset);

        ResultsGrid.ContextMenu = menu;
    }
}
