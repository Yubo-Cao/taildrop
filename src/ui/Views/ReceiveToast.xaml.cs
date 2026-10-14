using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using TailDrop.UI.Core;
using Windows.ApplicationModel.DataTransfer;
using Windows.Storage;

namespace TailDrop.UI.Views;

/// <summary>"You received a file": preview, where it goes, Save / Open.</summary>
public sealed partial class ReceiveToast : UserControl, IToastContent
{
    private readonly int _batch;
    private string _dest = "";  // folder picked for this notification; "" = default
    private string _thumbPath = "";
    private Thumbs.Thumb? _thumb;
    private bool _seenItems, _closed;
    private readonly Dictionary<string, Thumbs.Thumb?> _stripThumbs = new();

    public FrameworkElement Root => this;
    public Window? HostWindow { get; set; }
    public event Action? LayoutChanged;
    public event Action? CloseRequested;

    public ReceiveToast(int batch)
    {
        _batch = batch;
        InitializeComponent();
        ToolTipService.SetToolTip(CloseButton, Str.Get("Close"));
        ToolTipService.SetToolTip(MoreButton, Str.Get("MoreOptions"));
        DestLead.Text = Str.Get("SaveToLead");
        Store.ItemsChanged += Update;
        Store.SettingsChanged += Update;
        _ = PrefetchFolderNamesAsync();
        Update();
    }

    public int AutoDismissMs
    {
        get
        {
            var items = Store.Batch(_batch);
            if (items.Count == 0 || items.Any(i => i.IsWaiting)) return 0;
            if (items.All(i => i.State == "saved")) return 6000;
            if (items.All(i => i.State == "failed")) return 15000;
            return 30000;
        }
    }

    public void OnClosed()
    {
        if (_closed) return;
        _closed = true;
        Store.ItemsChanged -= Update;
        Store.SettingsChanged -= Update;
        HostLink.ToastClosed(_batch);
    }

    private async Task PrefetchFolderNamesAsync()
    {
        foreach (var dir in Folders()) await Format.FolderNameAsync(dir);
        Update();
    }

