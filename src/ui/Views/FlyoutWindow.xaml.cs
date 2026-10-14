using System.Collections.ObjectModel;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Microsoft.Windows.Storage.Pickers;
using TailDrop.UI.Core;
using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics;
using Windows.Storage;

namespace TailDrop.UI.Views;

/// <summary>The panel above the tray icon: devices to send to, files received.</summary>
public sealed partial class FlyoutWindow : Window
{
    private const double WidthDips = 360;
    private const int MaxTiles = 9;

    private readonly ObservableCollection<DeviceVm> _tiles = new();
    private readonly ObservableCollection<ItemVm> _rows = new();
    private int[]? _tray;
    private bool _dropMode, _shown, _hiding, _zoneDrag;
    private DateTime _hiddenAt = DateTime.MinValue;

    public event Action? ShownChanged;

    public bool IsShown => _shown;

    public FlyoutWindow()
    {
        InitializeComponent();
        Popups.MakePopup(this, noActivate: false);
        Tiles.ItemsSource = _tiles;
        Rows.ItemsSource = _rows;

        OfflineTitle.Text = Str.Get("NotConnectedTitle");
        OpenTailscaleButton.Content = Str.Get("OpenTailscale");
        RetryButton.Content = Str.Get("Retry");
        ReceivedLabel.Text = Str.Get("Received");
        SaveAllButton.Content = Str.Get("SaveAll");
        NoItems.Text = Str.Get("NoItems");
        NoDevices.Text = Str.Get("NoDevices");
        InboxLabel.Text = Str.Get("InboxButton");
        ToolTipService.SetToolTip(InboxButton, Str.Get("OpenInboxTooltip"));
        ToolTipService.SetToolTip(SettingsButton, Str.Get("Settings"));

        Store.StateChanged += Refresh;
        Store.DevicesChanged += Refresh;
        Store.ItemsChanged += Refresh;
        Store.SettingsChanged += Refresh;
        Store.DragEnded += OnDragEnded;
        Activated += OnActivated;
        Closed += (_, _) =>
        {
            Store.StateChanged -= Refresh;
            Store.DevicesChanged -= Refresh;
            Store.ItemsChanged -= Refresh;
            Store.SettingsChanged -= Refresh;
            Store.DragEnded -= OnDragEnded;
        };
        Refresh();
    }

    // ------------------------------------------------------------------
    // Showing and hiding

    public async void Toggle(int[]? tray, bool dropMode)
    {
        if (_shown && !dropMode && !_dropMode)
        {
            Hide();
            return;
        }
        // The click that deactivated (and so hid) the flyout also reaches the tray icon.
        if (!dropMode && (DateTime.Now - _hiddenAt).TotalMilliseconds < 350) return;
        await ShowAsync(tray, dropMode);
    }

    public async Task ShowAsync(int[]? tray, bool dropMode)
    {
        if (tray is { Length: 4 } && tray[2] > tray[0]) _tray = tray;
        _dropMode = dropMode;
        _hiding = false;
        Refresh();
        var aw = AppWindow;
        bool wasShown = _shown;
        if (!wasShown)
        {
            // Lay out off screen first, by the tray's display (so at its DPI), so the
            // size is known before it slides in.
            aw.Move(Popups.OffscreenNear(TrayPoint()));
            aw.Show(false);
            await Task.Delay(16);
        }
        _shown = true;
        ShownChanged?.Invoke();
        var (target, from) = Placement();
        if (!wasShown)
        {
            aw.Move(from);
            Popups.Fade(Root, 0, 1, 167, Popups.Decelerate);
            Popups.Animate(aw, target, 267, Popups.Decelerate);
        }
        else
        {
            Popups.Animate(aw, target, 167, Popups.Standard);
        }
        if (!dropMode)
        {
            Activate();
            Native.SetForegroundWindow(Popups.Hwnd(this));
        }
    }

    public void Hide()
    {
        if (!_shown || _hiding) return;
        _hiding = true;
        _hiddenAt = DateTime.Now;
        var aw = AppWindow;
        var (_, away) = Placement();
        Popups.Fade(Root, 1, 0, 117, Popups.Accelerate);
        Popups.Animate(aw, away, 117, Popups.Accelerate, () =>
        {
            if (!_hiding) return;
            aw.Hide();
            _shown = false;
            _hiding = false;
            _dropMode = false;
            ShownChanged?.Invoke();
        });
    }

