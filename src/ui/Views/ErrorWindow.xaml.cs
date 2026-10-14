using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using TailDrop.UI.Core;
using Windows.System;

namespace TailDrop.UI.Views;

/// <summary>Tailscale isn't running or isn't signed in: say so and offer to open it.</summary>
public sealed partial class ErrorWindow : Window
{
    private const double WidthDips = 440;
    private bool _presented, _closing;

    public ErrorWindow()
    {
        InitializeComponent();
        Popups.MakeDialog(this, AppTitleBar);
        RetryButton.Content = Str.Get("Retry");
        DismissButton.Content = Str.Get("Close");
        CheckingText.Text = Str.Get("Checking");
        Store.StateChanged += Refresh;
        Closed += (_, _) =>
        {
            _closing = true;
            Store.StateChanged -= Refresh;
        };
        Refresh();
    }

    private void Refresh()
    {
        if (_closing) return;
        if (Store.Connected)
        {
            // Connected now: nothing left to say.
            DispatcherQueue.TryEnqueue(Close);
            return;
        }
        Heading.Text = Str.Get(Store.Conn is "needsLogin" or "needsMachineAuth" ? "Error_SignInTitle" : "Error_Title");
        Message.Text = Str.Get("Conn_" + Store.Conn);
        Checking.Visibility = Store.Conn == "starting" ? Visibility.Visible : Visibility.Collapsed;
        OpenButton.Content = Str.Get(Store.Conn == "noService" ? "GetTailscale" : "OpenTailscale");
        if (_presented) DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low,
            () => Popups.FitDialog(this, Root, WidthDips));
    }

    public void Present()
    {
        if (_presented)
        {
            Activate();
            Native.SetForegroundWindow(Popups.Hwnd(this));
            return;
        }
        _presented = true;
        Popups.PresentDialog(this, Root, WidthDips, () => OpenButton.Focus(FocusState.Programmatic));
    }

    private void OnOpenTailscale(object sender, RoutedEventArgs e) => HostLink.OpenTailscale();
    private void OnRetry(object sender, RoutedEventArgs e) => HostLink.Refresh();
    private void OnClose(object sender, RoutedEventArgs e) => Close();

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Escape)
        {
            Close();
            e.Handled = true;
        }
    }
}
