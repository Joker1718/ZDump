using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using ZoltDumpUI.Core;
using ZoltDumpUI.Services;
using ZoltDumpUI.ViewModels;
using ZoltDumpUI.Views;

namespace ZoltDumpUI;

public partial class App : Application
{
    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            // Platform services
            var platform = new WindowsPlatformService();
            if (!platform.TryAcquireSingleInstance())
            {
                desktop.Shutdown();
                return;
            }

            // Logging
            var logDir = platform.GetLogDirectoryPath();
            var log = new LoggingService(logDir);
            log.Info("Zolt-Dump starting");

            // Pipe + Dump
            var pipe = new PipeClient();
            var dump = new DumpService(pipe);

            // Optional services
            var proxyDeploy = new ProxyDeployService();
            var aobMaker = new AobMakerBridgeService();

            // Main ViewModel
            var mainVm = new MainWindowViewModel(pipe, dump, log, platform, proxyDeploy, aobMaker);

            desktop.MainWindow = new MainWindow
            {
                DataContext = mainVm
            };

            desktop.ShutdownRequested += (_, _) =>
            {
                pipe.Dispose();
                (log as IDisposable)?.Dispose();
                platform.ReleaseSingleInstance();
            };
        }

        base.OnFrameworkInitializationCompleted();
    }
}