    private IEnumerable<string> Folders()
    {
        var list = new List<string> { Store.Settings.EffectiveDir, Store.Settings.Downloads };
        list.Add(Environment.GetFolderPath(Environment.SpecialFolder.Desktop));
        list.Add(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments));
        list.Add(Environment.GetFolderPath(Environment.SpecialFolder.MyPictures));
        if (_dest.Length > 0) list.Add(_dest);
        return list.Where(d => !string.IsNullOrEmpty(d)).Distinct(StringComparer.OrdinalIgnoreCase);
    }

    private void Update()
    {
        if (_closed) return;
        var items = Store.Batch(_batch);
        if (items.Count == 0)
        {
            if (_seenItems) CloseRequested?.Invoke();
            return;
        }
        _seenItems = true;
        bool receiving = items.Any(i => i.State == "receiving");
        bool waiting = items.Any(i => i.IsWaiting);
        bool pending = items.Any(i => i.IsPending);
        bool saved = items.All(i => i.State == "saved");
        bool failed = items.All(i => i.State == "failed");
        bool multi = items.Count > 1;
        var first = items[0];

        Caption.Text = receiving ? "TailDrop · " + Str.Get("Receiving") : "TailDrop";
        Title.Text = multi ? Str.F("NFiles", items.Count) : first.Name;

        long total = items.Sum(i => Math.Max(0, i.Size));
        long got = items.Sum(i => i.State == "receiving" ? i.Received : Math.Max(0, i.Size));
        SavedGlyph.Visibility = saved ? Visibility.Visible : Visibility.Collapsed;
        Subtitle.Foreground = (Brush)Application.Current.Resources[failed ? "SystemFillColorCriticalBrush" : "TextFillColorSecondaryBrush"];
        if (receiving)
            Subtitle.Text = Str.F("ToastReceiving", total > 0 ? (int)(got * 100 / total) : 0, Format.Size(got), Format.Size(total));
        else if (waiting && !pending)
            Subtitle.Text = Str.Get("Item_landing");
        else if (saved)
            Subtitle.Text = Str.F("Item_saved", Format.FolderNameOrPath(System.IO.Path.GetDirectoryName(first.Path) ?? ""));
        else if (failed)
            Subtitle.Text = Str.Get("Note_" + (first.Note.Length > 0 ? first.Note : "incomplete"));
        else
            Subtitle.Text = items.FirstOrDefault(i => i.Note.Length > 0) is { } bad
                ? Str.Get("Note_" + bad.Note)
                : Format.Size(total) + (multi ? "" : TypeSuffix(first.Name));

        Progress.Visibility = waiting && !pending ? Visibility.Visible : Visibility.Collapsed;
        Progress.IsIndeterminate = !receiving;
        Progress.Value = total > 0 ? (double)got / total : 0;

        UpdatePreview(items, multi);

        DestButton.Visibility = pending && !Store.Settings.AutoSave ? Visibility.Visible : Visibility.Collapsed;
        DestName.Text = Format.FolderNameOrPath(_dest.Length > 0 ? _dest : Store.Settings.EffectiveDir);
        DestButton.Flyout = DestMenu();

        Buttons.Visibility = pending || saved || failed ? Visibility.Visible : Visibility.Collapsed;
        SecondaryButton.Visibility = Visibility.Visible;
        MoreButton.Visibility = Visibility.Visible;
        Grid.SetColumnSpan(PrimaryButton, 1);
        if (pending)
        {
            PrimaryButton.Content = Str.Get(multi ? "SaveAll" : "Save");
            PrimaryButton.IsEnabled = !items.Any(i => i.State == "saving");
            SecondaryButton.Content = Str.Get(multi ? "ShowFiles" : "Open");
            MoreButton.Flyout = MoreMenu(items, multi);
        }
        else if (saved)
        {
            PrimaryButton.Content = Str.Get(multi ? "ShowInFolder" : "Open");
            PrimaryButton.IsEnabled = true;
            SecondaryButton.Content = Str.Get("ShowInFolder");
            SecondaryButton.Visibility = multi ? Visibility.Collapsed : Visibility.Visible;
            MoreButton.Visibility = Visibility.Collapsed;
            if (multi) Grid.SetColumnSpan(PrimaryButton, 3);
        }
        else if (failed)
        {
            PrimaryButton.Content = Str.Get("Dismiss");
            PrimaryButton.IsEnabled = true;
            SecondaryButton.Visibility = Visibility.Collapsed;
            MoreButton.Visibility = Visibility.Collapsed;
            Grid.SetColumnSpan(PrimaryButton, 3);
        }
        LayoutChanged?.Invoke();
    }

    private static string TypeSuffix(string name)
    {
        string ext = System.IO.Path.GetExtension(name).TrimStart('.').ToUpperInvariant();
        return ext.Length > 0 ? " · " + Str.F("FileType", ext) : "";
    }

    private void UpdatePreview(List<ItemInfo> items, bool multi)
    {
        if (!multi)
        {
            var it = items[0];
            if (it.HasFile && it.Path != _thumbPath)
            {
                _thumbPath = it.Path;
                _ = LoadThumbAsync(it.Path);
            }
            bool hero = _thumb is { IsPicture: true };
            HeroFrame.Visibility = hero ? Visibility.Visible : Visibility.Collapsed;
            Strip.Visibility = Visibility.Collapsed;
            Preview.Visibility = hero ? Visibility.Visible : Visibility.Collapsed;
            IconFrame.Visibility = hero ? Visibility.Collapsed : Visibility.Visible;
            if (hero)
            {
                Hero.Source = _thumb!.Source;
                double w = ToastWindow.WidthDips - 32;
                HeroFrame.Height = Math.Clamp(w / Math.Max(0.1, _thumb.Aspect), 120, 200);
            }
            else
            {
                IconImage.Source = _thumb?.Source;
                IconPlaceholder.Visibility = _thumb == null ? Visibility.Visible : Visibility.Collapsed;
            }
            return;
        }
        HeroFrame.Visibility = Visibility.Collapsed;
        IconFrame.Visibility = Visibility.Collapsed;
        Preview.Visibility = Visibility.Visible;
        Strip.Visibility = Visibility.Visible;
        int n = Math.Min(4, items.Count);
        // The thumbnails share the width; two get wide tiles rather than huge squares.
        double cellWidth = (ToastWindow.WidthDips - 32 - (n - 1) * 6) / n;
        double cellHeight = Math.Min(cellWidth, 120);
        Strip.Children.Clear();
        Strip.ColumnDefinitions.Clear();
        for (int i = 0; i < n; i++)
        {
            Strip.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var it = items[i];
            var cell = new Grid { Width = cellWidth, Height = cellHeight };
            var frame = new Border
            {
                CornerRadius = new CornerRadius(6),
                BorderThickness = new Thickness(1),
                BorderBrush = (Brush)Application.Current.Resources["CardStrokeColorDefaultBrush"],
                Background = (Brush)Application.Current.Resources["LayerFillColorDefaultBrush"],
            };
            if (it.HasFile && _stripThumbs.TryGetValue(it.Path, out var t) && t != null)
                frame.Child = new Image
                {
                    Source = t.Source,
                    Stretch = t.IsPicture ? Stretch.UniformToFill : Stretch.Uniform,
                    Margin = t.IsPicture ? new Thickness(0) : new Thickness(14),
                };
            else
            {
                frame.Child = new FontIcon { Glyph = "", FontSize = 20, Foreground = (Brush)Application.Current.Resources["TextFillColorSecondaryBrush"] };
                if (it.HasFile && !_stripThumbs.ContainsKey(it.Path)) _ = LoadStripThumbAsync(it.Path);
            }
            cell.Children.Add(frame);
            if (it.State == "receiving")
                cell.Children.Add(new ProgressBar
                {
                    Maximum = 1,
                    Value = it.Size > 0 ? (double)it.Received / it.Size : 0,
                    VerticalAlignment = VerticalAlignment.Bottom,
                    Margin = new Thickness(8, 0, 8, 8),
                });
            if (i == n - 1 && items.Count > 4)
            {
                cell.Children.Add(new Border
                {
                    CornerRadius = new CornerRadius(6),
                    Background = new SolidColorBrush(Windows.UI.Color.FromArgb(0x80, 0, 0, 0)),
                    Child = new TextBlock
                    {
                        Text = "+" + (items.Count - 3),
                        FontSize = 18,
                        FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
                        Foreground = new SolidColorBrush(Microsoft.UI.Colors.White),
                        HorizontalAlignment = HorizontalAlignment.Center,
                        VerticalAlignment = VerticalAlignment.Center,
                    },
                });
            }
            Grid.SetColumn(cell, i);
            Strip.Children.Add(cell);
        }
    }

    private async Task LoadThumbAsync(string path)
    {
        var t = await Thumbs.GetAsync(path, 320);
        if (path != _thumbPath) return;
        _thumb = t;
        Update();
    }

    private async Task LoadStripThumbAsync(string path)
    {
        _stripThumbs[path] = null;
        var t = await Thumbs.GetAsync(path, 160);
        _stripThumbs[path] = t;
        if (t != null) Update();
    }

    private MenuFlyout DestMenu()
    {
        var menu = new MenuFlyout();
        string current = _dest.Length > 0 ? _dest : Store.Settings.EffectiveDir;
        foreach (string dir in Folders().Where(d => !string.Equals(d, Store.Settings.Downloads, StringComparison.OrdinalIgnoreCase) ||
                                                     string.Equals(d, Store.Settings.EffectiveDir, StringComparison.OrdinalIgnoreCase)))
        {
            bool isDefault = string.Equals(dir, Store.Settings.EffectiveDir, StringComparison.OrdinalIgnoreCase);
            var item = new RadioMenuFlyoutItem
            {
                Text = Format.FolderNameOrPath(dir) + (isDefault ? " " + Str.Get("DefaultSuffix") : ""),
                GroupName = "dest",
                IsChecked = string.Equals(dir, current, StringComparison.OrdinalIgnoreCase),
            };
            string d = dir;
            item.Click += (_, _) =>
            {
                _dest = isDefault ? "" : d;
                Update();
            };
            menu.Items.Add(item);
        }
        menu.Items.Add(new MenuFlyoutSeparator());
        var choose = new MenuFlyoutItem { Text = Str.Get("ChooseFolder"), Icon = new FontIcon { Glyph = "" } };
        choose.Click += async (_, _) =>
        {
            if (HostWindow == null) return;
            string? dir = await App.PickFolderAsync(HostWindow);
            if (dir == null) return;
            _dest = dir;
            await Format.FolderNameAsync(dir);
            Update();
        };
        menu.Items.Add(choose);
        return menu;
    }

    private MenuFlyout MoreMenu(List<ItemInfo> items, bool multi)
    {
        var menu = new MenuFlyout();
        var saveTo = new MenuFlyoutItem { Text = Str.Get(multi ? "SaveAllTo" : "SaveTo"), Icon = new FontIcon { Glyph = "" } };
        saveTo.Click += async (_, _) =>
        {
            if (HostWindow == null) return;
            string? dir = await App.PickFolderAsync(HostWindow);
            if (dir != null) HostLink.SaveBatch(_batch, dir);
        };
        menu.Items.Add(saveTo);
        var reveal = new MenuFlyoutItem { Text = Str.Get("ShowInReceived"), Icon = new FontIcon { Glyph = "" } };
        reveal.Click += (_, _) => HostLink.Reveal(items[0].Id);
        menu.Items.Add(reveal);
        menu.Items.Add(new MenuFlyoutSeparator());
        var delete = new MenuFlyoutItem { Text = Str.Get(multi ? "DeleteAll" : "Delete"), Icon = new FontIcon { Glyph = "" } };
        delete.Click += (_, _) =>
        {
            foreach (var i in Store.Batch(_batch)) HostLink.Discard(i.Id);
        };
        menu.Items.Add(delete);
        return menu;
    }

    private void OnClose(object sender, RoutedEventArgs e) => CloseRequested?.Invoke();

    private void OnPrimary(object sender, RoutedEventArgs e)
    {
        var items = Store.Batch(_batch);
        if (items.Count == 0) return;
        if (items.Any(i => i.IsPending)) HostLink.SaveBatch(_batch, _dest);
        else if (items.All(i => i.State == "saved"))
        {
            if (items.Count > 1) HostLink.Reveal(items[0].Id);
            else HostLink.Open(items[0].Id);
            CloseRequested?.Invoke();
        }
        else CloseRequested?.Invoke();
    }

    private void OnSecondary(object sender, RoutedEventArgs e)
    {
        var items = Store.Batch(_batch);
        if (items.Count == 0) return;
        if (items.Count > 1 || items[0].State == "saved") HostLink.Reveal(items[0].Id);
        else HostLink.Open(items[0].Id);
    }

    private void OnPreviewTapped(object sender, TappedRoutedEventArgs e)
    {
        var items = Store.Batch(_batch);
        if (items.Count == 1 && items[0].HasFile) HostLink.Open(items[0].Id);
    }

    private async void OnDragStarting(UIElement sender, DragStartingEventArgs args)
    {
        var files = Store.Batch(_batch).Where(i => i.HasFile).ToList();
        if (files.Count == 0)
        {
            args.Cancel = true;
            return;
        }
        var deferral = args.GetDeferral();
        try
        {
            var storage = new List<IStorageItem>();
            foreach (var i in files) storage.Add(await StorageFile.GetFileFromPathAsync(i.Path));
            args.Data.SetStorageItems(storage);
            args.Data.RequestedOperation = DataPackageOperation.Move;
            args.AllowedOperations = DataPackageOperation.Copy | DataPackageOperation.Move;
            if (_thumb?.Source is BitmapImage bmp) args.DragUI.SetContentFromBitmapImage(bmp);
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

    private void OnDropCompleted(UIElement sender, DropCompletedEventArgs args) => HostLink.DragDone();
}
