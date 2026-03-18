using System;
using System.ComponentModel;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Threading;
using Avalonia.VisualTree;
using ZoltDumpUI.Models;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class LiveWalkerPanel : UserControl
{
    /// <summary>Tracks the field under the pointer when context menu opens.</summary>
    private LiveFieldValue? _contextField;

    public LiveWalkerPanel()
    {
        InitializeComponent();
        BuildFieldsContextMenu();
        WireDoubleClick();
        WireScrollTarget();
    }

    /// <summary>
    /// When the ViewModel sets PendingScrollTarget, scroll the DataGrid to
    /// the matching field row and select it. This restores scroll position
    /// when pressing Back from a container drill-down.
    /// </summary>
    /// <summary>Previously observed ViewModel (for unsubscription).</summary>
    private LiveWalkerViewModel? _observedVm;

    private void WireScrollTarget()
    {
        // Subscribe to DataContext changes to watch the ViewModel's property
        DataContextChanged += (_, _) =>
        {
            if (_observedVm is not null)
            {
                _observedVm.PropertyChanged -= OnViewModelPropertyChanged;
                _observedVm = null;
            }
            if (DataContext is LiveWalkerViewModel vm)
            {
                vm.PropertyChanged += OnViewModelPropertyChanged;
                _observedVm = vm;
            }
        };
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(LiveWalkerViewModel.PendingScrollTarget)) return;
        if (sender is not LiveWalkerViewModel vm) return;
        string? target = vm.PendingScrollTarget;
        if (string.IsNullOrEmpty(target)) return;

        // Clear immediately so we don't process it twice
        vm.PendingScrollTarget = null;

        // Dispatch after layout so the DataGrid has the new items
        Dispatcher.UIThread.Post(() =>
        {
            var match = vm.Fields.FirstOrDefault(f => f.Name == target);
            if (match is not null)
            {
                FieldsGrid.SelectedItem = match;
                FieldsGrid.ScrollIntoView(match, null);
            }
        }, DispatcherPriority.Background);
    }

    /// <summary>Enter key in address TextBox triggers Go navigation.</summary>
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.Enter && AddressBox.IsFocused && DataContext is LiveWalkerViewModel vm)
        {
            if (vm.GoToAddressCommand.CanExecute(null))
                vm.GoToAddressCommand.Execute(null);
            e.Handled = true;
        }
    }

    /// <summary>
    /// Double-click on a row: drills into pointer or container fields.
    /// </summary>
    private void WireDoubleClick()
    {
        FieldsGrid.DoubleTapped += async (_, e) =>
        {
            if (DataContext is not LiveWalkerViewModel vm) return;
            if (FieldsGrid.SelectedItem is not LiveFieldValue field) return;

            // Container fields: drill into elements
            if (field.IsContainer && !string.IsNullOrEmpty(field.Address))
            {
                await vm.NavigateContainerAsync(field);
                e.Handled = true;
                return;
            }

            // Pointer fields: navigate to pointed object
            if (field.IsPointer && !string.IsNullOrEmpty(field.PtrValue))
            {
                if (vm.NavigatePointerCommand.CanExecute(field))
                    await vm.NavigatePointerCommand.ExecuteAsync(field);
                e.Handled = true;
            }
        };
    }

    /// <summary>
    /// Build ContextMenu in code-behind for AOT compatibility.
    /// XAML bindings on ContextMenu popups fail with Native AOT because the popup
    /// is disconnected from the visual tree and DataContext is typed as object?.
    ///
    /// Also handles right-click row selection — Avalonia DataGrid does not auto-select
    /// rows on right-click, so we capture the row in ContextRequested.
    /// </summary>
    private void BuildFieldsContextMenu()
    {
        var menu = new ContextMenu();

        // Helper: get ViewModel (strongly typed, no reflection)
        LiveWalkerViewModel? GetVm() => DataContext as LiveWalkerViewModel;

        // Capture the right-clicked row BEFORE the context menu opens.
        // Avalonia DataGrid doesn't select on right-click, so SelectedItem may be stale.
        FieldsGrid.AddHandler(Control.ContextRequestedEvent, (object? sender, ContextRequestedEventArgs e) =>
        {
            // Try to find the DataGridRow under the pointer
            if (e.Source is Visual visual)
            {
                var row = visual.FindAncestorOfType<DataGridRow>();
                if (row?.DataContext is LiveFieldValue field)
                {
                    _contextField = field;
                    FieldsGrid.SelectedItem = field; // Also visually select it
                    return;
                }
            }

            // Fallback to current selection
            _contextField = FieldsGrid.SelectedItem as LiveFieldValue;
        }, RoutingStrategies.Tunnel);

        // ── Drill Into Container ──
        var drillContainer = new MenuItem { Header = "Drill Into Container" };
        drillContainer.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextField is { IsContainer: true } field)
                await vm.NavigateContainerAsync(field);
        };
        menu.Items.Add(drillContainer);

        // ── CE Lua Scripts ──
        var generateCeLua = new MenuItem { Header = "Generate CE Lua Script" };
        generateCeLua.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextField is { } field)
                await vm.GenerateCeScriptAsync(field);
        };
        menu.Items.Add(generateCeLua);

        var generatePathMatch = new MenuItem { Header = "Generate CE Script (Path Match)" };
        generatePathMatch.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextField is { } field)
                await vm.GeneratePathMatchScriptAsync(field);
        };
        menu.Items.Add(generatePathMatch);

        menu.Items.Add(new Separator());

        // ── CE XML / Field ──
        var copyCeXml = new MenuItem { Header = "Copy CE XML" };
        copyCeXml.Click += (_, _) =>
        {
            if (GetVm() is { } vm && vm.CopyCeXmlCommand.CanExecute(null))
                vm.CopyCeXmlCommand.Execute(null);
        };
        menu.Items.Add(copyCeXml);

        var copyCeField = new MenuItem { Header = "Copy CE Field" };
        copyCeField.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextField is { } field)
                await vm.CopyCeFieldAsync(field);
        };
        menu.Items.Add(copyCeField);

        menu.Items.Add(new Separator());

        // ── Export ──
        var exportCsx = new MenuItem { Header = "Export CSX" };
        exportCsx.Click += (_, _) =>
        {
            if (GetVm() is { } vm && vm.ExportCsxCommand.CanExecute(null))
                vm.ExportCsxCommand.Execute(null);
        };
        menu.Items.Add(exportCsx);

        var exportSdk = new MenuItem { Header = "Export SDK" };
        exportSdk.Click += (_, _) =>
        {
            if (GetVm() is { } vm && vm.ExportSdkCommand.CanExecute(null))
                vm.ExportSdkCommand.Execute(null);
        };
        menu.Items.Add(exportSdk);

        menu.Items.Add(new Separator());

        // ── Copy Address / Name ──
        var copyAddr = new MenuItem { Header = "Copy Address" };
        copyAddr.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextField is { } field)
                await vm.CopyFieldAddressAsync(field);
        };
        menu.Items.Add(copyAddr);

        var copyName = new MenuItem { Header = "Copy Name" };
        copyName.Click += async (_, _) =>
        {
            if (GetVm() is { } vm && _contextField is { } field)
                await vm.CopyFieldNameAsync(field);
        };
        menu.Items.Add(copyName);

        FieldsGrid.ContextMenu = menu;
    }
}
