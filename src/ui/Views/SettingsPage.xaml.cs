using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using TailDrop.UI.Core;

namespace TailDrop.UI.Views;

/// <summary>One section of Settings: general, receiving, sending, devices or about.</summary>
public sealed partial class SettingsPage : Page
{
    private readonly ObservableCollection<DeviceVm> _devices = new();
    private bool _updating;

    public string Section { get; private set; } = "general";

    public SettingsPage()
    {
        InitializeComponent();
        DeviceList.ItemsSource = _devices;

        TailscaleTitle.Text = "Tailscale";
        RetryButton.Content = Str.Get("Retry");
        OpenTailscaleButton.Content = Str.Get("OpenTailscale");
        StartupCard.Header = Str.Get("Startup_Header");
        StartupCard.Description = Str.Get("Startup_Description");
        SummaryFolderCard.Header = Str.Get("Folder_Header");
        FolderCard.Header = Str.Get("Folder_Header");
        ResetFolderButton.Content = Str.Get("Folder_Reset");
        ChangeFolderButton.Content = Str.Get("Folder_Change");
        AutoSaveCard.Header = Str.Get("AutoSave_Header");
        AutoSaveCard.Description = Str.Get("AutoSave_Description");
        InboxCard.Header = Str.Get("Inbox_Header");
        OpenInboxButton.Content = Str.Get("Inbox_Open");
        TrayDropCard.Header = Str.Get("TrayDrop_Header");
        TrayDropCard.Description = Str.Get("TrayDrop_Description");
        ExplorerCard.Header = Str.Get("Explorer_Header");
        DevicesHint.Text = Str.Get("Devices_Hint");
        NoDevices.Text = Str.Get("NoDevices");
        GitHubLink.Content = Str.Get("About_GitHub");
        LicenseCard.Header = Str.Get("About_LicenseHeader");
        LicenseCard.Description = Str.Get("About_License");
        LicenseLink.Content = Str.Get("About_LicenseLink");
        IssuesCard.Header = Str.Get("About_IssuesHeader");
        IssuesCard.Description = Str.Get("About_IssuesDescription");
        IssuesLink.Content = Str.Get("About_IssuesLink");
        Disclaimer.Text = Str.Get("About_Disclaimer");

        Loaded += (_, _) =>
        {
            Store.StateChanged += Refresh;
            Store.SettingsChanged += Refresh;
            Store.DevicesChanged += Refresh;
            Refresh();
        };
        Unloaded += (_, _) =>
        {
            Store.StateChanged -= Refresh;
            Store.SettingsChanged -= Refresh;
            Store.DevicesChanged -= Refresh;
        };
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        Section = e.Parameter as string ?? "general";
        GeneralPanel.Visibility = Show("general");
        ReceivingPanel.Visibility = Show("receiving");
        SendingPanel.Visibility = Show("sending");
        DevicesPanel.Visibility = Show("devices");
        AboutPanel.Visibility = Show("about");
    }

    private Visibility Show(string section) => Section == section ? Visibility.Visible : Visibility.Collapsed;

