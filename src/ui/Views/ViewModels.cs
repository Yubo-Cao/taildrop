using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using TailDrop.UI.Core;

namespace TailDrop.UI.Views;

public static class Bound
{
    /// <summary>The view model an event came from: the Tag of the sender or of an element around it.</summary>
    public static T? Item<T>(object? sender) where T : class
    {
        for (var d = sender as DependencyObject; d != null; d = VisualTreeHelper.GetParent(d))
            if (d is FrameworkElement { Tag: T vm }) return vm;
        return null;
    }
}

/// <summary>A device tile in the flyout, a row in the picker or in Settings.</summary>
public sealed partial class DeviceVm : Observable
{
    private string _name = "", _glyph = "", _status = "", _kind = "other";
    private bool _online, _favorite, _dropHover;

    public string Id { get; init; } = "";
    public bool IsMore { get; init; }
    /// <summary>For Tag="{x:Bind Self}": ItemsRepeater leaves DataContext unset with x:Bind templates.</summary>
    public DeviceVm Self => this;

    public string Name { get => _name; set => Set(ref _name, value); }
    public string Glyph { get => _glyph; set => Set(ref _glyph, value); }
    public string Status { get => _status; set => Set(ref _status, value); }
    public string Kind { get => _kind; set => Set(ref _kind, value); }

    public bool Online
    {
        get => _online;
        set
        {
            if (Set(ref _online, value))
            {
                Raise(nameof(ContentOpacity));
                Raise(nameof(OfflineVisibility));
                Raise(nameof(OnlineDotOpacity));
            }
        }
    }

    public bool Favorite
    {
        get => _favorite;
        set
        {
            if (Set(ref _favorite, value))
            {
                Raise(nameof(FavoriteVisibility));
                Raise(nameof(StarGlyph));
            }
        }
    }

    public bool DropHover
    {
        get => _dropHover;
        set
        {
            if (Set(ref _dropHover, value)) Raise(nameof(DropHighlightOpacity));
        }
    }

    public double ContentOpacity => Online || IsMore ? 1.0 : 0.45;
    public double OnlineDotOpacity => Online ? 1.0 : 0.0;
    public Visibility OfflineVisibility => Online || IsMore ? Visibility.Collapsed : Visibility.Visible;
    public Visibility FavoriteVisibility => Favorite ? Visibility.Visible : Visibility.Collapsed;
    public string StarGlyph => Favorite ? "" : "";
    public double DropHighlightOpacity => DropHover ? 1.0 : 0.0;

    public static DeviceVm From(DeviceInfo d)
    {
        var vm = new DeviceVm { Id = d.Id };
        vm.Update(d);
        return vm;
    }

    public void Update(DeviceInfo d)
    {
        Name = d.Name;
        Kind = d.Kind;
        Glyph = Format.KindGlyph(d.Kind);
        Online = d.Online;
        Favorite = d.Favorite;
        Status = Format.DeviceStatus(d);
    }
}

/// <summary>A received file in the flyout list.</summary>
public sealed partial class ItemVm : Observable
{
    private string _name = "", _status = "", _path = "", _state = "";
    private double _progress;
    private ImageSource? _thumb;
    private bool _isPicture, _hover;

    public int Id { get; init; }
    public string Key => Id.ToString();
    public ItemVm Self => this;

    public string Name { get => _name; set => Set(ref _name, value); }
    public string Status { get => _status; set => Set(ref _status, value); }
    public string Path { get => _path; set => Set(ref _path, value); }

    public string State
    {
        get => _state;
        set
        {
            if (Set(ref _state, value))
            {
                Raise(nameof(ProgressVisibility));
                Raise(nameof(ProgressIndeterminate));
                Raise(nameof(SaveVisibility));
                Raise(nameof(OpenVisibility));
                Raise(nameof(RemoveVisibility));
                Raise(nameof(RemoveGlyph));
                Raise(nameof(StatusBrush));
            }
        }
    }

    public double Progress
    {
        get => _progress;
        set
        {
            if (Set(ref _progress, value)) Raise(nameof(ProgressIndeterminate));
        }
    }

    public ImageSource? Thumb
    {
        get => _thumb;
        set
        {
            if (Set(ref _thumb, value)) Raise(nameof(PlaceholderVisibility));
        }
    }

    public bool IsPicture
    {
        get => _isPicture;
        set
        {
            if (Set(ref _isPicture, value))
            {
                Raise(nameof(ThumbStretch));
                Raise(nameof(ThumbMargin));
            }
        }
    }

    public bool Hover
    {
        get => _hover;
        set
        {
            if (Set(ref _hover, value))
            {
                Raise(nameof(SaveVisibility));
                Raise(nameof(OpenVisibility));
                Raise(nameof(RemoveVisibility));
            }
        }
    }

    public Visibility ProgressVisibility => State is "receiving" or "landing" ? Visibility.Visible : Visibility.Collapsed;
    /// <summary>Finishing up, or a size tailscaled didn't announce.</summary>
    public bool ProgressIndeterminate => State == "landing" || (State == "receiving" && Progress <= 0);
    public Visibility SaveVisibility => Hover && State == "pending" ? Visibility.Visible : Visibility.Collapsed;
    public Visibility OpenVisibility => Hover && State is "pending" or "saved" ? Visibility.Visible : Visibility.Collapsed;
    public Visibility RemoveVisibility => Hover && State is not ("receiving" or "landing" or "saving") ? Visibility.Visible : Visibility.Collapsed;
    public string RemoveGlyph => State == "pending" ? "" : "";
    public Visibility PlaceholderVisibility => Thumb == null ? Visibility.Visible : Visibility.Collapsed;
    public Stretch ThumbStretch => IsPicture ? Stretch.UniformToFill : Stretch.Uniform;
    public Thickness ThumbMargin => IsPicture ? new Thickness(0) : new Thickness(6);

    public Brush StatusBrush => (Brush)Application.Current.Resources[
        State == "failed" ? "SystemFillColorCriticalBrush" : "TextFillColorSecondaryBrush"];
}
