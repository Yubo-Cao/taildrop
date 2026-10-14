using System.Collections.ObjectModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using TailDrop.UI.Core;
using Windows.System;

namespace TailDrop.UI.Views;

/// <summary>"Send 3 files to…": every device in the tailnet, for files dropped or picked without one.</summary>
public sealed partial class PickerWindow : Window
{
    private const double WidthDips = 420;

    private readonly List<string> _paths;
    private readonly ObservableCollection<DeviceVm> _tiles = new();
    private bool _presented;

    public PickerWindow(IReadOnlyList<string> paths)
    {
        _paths = paths.ToList();
        InitializeComponent();
        Title = Str.Get("Picker_Title");
        AppTitleBar.Title = Str.Get("Picker_Title");
        Popups.MakeDialog(this, AppTitleBar);
        Tiles.ItemsSource = _tiles;
        ChooseLabel.Text = Str.Get("ChooseDevice");
        AddFilesButton.Content = Str.Get("Picker_AddFiles");
        CancelButton.Content = Str.Get("Cancel");
        NoDevices.Text = Str.Get("NoDevices");
        ShowFiles();
        Store.DevicesChanged += Refresh;
        Closed += (_, _) => Store.DevicesChanged -= Refresh;
        Refresh();
    }

    private void ShowFiles()
    {
        Heading.Text = _paths.Count == 1 ? Format.FileName(_paths[0]) : Str.F("NItems", _paths.Count);
        Subtitle.Text = Describe(_paths);
        BackCard.Visibility = _paths.Count > 1 ? Visibility.Visible : Visibility.Collapsed;
        bool folder = System.IO.Directory.Exists(_paths[0]);
        FileGlyph.Glyph = folder ? "" : "";
        if (!folder) _ = LoadThumbAsync(_paths[0]);
    }

    private static string Describe(IReadOnlyList<string> paths)
    {
        long total = 0;
        int folders = 0;
        foreach (string p in paths)
        {
            try
            {
                if (System.IO.Directory.Exists(p)) folders++;
                else total += new System.IO.FileInfo(p).Length;
            }
            catch
            {
            }
        }
        if (paths.Count == 1 && folders == 1) return Str.Get("Folder");
        string size = total > 0 ? Format.Size(total) : "";
        if (paths.Count == 1) return size;
        string names = string.Join(", ", paths.Take(3).Select(Format.FileName)) + (paths.Count > 3 ? ", …" : "");
        return size.Length > 0 ? size + " · " + names : names;
    }

    private async Task LoadThumbAsync(string path)
    {
        var t = await Thumbs.GetAsync(path, 96);
        if (t == null || path != _paths[0]) return;
        FileThumb.Source = t.Source;
        FileThumb.Stretch = t.IsPicture ? Stretch.UniformToFill : Stretch.Uniform;
        FileThumb.Margin = t.IsPicture ? new Thickness(0) : new Thickness(8);
        FileGlyph.Visibility = Visibility.Collapsed;
    }

    private void Refresh()
    {
        var models = Store.Devices;
        Collections.Sync(_tiles, models, d => d.Id, v => v.Id, DeviceVm.From, (v, d) => v.Update(d));
        NoDevices.Visibility = models.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        TileScroller.Visibility = models.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
        if (_presented) DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low,
            () => Popups.FitDialog(this, Root, WidthDips));
    }

    /// <summary>Shows it in the middle of the screen the pointer is on.</summary>
    public void Present()
    {
        if (_presented)
        {
            Activate();
            return;
        }
        _presented = true;
        Popups.PresentDialog(this, Root, WidthDips, () =>
        {
            if (_tiles.Count > 0) CancelButton.Focus(FocusState.Programmatic);
        });
    }

    private async void OnAddFiles(object sender, RoutedEventArgs e)
    {
        var more = await App.Instance.ChooseFilesAsync(this);
        if (more.Count == 0) return;
        foreach (string p in more)
            if (!_paths.Contains(p, StringComparer.OrdinalIgnoreCase)) _paths.Add(p);
        ShowFiles();
        Popups.FitDialog(this, Root, WidthDips);
    }

    private void OnTileClick(object sender, RoutedEventArgs e)
    {
        if (Bound.Item<DeviceVm>(sender) is not { } vm || !vm.Online) return;
        HostLink.SendFiles(vm.Id, _paths);
        Close();
    }

    private void OnCancel(object sender, RoutedEventArgs e) => Close();

    private void OnKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Escape)
        {
            Close();
            e.Handled = true;
        }
    }
}
