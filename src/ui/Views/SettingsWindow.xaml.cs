using System.Collections.ObjectModel;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Navigation;
using TailDrop.UI.Core;
using Windows.Graphics;

namespace TailDrop.UI.Views;

/// <summary>
/// Settings, laid out like Windows Settings: sections in a NavigationView,
/// a page per device under Devices, title bar drawn by WinUI over Mica.
/// </summary>
public sealed partial class SettingsWindow : Window
{
    private readonly ObservableCollection<string> _crumbs = new();
    private bool _syncingNav;
    // What the frame shows. Frame.Navigated comes before the page's OnNavigatedTo,
    // so these come from the navigation itself rather than from the page.
    private string _section = "", _device = "";

    public SettingsWindow()
    {
        InitializeComponent();
        Title = Str.Get("WindowTitle");
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        AppWindow.TitleBar.PreferredHeightOption = TitleBarHeightOption.Tall;
        string icon = System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico");
        if (System.IO.File.Exists(icon)) AppWindow.SetIcon(icon);

        NavGeneral.Content = Str.Get("Section_General");
        NavReceiving.Content = Str.Get("Section_Receiving");
        NavSending.Content = Str.Get("Section_Sending");
        NavDevices.Content = Str.Get("Section_Devices");
        NavAbout.Content = Str.Get("Section_About");
        Crumbs.ItemsSource = _crumbs;

        if (AppWindow.Presenter is OverlappedPresenter p)
        {
            p.PreferredMinimumWidth = 500;
            p.PreferredMinimumHeight = 480;
        }
        double scale = Popups.Scale(this);
        var size = new SizeInt32((int)(1100 * scale), (int)(760 * scale));
        var work = DisplayArea.Primary.WorkArea;
        size.Width = Math.Min(size.Width, work.Width - 48);
        size.Height = Math.Min(size.Height, work.Height - 48);
        AppWindow.MoveAndResize(new RectInt32(work.X + (work.Width - size.Width) / 2, work.Y + (work.Height - size.Height) / 2,
            size.Width, size.Height));

        Pages.NavigationFailed += (_, e) =>
        {
            Log.Write($"navigation to {e.SourcePageType} failed: {e.Exception}");
            e.Handled = true;
        };
        Store.StateChanged += UpdateAccount;
        Store.DevicesChanged += UpdateCrumbs;
        Closed += (_, _) =>
        {
            Store.StateChanged -= UpdateAccount;
            Store.DevicesChanged -= UpdateCrumbs;
        };
        UpdateAccount();
        Select("general");
    }

    /// <summary>Shows Settings, or a device's page when `deviceId` is set.</summary>
    public void ShowPage(string? deviceId)
    {
        if (deviceId != null && _device != deviceId)
        {
            if (_section != "devices") Select("devices", animate: false);
            OpenDevice(deviceId, AppWindow.IsVisible);
        }
        AppWindow.Show(true);
        Activate();
        Native.SetForegroundWindow(Popups.Hwnd(this));
    }

    public void OpenDevice(string deviceId, bool animate = true) =>
        Pages.Navigate(typeof(DevicePage), deviceId,
            animate ? new DrillInNavigationTransitionInfo() : new SuppressNavigationTransitionInfo());

    public void GoBack()
    {
        if (Pages.CanGoBack) Pages.GoBack();
    }

    private void Select(string section, bool animate = true)
    {
        var item = Nav.MenuItems.OfType<NavigationViewItem>().FirstOrDefault(i => (string)i.Tag == section);
        if (item == null) return;
        _syncingNav = true;
        Nav.SelectedItem = item;
        _syncingNav = false;
        Pages.Navigate(typeof(SettingsPage), section,
            animate ? new EntranceNavigationTransitionInfo() : new SuppressNavigationTransitionInfo());
    }