    private (PointInt32 target, PointInt32 offset) Placement()
    {
        double scale = Popups.Scale(this);
        var work = Popups.WorkArea(_tray);
        bool top = Popups.TaskbarAtTop(_tray, work);
        int margin = (int)(12 * scale);

        // Fit the content; the body scrolls if the screen is too short.
        Body.MaxHeight = double.PositiveInfinity;
        double h = Popups.LaidOutHeight(Root);
        double maxH = (work.Height - 2 * margin) / scale;
        if (h > maxH)
        {
            Body.MaxHeight = Math.Max(120, Body.DesiredSize.Height - (h - maxH));
            h = maxH;
        }
        var outer = Popups.FitContent(this, Root, WidthDips, h);

        int center = _tray is { Length: 4 } && _tray[2] > _tray[0] ? (_tray[0] + _tray[2]) / 2 : work.X + work.Width - margin;
        int x = Math.Clamp(center - outer.Width / 2, work.X + margin, work.X + work.Width - margin - outer.Width);
        int y = top ? work.Y + margin : work.Y + work.Height - margin - outer.Height;
        int slide = (int)(24 * scale);
        return (new PointInt32(x, y), new PointInt32(x, top ? y - slide : y + slide));
    }

    private PointInt32 TrayPoint()
    {
        if (_tray is { Length: 4 } && _tray[2] > _tray[0]) return new PointInt32((_tray[0] + _tray[2]) / 2, (_tray[1] + _tray[3]) / 2);
        var work = DisplayArea.Primary.WorkArea;
        return new PointInt32(work.X + work.Width - 1, work.Y + work.Height - 1);
    }

