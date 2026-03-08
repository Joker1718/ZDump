using Avalonia.Controls;
using Avalonia.Input;
using ZoltDumpUI.ViewModels;

namespace ZoltDumpUI.Views;

public partial class NodeTreePanel : UserControl
{
    public NodeTreePanel()
    {
        InitializeComponent();
    }

    /// <summary>Enter key in search triggers Load.</summary>
    protected override void OnKeyDown(KeyEventArgs e)
    {
        base.OnKeyDown(e);
        if (e.Key == Key.Enter && DataContext is NodeTreeViewModel vm)
        {
            if (vm.LoadAllCommand.CanExecute(null))
                vm.LoadAllCommand.Execute(null);
            e.Handled = true;
        }
    }
}
