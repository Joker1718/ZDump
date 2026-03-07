using CommunityToolkit.Mvvm.ComponentModel;

namespace ZoltDumpUI.ViewModels;

/// <summary>Base ViewModel providing common error handling.</summary>
public partial class ViewModelBase : ObservableObject
{
    [ObservableProperty]
    private string _errorMessage = "";

    protected void SetError(string message) => ErrorMessage = message;
    protected void ClearError() => ErrorMessage = "";
}
