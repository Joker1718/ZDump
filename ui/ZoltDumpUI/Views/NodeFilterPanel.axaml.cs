using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using ZoltDumpUI.Models;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class NodeFilterPanel : UserControl
{
    /// <summary>Tracks the class under the pointer when context menu opens.</summary>
    private ClassListItem? _contextClass;

    public NodeFilterPanel()
    {
        InitializeComponent();
        BuildContextMenu();
        WireSelectionChanged();
    }

    /// <summary>When a class row is selected, load its property detail.</summary>
    private void WireSelectionChanged()
    {
        ClassGrid.SelectionChanged += async (_, _) =>
        {
            if (DataContext is not NodeFilterViewModel vm) return;
            if (ClassGrid.SelectedItem is not ClassListItem item) return;
            await vm.LoadClassDetailAsync(item.ClassName);
        };
    }

    /// <summary>
    /// Build ContextMenu in code-behind for AOT compatibility.
    /// </summary>
    private void BuildContextMenu()
    {
        var menu = new ContextMenu();

        NodeFilterViewModel? GetVm() => DataContext as NodeFilterViewModel;

        // Capture the right-clicked row
        ClassGrid.AddHandler(Control.ContextRequestedEvent, (object? sender, ContextRequestedEventArgs e) =>
        {
            if (e.Source is Visual visual)
            {
                var row = visual.FindAncestorOfType<DataGridRow>();
                if (row?.DataContext is ClassListItem item)
                {
                    _contextClass = item;
                    ClassGrid.SelectedItem = item;
                    return;
                }
            }
            _contextClass = ClassGrid.SelectedItem as ClassListItem;
        }, RoutingStrategies.Tunnel);

        // ── Find Instances ──
        var findInstances = new MenuItem { Header = "Find Instances" };
        findInstances.Click += (_, _) =>
        {
            if (GetVm() is { } vm && _contextClass is { } item)
            {
                if (vm.FindInstancesCommand.CanExecute(item))
                    vm.FindInstancesCommand.Execute(item);
            }
        };
        menu.Items.Add(findInstances);

        // ── Open in Live Walker ──
        var openWalker = new MenuItem { Header = "Open in Live Walker" };
        openWalker.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextClass is { } item)
                await vm.OpenInLiveWalkerCommand.ExecuteAsync(item);
        };
        menu.Items.Add(openWalker);

        ClassGrid.ContextMenu = menu;
    }
}
