using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using TailDrop.UI.Core;

namespace TailDrop.UI.Views;

/// <summary>"Sending to ipad": progress, then sent or what went wrong.</summary>
public sealed partial class SendToast : UserControl, IToastContent
{
    private readonly int _job;
    private bool _closed, _thumbRequested;
    private string _lastState = "";

    public FrameworkElement Root => this;
    public Window? HostWindow { get; set; }
    public event Action? LayoutChanged;
    public event Action? CloseRequested;

    public SendToast(int job)
    {
        _job = job;
        InitializeComponent();
        ToolTipService.SetToolTip(CloseButton, Str.Get("Close"));
        Store.JobsChanged += Update;
        Update();
    }

    public int AutoDismissMs => Store.Job(_job)?.State == "done" ? 4500 : 0;

    public void OnClosed()
    {
        if (_closed) return;
        _closed = true;
        Store.JobsChanged -= Update;
        HostLink.DismissJob(_job);
    }

    private void Update()
    {
        if (_closed) return;
        var j = Store.Job(_job);
        if (j == null) return;
        if (j.State == "cancelled")
        {
            CloseRequested?.Invoke();
            return;
        }
        DeviceGlyph.Glyph = Format.KindGlyph(j.Kind);
        BadgeGlyph.Glyph = j.State == "done" ? "" : j.State == "failed" ? "" : "";
        if (!_thumbRequested && j.First.Length > 0)
        {
            _thumbRequested = true;
            _ = LoadThumbAsync(j.First);
        }
        string what = j.FileCount > 1 ? Str.F("NItems", j.FileCount) : j.Current;
        int pct = j.Total > 0 ? (int)(j.Sent * 100 / j.Total) : 0;
        Subtitle.Foreground = (Brush)Application.Current.Resources[j.State == "failed" ? "SystemFillColorCriticalBrush" : "TextFillColorSecondaryBrush"];
        switch (j.State)
        {
            case "preparing":
                Title.Text = Str.F("Job_preparing", j.DeviceName);
                Subtitle.Text = j.Current;
                break;
            case "sending":
                Title.Text = Str.F("Job_sending", j.DeviceName);
                Subtitle.Text = what + " · " + pct + "%" + (j.Rate > 0 ? " · " + Format.Rate(j.Rate) : "");
                break;
            case "done":
                Title.Text = Str.F("Job_done", j.DeviceName);
                Subtitle.Text = what + " · " + Format.Size(j.Total);
                break;
            default:
                Title.Text = Str.F("Job_failed", j.DeviceName);
                Subtitle.Text = Str.F("Err_" + (j.Error.Length > 0 ? j.Error : "refused"), j.DeviceName, j.ErrorDetail);
                break;
        }
        bool running = j.IsRunning;
        Progress.Visibility = running ? Visibility.Visible : Visibility.Collapsed;
        Progress.IsIndeterminate = j.State == "preparing";
        Progress.Value = j.Total > 0 ? (double)j.Sent / j.Total : 0;

        if (running)
        {
            Buttons.Visibility = Visibility.Visible;
            PrimaryButton.Style = (Style)Application.Current.Resources["DefaultButtonStyle"];
            PrimaryButton.Content = Str.Get("Cancel");
            SecondaryButton.Visibility = Visibility.Collapsed;
            Grid.SetColumnSpan(PrimaryButton, 2);
        }
        else if (j.State == "failed")
        {
            Buttons.Visibility = Visibility.Visible;
            PrimaryButton.Style = (Style)Application.Current.Resources["AccentButtonStyle"];
            PrimaryButton.Content = Str.Get("TryAgain");
            SecondaryButton.Visibility = Visibility.Visible;
            SecondaryButton.Content = Str.Get("Dismiss");
            Grid.SetColumnSpan(PrimaryButton, 1);
        }
        else
        {
            Buttons.Visibility = Visibility.Collapsed;
        }
        if (j.State != _lastState)
        {
            _lastState = j.State;
            LayoutChanged?.Invoke();
        }
    }

    private async Task LoadThumbAsync(string path)
    {
        var t = await Thumbs.GetAsync(path, 96);
        if (t == null) return;
        Thumb.Source = t.Source;
        Thumb.Stretch = t.IsPicture ? Stretch.UniformToFill : Stretch.Uniform;
        Thumb.Margin = t.IsPicture ? new Thickness(0) : new Thickness(8);
        DeviceGlyph.Visibility = Visibility.Collapsed;
    }

    private void OnClose(object sender, RoutedEventArgs e) => CloseRequested?.Invoke();

    private void OnPrimary(object sender, RoutedEventArgs e)
    {
        var j = Store.Job(_job);
        if (j == null) return;
        if (j.IsRunning)
        {
            HostLink.Cancel(_job);
        }
        else if (j.State == "failed")
        {
            HostLink.Retry(_job);
            CloseRequested?.Invoke();
        }
    }

    private void OnSecondary(object sender, RoutedEventArgs e) => CloseRequested?.Invoke();
}