    private void OnNavSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        if (_syncingNav || args.SelectedItem is not NavigationViewItem { Tag: string section }) return;
        if (_device.Length == 0 && _section == section) return;
        Pages.Navigate(typeof(SettingsPage), section, new EntranceNavigationTransitionInfo());
        if (Nav.DisplayMode != NavigationViewDisplayMode.Expanded) Nav.IsPaneOpen = false;
    }

    private void OnNavigated(object sender, NavigationEventArgs e)
    {
        AppTitleBar.IsBackButtonEnabled = Pages.CanGoBack;
        bool device = e.SourcePageType == typeof(DevicePage);
        _device = device ? e.Parameter as string ?? "" : "";
        _section = device ? "devices" : e.Parameter as string ?? "general";
        // Going back may land on another section: keep the selection in step.
        var item = Nav.MenuItems.OfType<NavigationViewItem>().FirstOrDefault(i => (string)i.Tag == _section);
        if (item != null && !ReferenceEquals(Nav.SelectedItem, item))
        {
            _syncingNav = true;
            Nav.SelectedItem = item;
            _syncingNav = false;
        }
        UpdateCrumbs();
    }

    private void UpdateCrumbs()
    {
        var want = new List<string>();
        if (_section.Length > 0) want.Add(Str.Get("Section_" + char.ToUpperInvariant(_section[0]) + _section[1..]));
        if (_device.Length > 0) want.Add(Store.Device(_device)?.Name ?? _device);
        if (want.SequenceEqual(_crumbs)) return;
        _crumbs.Clear();
        foreach (string c in want) _crumbs.Add(c);
    }

    private void UpdateAccount()
    {
        bool ok = Store.Connected;
        AccountName.Text = ok && Store.Self.Length > 0 ? Store.Self : Str.Get("NotConnectedTitle");
        AccountDetail.Text = ok ? (Store.Tailnet.Length > 0 ? Store.Tailnet : Str.Get("Connected")) : Str.Get("Conn_short_" + Store.Conn);
        AccountDot.Fill = (Brush)Application.Current.Resources[ok ? "SystemFillColorSuccessBrush" : "SystemFillColorCautionBrush"];
    }

    private void OnCrumbClicked(BreadcrumbBar sender, BreadcrumbBarItemClickedEventArgs args)
    {
        if (args.Index == 0 && _device.Length > 0) GoBack();
    }

    // Not from inside the title bar's own click handling: navigating there rebuilds
    // the page under the title bar's input regions mid-click.
    private void OnBackRequested(TitleBar sender, object args) => DispatcherQueue.TryEnqueue(GoBack);

    private void OnBackAccelerator(Microsoft.UI.Xaml.Input.KeyboardAccelerator sender,
        Microsoft.UI.Xaml.Input.KeyboardAcceleratorInvokedEventArgs args)
    {
        GoBack();
        args.Handled = true;
    }

    // The mouse's back button, as in Windows Settings.
    private void OnRootPointerPressed(object sender, Microsoft.UI.Xaml.Input.PointerRoutedEventArgs e)
    {
        if (!e.GetCurrentPoint(Root).Properties.IsXButton1Pressed) return;
        GoBack();
        e.Handled = true;
    }

    private void OnPaneToggleRequested(TitleBar sender, object args) => Nav.IsPaneOpen = !Nav.IsPaneOpen;

    private void OnDisplayModeChanged(NavigationView sender, NavigationViewDisplayModeChangedEventArgs args)
    {
        AppTitleBar.IsPaneToggleButtonVisible = args.DisplayMode != NavigationViewDisplayMode.Expanded;
        UpdateAccountVisibility();
    }

    private void OnPaneOpening(NavigationView sender, object args) => UpdateAccountVisibility(open: true);

    private void OnPaneClosing(NavigationView sender, NavigationViewPaneClosingEventArgs args) => UpdateAccountVisibility(open: false);

    // The account header needs the full pane; compact mode shows icons only.
    private void UpdateAccountVisibility(bool? open = null) =>
        Account.Visibility = (open ?? Nav.IsPaneOpen) || Nav.DisplayMode == NavigationViewDisplayMode.Expanded
            ? Visibility.Visible : Visibility.Collapsed;
}
