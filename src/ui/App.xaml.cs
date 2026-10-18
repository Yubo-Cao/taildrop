using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.Windows.Storage.Pickers;
using TailDrop.UI.Core;
using TailDrop.UI.Views;

namespace TailDrop.UI;

/// <summary>
/// TailDropUI.exe: every window TailDrop shows. TailDrop.exe starts it with
/// --pipe when something needs to be on screen (or --warm when the pointer
/// reaches the tray icon); it exits after a minute with nothing open.
/// </summary>
public partial class App : Application
{
    public static App Instance => (App)Current;

    private DispatcherQueue? _ui;
    private DispatcherQueueTimer? _idle;
    private int _idleSeconds;
    private int _busy;  // open file/folder dialogs
    private FlyoutWindow? _flyout;
    private SettingsWindow? _settings;
    private PickerWindow? _picker;
    private ErrorWindow? _error;
    private ToastManager? _toasts;

    public App()
    {
        InitializeComponent();
        // Windows come and go; the process lives until it decides to exit.
        DispatcherShutdownMode = DispatcherShutdownMode.OnExplicitShutdown;
        if (Environment.GetEnvironmentVariable("TAILDROP_LOG") != null)
            AppDomain.CurrentDomain.FirstChanceException += (_, e) =>
                Log.Write("first chance: " + e.Exception.Message + "\n" + new System.Diagnostics.StackTrace(1, false));
        UnhandledException += (_, e) =>
        {
            Log.Write("unhandled: " + e.Exception);
            e.Handled = true;
        };
    }

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        _ui = DispatcherQueue.GetForCurrentThread();
        string[] argv = Environment.GetCommandLineArgs();
        int at = Array.IndexOf(argv, "--pipe");
        if (at < 0 || at + 1 >= argv.Length)
        {
            Exit();
            return;
        }
        _toasts = new ToastManager();
        Store.ShowRequested += OnShow;
        Store.NotifyRequested += OnNotify;
        Store.StateChanged += () =>
        {
            if (Store.Connected && _error != null) _error.Close();
        };
        Store.Resumed += () =>
        {
            // After sleep or hibernation, a UI with nothing on screen makes way for a
            // fresh one; the next thing to show starts it again.
            if (!Anything()) Quit();
        };
        HostLink.Disconnected += Quit;
        if (!await HostLink.ConnectAsync(argv[at + 1], _ui))
        {
            Quit();
            return;
        }
        _idle = _ui.CreateTimer();
        _idle.Interval = TimeSpan.FromSeconds(5);
        _idle.Tick += (_, _) => CheckIdle();
        _idle.Start();
    }

    private bool Anything() =>
        _busy > 0 || (_flyout?.IsShown ?? false) || _settings != null || _picker != null || _error != null ||
        (_toasts?.Count ?? 0) > 0;

    private void CheckIdle()
    {
        _idleSeconds = Anything() ? 0 : _idleSeconds + 5;
        if (_idleSeconds >= 60) Quit();
    }

    private bool _quitting;

    /// <summary>
    /// Exits for sure. The pipe goes first, so the host knows right away and starts a
    /// new UI for whatever comes next; WinUI's own shutdown gets two seconds.
    /// </summary>
    public void Quit()
    {
        if (_quitting) return;
        _quitting = true;
        _idle?.Stop();
        HostLink.Close();
        _ = Task.Delay(2000).ContinueWith(_ => Native.ExitNow(), TaskScheduler.Default);
        try
        {
            Exit();
        }
        catch
        {
            Native.ExitNow();
        }
    }

    private void OnShow(ShowMsg msg)
    {
        _idleSeconds = 0;
        switch (msg.What)
        {
            case "flyout":
                Flyout().Toggle(msg.Tray, msg.DropMode);
                break;
            case "settings":
                OpenSettings(null);
                break;
            case "device":
                OpenSettings(msg.Device);
                break;
            case "picker":
                if (msg.Paths is { Count: > 0 }) OpenPicker(msg.Paths);
                break;
            case "pickFiles":
                _ = PickFilesAsync(string.IsNullOrEmpty(msg.Device) ? null : msg.Device);
                break;
            case "error":
                OpenError();
                break;
        }
    }

    private void OnNotify(NotifyMsg msg)
    {
        _idleSeconds = 0;
        if (msg.Batch > 0) _toasts!.ShowBatch(msg.Batch, msg.Tray);
        else if (msg.Job > 0) _toasts!.ShowJob(msg.Job, msg.Tray);
    }

    private FlyoutWindow Flyout()
    {
        if (_flyout == null)
        {
            _flyout = new FlyoutWindow();
            // Notifications step aside while the flyout is open.
            _flyout.ShownChanged += () => _toasts?.SetSuppressed(_flyout.IsShown);
        }
        return _flyout;
    }

    public SettingsWindow? SettingsHost => _settings;

    public void OpenSettings(string? deviceId)
    {
        if (_settings == null)
        {
            _settings = new SettingsWindow();
            _settings.Closed += (_, _) => _settings = null;
        }
        _settings.ShowPage(deviceId);
    }

    public void OpenPicker(IReadOnlyList<string> paths)
    {
        if (!Store.Connected)
        {
            OpenError();
            return;
        }
        _picker?.Close();
        _picker = new PickerWindow(paths);
        _picker.Closed += (_, _) => _picker = null;
        _picker.Present();
    }

    public void OpenError()
    {
        if (Store.Connected) return;
        if (_error == null)
        {
            _error = new ErrorWindow();
            _error.Closed += (_, _) => _error = null;
        }
        _error.Present();
    }

    /// <summary>Lets the user choose files, then sends them to `deviceId` or opens the picker.</summary>
    public async Task PickFilesAsync(string? deviceId, Window? owner = null)
    {
        var paths = await ChooseFilesAsync(owner ?? Flyout());
        if (paths.Count == 0) return;
        if (deviceId != null) HostLink.SendFiles(deviceId, paths);
        else OpenPicker(paths);
    }

    /// <summary>The system file picker; empty when cancelled.</summary>
    public async Task<List<string>> ChooseFilesAsync(Window owner)
    {
        _busy++;
        try
        {
            var picker = new FileOpenPicker(owner.AppWindow.Id) { ViewMode = PickerViewMode.List };
            picker.FileTypeFilter.Add("*");
            var picked = await picker.PickMultipleFilesAsync();
            return picked?.Select(p => p.Path).Where(p => !string.IsNullOrEmpty(p)).ToList() ?? new List<string>();
        }
        catch
        {
            return new List<string>();
        }
        finally
        {
            _busy--;
        }
    }

    public static async Task<string?> PickFolderAsync(Window owner)
    {
        Instance._busy++;
        try
        {
            var picker = new FolderPicker(owner.AppWindow.Id);
            var result = await picker.PickSingleFolderAsync();
            return string.IsNullOrEmpty(result?.Path) ? null : result.Path;
        }
        catch
        {
            return null;
        }
        finally
        {
            Instance._busy--;
        }
    }
}
