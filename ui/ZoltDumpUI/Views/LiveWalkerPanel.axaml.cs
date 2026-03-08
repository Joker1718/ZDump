using Avalonia.Controls;
using Avalonia.Input;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class LiveWalkerPanel : UserControl
{
    public LiveWalkerPanel()
    {
        InitializeComponent();
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
}