    private void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        if (args.WindowActivationState == WindowActivationState.Deactivated)
        {
            if (_shown && !_dropMode) Hide();
        }
        else
        {
            _dropMode = false;
        }
    }

    private void OnDragEnded()
    {
        if (_dropMode && _shown) Hide();
    }

    private void Relayout()
    {
        if (!_shown || _hiding) return;
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            if (!_shown || _hiding) return;
            var (target, _) = Placement();
            Popups.Animate(AppWindow, target, 167, Popups.Standard);
        });
    }

    // ------------------------------------------------------------------
    // Content

    private void Refresh()
    {
        bool ok = Store.Connected;
        StatusDot.Fill = (Brush)Application.Current.Resources[ok ? "SystemFillColorSuccessBrush" : "SystemFillColorCautionBrush"];
        StatusText.Text = ok
            ? (Store.Tailnet.Length > 0 ? Store.Self + " · " + Store.Tailnet : Store.Self)
            : Str.Get("NotConnectedTitle");
        OfflineCard.Visibility = ok ? Visibility.Collapsed : Visibility.Visible;
        OnlinePanel.Visibility = ok ? Visibility.Visible : Visibility.Collapsed;
        OfflineMessage.Text = Str.Get("Conn_" + Store.Conn);
        SendLabel.Text = Str.Get(_dropMode || _zoneDrag ? "DropOnDevice" : "SendTo");

        var devices = Store.Devices;
        var shown = devices.Count > MaxTiles ? devices.Take(MaxTiles - 1).ToList() : devices;
        var models = shown.Select(d => (d.Id, d)).ToList();
        Collections.Sync(_tiles, models, m => m.Id, v => v.Id, m => DeviceVm.From(m.d), (v, m) => v.Update(m.d));
        bool more = devices.Count > MaxTiles;
        if (more && !_tiles.Any(t => t.IsMore))
            _tiles.Add(new DeviceVm { Id = "*more", IsMore = true, Name = Str.Get("More"), Glyph = "", Online = true });
        if (!more)
            for (int i = _tiles.Count - 1; i >= 0; i--)
                if (_tiles[i].IsMore) _tiles.RemoveAt(i);
        NoDevices.Visibility = devices.Count == 0 ? Visibility.Visible : Visibility.Collapsed;

        // Newest first, a handful only: the inbox folder has the rest.
        var items = Store.Items.AsEnumerable().Reverse().Take(4).ToList();
        Collections.Sync(_rows, items, i => i.Id.ToString(), v => v.Key,
            i => UpdateRow(new ItemVm { Id = i.Id }, i), (v, i) => UpdateRow(v, i));
        NoItems.Visibility = items.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        SaveAllButton.Visibility = Store.Items.Count(i => i.State == "pending") > 1 ? Visibility.Visible : Visibility.Collapsed;
        _ = UpdateSavesToAsync();
        Relayout();
    }

    private async Task UpdateSavesToAsync()
    {
        string dir = Store.Settings.EffectiveDir;
        SavesTo.Content = Str.F("SavesTo", Format.FolderNameOrPath(dir));
        string name = await Format.FolderNameAsync(dir);
        SavesTo.Content = Str.F("SavesTo", name);
    }

    private ItemVm UpdateRow(ItemVm vm, ItemInfo i)
    {
        vm.Name = i.Name;
        vm.State = i.State;
        vm.Progress = i.Size > 0 ? Math.Clamp((double)i.Received / i.Size, 0, 1) : 0;
        vm.Status = ItemStatus(i);
        if (vm.Path != i.Path)
        {
            vm.Path = i.Path;
            vm.Thumb = null;
            if (i.HasFile) _ = LoadThumbAsync(vm, i.Path);
        }
        return vm;
    }

    private static async Task LoadThumbAsync(ItemVm vm, string path)
    {
        var t = await Thumbs.GetAsync(path, 96);
        if (t == null || vm.Path != path) return;
        vm.IsPicture = t.IsPicture;
        vm.Thumb = t.Source;
    }

    public static string ItemStatus(ItemInfo i) => i.State switch
    {
        "receiving" => Str.F("Item_receiving", i.Size > 0 ? (int)(i.Received * 100 / i.Size) : 0),
        "landing" => Str.Get("Item_landing"),
        "pending" => Str.F("Item_pending", Format.Size(i.Size)),
        "saving" => Str.Get("Item_saving"),
        "saved" => Str.F("Item_saved", Format.FolderNameOrPath(System.IO.Path.GetDirectoryName(i.Path) ?? "")),
        _ => Str.Get(i.Note.Length > 0 ? "Note_" + i.Note : "Note_incomplete"),
    };

    // ------------------------------------------------------------------
    // Devices

    private static DeviceVm? Vm(object sender) => Bound.Item<DeviceVm>(sender);

    private async void OnTileClick(object sender, RoutedEventArgs e)
    {
        var vm = Vm(sender);
        if (vm == null) return;
        if (vm.IsMore)
        {
            Hide();
            await App.Instance.PickFilesAsync(null);
            return;
        }
        if (!vm.Online) return;
        Hide();
        await App.Instance.PickFilesAsync(vm.Id);
    }

    private void OnTileRightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        var vm = Vm(sender);
        if (vm == null || vm.IsMore) return;
        var menu = DeviceMenu.Create(vm.Id, Hide);
        menu.ShowAt((FrameworkElement)sender, e.GetPosition((FrameworkElement)sender));
        e.Handled = true;
    }

    private void OnTileDragOver(object sender, DragEventArgs e)
    {
        var vm = Vm(sender);
        e.Handled = true;
        if (vm == null || !e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        SetZoneDrag(true);
        e.DragUIOverride.IsCaptionVisible = true;
        e.DragUIOverride.IsGlyphVisible = true;
        if (vm.IsMore)
        {
            e.AcceptedOperation = DataPackageOperation.Copy;
            e.DragUIOverride.Caption = Str.Get("DropChoose");
            vm.DropHover = true;
        }
        else if (vm.Online)
        {
            e.AcceptedOperation = DataPackageOperation.Copy;
            e.DragUIOverride.Caption = Str.F("DropSendTo", vm.Name);
            vm.DropHover = true;
        }
        else
        {
            e.AcceptedOperation = DataPackageOperation.None;
            e.DragUIOverride.Caption = Str.F("DropOffline", vm.Name);
        }
    }

    private void OnTileDragLeave(object sender, DragEventArgs e)
    {
        if (Vm(sender) is { } vm) vm.DropHover = false;
    }

    private async void OnTileDrop(object sender, DragEventArgs e)
    {
        e.Handled = true;
        var vm = Vm(sender);
        if (vm == null) return;
        vm.DropHover = false;
        var paths = await DroppedPathsAsync(e);
        SetZoneDrag(false);
        if (paths.Count == 0) return;
        Hide();
        if (vm.IsMore) App.Instance.OpenPicker(paths);
        else if (vm.Online) HostLink.SendFiles(vm.Id, paths);
    }

    private void OnZoneDragOver(object sender, DragEventArgs e)
    {
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return;
        SetZoneDrag(true);
        e.AcceptedOperation = DataPackageOperation.Copy;
        e.DragUIOverride.Caption = Str.Get("DropChoose");
        ZoneHighlight.Opacity = 1;
    }

    private void OnZoneDragLeave(object sender, DragEventArgs e)
    {
        ZoneHighlight.Opacity = 0;
        SetZoneDrag(false);
    }

    private async void OnZoneDrop(object sender, DragEventArgs e)
    {
        ZoneHighlight.Opacity = 0;
        var paths = await DroppedPathsAsync(e);
        SetZoneDrag(false);
        if (paths.Count == 0) return;
        Hide();
        App.Instance.OpenPicker(paths);
    }

    private void SetZoneDrag(bool on)
    {
        if (_zoneDrag == on) return;
        _zoneDrag = on;
        SendLabel.Text = Str.Get(_dropMode || _zoneDrag ? "DropOnDevice" : "SendTo");
    }

    public static async Task<List<string>> DroppedPathsAsync(DragEventArgs e)
    {
        var paths = new List<string>();
        if (!e.DataView.Contains(StandardDataFormats.StorageItems)) return paths;
        var deferral = e.GetDeferral();
        try
        {
            foreach (var item in await e.DataView.GetStorageItemsAsync())
                if (!string.IsNullOrEmpty(item.Path)) paths.Add(item.Path);
        }
        catch
        {
        }
        finally
        {
            deferral.Complete();
        }
        return paths;
    }

    // ------------------------------------------------------------------
    // Received files

    private static ItemVm? Row(object sender) => Bound.Item<ItemVm>(sender);

    private static bool FromButton(object? source)
    {
        for (var d = source as DependencyObject; d != null; d = VisualTreeHelper.GetParent(d))
            if (d is ButtonBase) return true;
        return false;
    }

    private void OnRowPointerEntered(object sender, PointerRoutedEventArgs e)
    {
        if (Row(sender) is { } vm) vm.Hover = true;
        if (sender is Grid g) g.Background = (Brush)Application.Current.Resources["SubtleFillColorSecondaryBrush"];
    }

    private void OnRowPointerExited(object sender, PointerRoutedEventArgs e)
    {
        if (Row(sender) is { } vm) vm.Hover = false;
        if (sender is Grid g) g.Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent);
    }

    private void OnRowTapped(object sender, TappedRoutedEventArgs e)
    {
        if (FromButton(e.OriginalSource)) return;
        if (Row(sender) is { } vm && vm.State is "pending" or "saved") HostLink.Open(vm.Id);
    }

    private void OnRowSave(object sender, RoutedEventArgs e)
    {
        if (Row(sender) is { } vm) HostLink.Save(vm.Id);
    }

    private void OnRowOpen(object sender, RoutedEventArgs e)
    {
        if (Row(sender) is { } vm) HostLink.Open(vm.Id);
    }

    private void OnRowRemove(object sender, RoutedEventArgs e)
    {
        if (Row(sender) is not { } vm) return;
        if (vm.State == "pending") HostLink.Discard(vm.Id);
        else HostLink.Forget(vm.Id);
    }

    private void OnRowRightTapped(object sender, RightTappedRoutedEventArgs e)
    {
        if (Row(sender) is not { } vm) return;
        var menu = ItemMenu.Create(vm.Id, this);
        menu.ShowAt((FrameworkElement)sender, e.GetPosition((FrameworkElement)sender));
        e.Handled = true;
    }

    private async void OnRowDragStarting(UIElement sender, DragStartingEventArgs args)
    {
        if (Row(sender) is not { } vm || vm.State is not ("pending" or "saved") || vm.Path.Length == 0)
        {
            args.Cancel = true;
            return;
        }
        var deferral = args.GetDeferral();
        try
        {
            var file = await StorageFile.GetFileFromPathAsync(vm.Path);
            args.Data.SetStorageItems(new List<IStorageItem> { file });
            args.Data.RequestedOperation = DataPackageOperation.Move;
            args.AllowedOperations = DataPackageOperation.Copy | DataPackageOperation.Move;
            if (vm.Thumb is BitmapImage bmp) args.DragUI.SetContentFromBitmapImage(bmp);
        }
        catch
        {
            args.Cancel = true;
        }
        finally
        {
            deferral.Complete();
        }
    }

    private void OnRowDropCompleted(UIElement sender, DropCompletedEventArgs args) => HostLink.DragDone();

    private void OnSaveAll(object sender, RoutedEventArgs e)
    {
        foreach (var i in Store.Items.Where(i => i.State == "pending").ToList()) HostLink.Save(i.Id);
    }

    // ------------------------------------------------------------------
    // Header and footer

    private void OnInbox(object sender, RoutedEventArgs e)
    {
        Hide();
        HostLink.OpenInbox();
    }

    private void OnSettings(object sender, RoutedEventArgs e)
    {
        Hide();
        App.Instance.OpenSettings(null);
    }

    private void OnOpenTailscale(object sender, RoutedEventArgs e)
    {
        Hide();
        HostLink.OpenTailscale();
    }

    private void OnRetry(object sender, RoutedEventArgs e) => HostLink.Refresh();
}