    private void Refresh()
    {
        _updating = true;
        bool ok = Store.Connected;
        StatusDot.Fill = (Brush)Application.Current.Resources[ok ? "SystemFillColorSuccessBrush" : "SystemFillColorCautionBrush"];
        StatusText.Text = ok
            ? (Store.Tailnet.Length > 0 ? Str.F("ConnectedAs", Store.Self, Store.Tailnet) : Str.Get("Connected"))
            : Str.Get("Conn_" + Store.Conn);
        RetryButton.Visibility = ok ? Visibility.Collapsed : Visibility.Visible;
        OpenTailscaleButton.Style = (Style)Application.Current.Resources[ok ? "DefaultButtonStyle" : "AccentButtonStyle"];

        var s = Store.Settings;
        StartupToggle.IsOn = s.RunAtLogin;
        StartupState.Text = Str.Get(s.RunAtLogin ? "On" : "Off");
        SummaryFolderCard.Description = s.EffectiveDir;
        SummaryFolderLink.Content = Str.Get("Folder_Change");

        FolderCard.Description = s.EffectiveDir;
        ResetFolderButton.Visibility = s.DownloadDir.Length > 0 &&
            !string.Equals(s.DownloadDir, s.Downloads, StringComparison.OrdinalIgnoreCase) ? Visibility.Visible : Visibility.Collapsed;
        AutoSaveToggle.IsOn = s.AutoSave;
        AutoSaveState.Text = Str.Get(s.AutoSave ? "On" : "Off");
        InboxCard.Description = Str.F("Inbox_Description", s.Inbox);

        TrayDropToggle.IsOn = s.TrayDrop;
        TrayDropState.Text = Str.Get(s.TrayDrop ? "On" : "Off");
        ExplorerCard.Description = Str.Get(s.Win11Menu ? "Explorer_Win11" : "Explorer_Classic");
        ExplorerBadgeText.Text = Str.Get(s.Win11Menu ? "Explorer_BadgeWin11" : "Explorer_BadgeClassic");
        ExplorerBadge.Background = (Brush)Application.Current.Resources[
            s.Win11Menu ? "SystemFillColorSuccessBackgroundBrush" : "SystemFillColorNeutralBackgroundBrush"];

        var models = Store.Devices;
        Collections.Sync(_devices, models, d => d.Id, v => v.Id, DeviceVm.From, (v, d) => v.Update(d));
        NoDevices.Visibility = models.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        VersionText.Text = Str.F("About_Version", s.Version);
        _updating = false;
    }

    private void OnRetry(object sender, RoutedEventArgs e) => HostLink.Refresh();
    private void OnOpenTailscale(object sender, RoutedEventArgs e) => HostLink.OpenTailscale();
    private void OnOpenInbox(object sender, RoutedEventArgs e) => HostLink.OpenInbox();
    private void OnResetFolder(object sender, RoutedEventArgs e) => HostLink.SetSetting("downloadDir", "");
    private void OnGoReceiving(object sender, RoutedEventArgs e) => Frame.Navigate(typeof(SettingsPage), "receiving",
        new Microsoft.UI.Xaml.Media.Animation.EntranceNavigationTransitionInfo());

    private async void OnChangeFolder(object sender, RoutedEventArgs e)
    {
        var owner = App.Instance.SettingsHost;
        if (owner == null) return;
        string? dir = await App.PickFolderAsync(owner);
        if (dir != null) HostLink.SetSetting("downloadDir", dir);
    }

    private void OnAutoSaveToggled(object sender, RoutedEventArgs e)
    {
        AutoSaveState.Text = Str.Get(AutoSaveToggle.IsOn ? "On" : "Off");
        if (!_updating) HostLink.SetSetting("autoSave", AutoSaveToggle.IsOn);
    }

    private void OnTrayDropToggled(object sender, RoutedEventArgs e)
    {
        TrayDropState.Text = Str.Get(TrayDropToggle.IsOn ? "On" : "Off");
        if (!_updating) HostLink.SetSetting("trayDrop", TrayDropToggle.IsOn);
    }

    private void OnStartupToggled(object sender, RoutedEventArgs e)
    {
        StartupState.Text = Str.Get(StartupToggle.IsOn ? "On" : "Off");
        if (!_updating) HostLink.SetSetting("runAtLogin", StartupToggle.IsOn);
    }

    private void OnDeviceClick(object sender, RoutedEventArgs e)
    {
        if (Bound.Item<DeviceVm>(sender) is { } vm) App.Instance.SettingsHost?.OpenDevice(vm.Id);
    }

    private void OnDeviceRightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        if (Bound.Item<DeviceVm>(sender) is not { } vm) return;
        DeviceMenu.Create(vm.Id).ShowAt((FrameworkElement)sender, e.GetPosition((FrameworkElement)sender));
        e.Handled = true;
    }
}
