using Avalonia.Controls;
using Avalonia.Input;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class PropertySearchPanel : UserControl
{
    public PropertySearchPanel()
    {
        InitializeComponent();
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
}
