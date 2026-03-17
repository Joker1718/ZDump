using Avalonia.Controls;
using Avalonia.Input;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class InstanceFinderPanel : UserControl
{
    public InstanceFinderPanel()
    {
        InitializeComponent();
        InstancesGrid.DoubleTapped += OnInstancesGridDoubleTapped;
    }

    private void OnInstancesGridDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (DataContext is not InstanceFinderViewModel vm) return;
        if (vm.SelectedInstance is not { } entry) return;
        if (vm.OpenInLiveWalkerCommand.CanExecute(entry))
            vm.OpenInLiveWalkerCommand.Execute(entry);
    }

    /// <summary>Enter key triggers search based on focused TextBox.</summary>
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key != Key.Enter || DataContext is not InstanceFinderViewModel vm) return;

        if (ClassNameBox.IsFocused)
        {
            if (vm.FindInstancesCommand.CanExecute(null))
                vm.FindInstancesCommand.Execute(null);
            e.Handled = true;
        }
        else if (AddressBox.IsFocused)
        {
            if (vm.FindByAddressCommand.CanExecute(null))
                vm.FindByAddressCommand.Execute(null);
            e.Handled = true;
        }
    }
}
