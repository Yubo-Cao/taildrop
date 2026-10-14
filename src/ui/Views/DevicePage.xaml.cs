using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Navigation;
using TailDrop.UI.Core;
using Windows.System;

namespace TailDrop.UI.Views;

/// <summary>One device: its name and icon in TailDrop, favorite, what Tailscale reports.</summary>
public sealed partial class DevicePage : Page
{
    private const string AutoKind = "auto";

    private bool _updating;
    // The "Automatic (Tablet)" choice. Kept here because a selected ComboBoxItem
    // lends its content to the closed ComboBox, so it can't be found through the item.
    private readonly TextBlock _autoText = new() { VerticalAlignment = VerticalAlignment.Center };
    private readonly FontIcon _autoIcon = new() { FontSize = 16 };

    public string DeviceId { get; private set; } = "";

    public DevicePage()
    {
        InitializeComponent();
        SendLabel.Text = Str.Get("SendFiles");
        PersonalizeHeader.Text = Str.Get("Section_Personalize");
        NameCard.Header = Str.Get("Name_Header");
        IconCard.Header = Str.Get("Icon_Header");
        IconCard.Description = Str.Get("Icon_Description");
        FavoriteCard.Header = Str.Get("Favorite_Header");
        FavoriteCard.Description = Str.Get("Favorite_Description");
        DetailsHeader.Text = Str.Get("Section_Details");
        HostLabel.Text = Str.Get("Detail_Host");
        DnsLabel.Text = Str.Get("Detail_Dns");
        IpLabel.Text = Str.Get("Detail_Ip");
        OsLabel.Text = Str.Get("Detail_Os");
        GoneBar.Title = Str.Get("Device_GoneTitle");
        GoneBar.Message = Str.Get("Device_GoneMessage");
        ToolTipService.SetToolTip(NameBox, Str.Get("Name_Tooltip"));

        var autoRow = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        autoRow.Children.Add(_autoIcon);
        autoRow.Children.Add(_autoText);
        KindBox.Items.Add(new ComboBoxItem { Content = autoRow, Tag = AutoKind });
        foreach (string kind in Format.Kinds) KindBox.Items.Add(KindItem(kind));
        Unloaded += (_, _) => Store.DevicesChanged -= Refresh;
    }

    private static ComboBoxItem KindItem(string kind)
    {
        var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 12 };
        row.Children.Add(new FontIcon { Glyph = Format.KindGlyph(kind), FontSize = 16 });
        row.Children.Add(new TextBlock { Text = Format.KindLabel(kind), VerticalAlignment = VerticalAlignment.Center });
        return new ComboBoxItem { Content = row, Tag = kind };
    }

    protected override void OnNavigatedTo(NavigationEventArgs e)
    {
        base.OnNavigatedTo(e);
        DeviceId = e.Parameter as string ?? "";
        Store.DevicesChanged -= Refresh;
        Store.DevicesChanged += Refresh;
        Refresh();
    }

    protected override void OnNavigatedFrom(NavigationEventArgs e)
    {
        base.OnNavigatedFrom(e);
        CommitName();
        Store.DevicesChanged -= Refresh;
    }

    private void Refresh()
    {
        var d = Store.Device(DeviceId);
        _updating = true;
        GoneBar.IsOpen = d == null;
        NameBox.IsEnabled = KindBox.IsEnabled = FavoriteToggle.IsEnabled = d != null;
        if (d == null)
        {
            SendButton.IsEnabled = false;
            HeroDot.Fill = (Brush)Application.Current.Resources["SystemFillColorNeutralBrush"];
            HeroStatus.Text = Str.Get("Offline");
            _updating = false;
            return;
        }

        HeroGlyph.Glyph = Format.KindGlyph(d.Kind);
        HeroName.Text = d.Name;
        HeroDot.Fill = (Brush)Application.Current.Resources[d.Online ? "SystemFillColorSuccessBrush" : "SystemFillColorNeutralBrush"];
        HeroStatus.Text = Format.DeviceStatus(d);
        SendButton.IsEnabled = d.Online && Store.Connected;

        NameCard.Description = Str.F("Name_Description", d.HostName);
        NameBox.PlaceholderText = d.HostName;
        if (NameBox.FocusState == FocusState.Unfocused) NameBox.Text = d.Nickname;

        IconCard.Glyph = Format.KindGlyph(d.Kind);
        _autoText.Text = Str.F("Automatic", Format.KindLabel(d.AutoKind));
        _autoIcon.Glyph = Format.KindGlyph(d.AutoKind);
        string want = d.Custom ? d.Kind : AutoKind;
        KindBox.SelectedItem = KindBox.Items.OfType<ComboBoxItem>().FirstOrDefault(i => i.Tag as string == want);

        FavoriteToggle.IsOn = d.Favorite;
        FavoriteState.Text = Str.Get(d.Favorite ? "On" : "Off");

        HostValue.Text = d.HostName;
        DnsValue.Text = d.Dns.TrimEnd('.');
        IpValue.Text = d.Ip;
        OsValue.Text = Format.OsLabel(d.Os);
        _updating = false;
    }

    private void CommitName()
    {
        var d = Store.Device(DeviceId);
        if (d == null) return;
        string name = NameBox.Text.Trim();
        if (name == d.HostName) name = "";
        if (name != d.Nickname) HostLink.SetNickname(DeviceId, name);
    }

    private void OnNameKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.Enter)
        {
            CommitName();
            e.Handled = true;
        }
        else if (e.Key == VirtualKey.Escape)
        {
            NameBox.Text = Store.Device(DeviceId)?.Nickname ?? "";
            e.Handled = true;
        }
    }

    private void OnNameLostFocus(object sender, RoutedEventArgs e) => CommitName();

    private void OnKindChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_updating || KindBox.SelectedItem is not ComboBoxItem { Tag: string kind }) return;
        HostLink.SetKind(DeviceId, kind == AutoKind ? "" : kind);
    }

    private void OnFavoriteToggled(object sender, RoutedEventArgs e)
    {
        FavoriteState.Text = Str.Get(FavoriteToggle.IsOn ? "On" : "Off");
        if (!_updating) HostLink.SetFavorite(DeviceId, FavoriteToggle.IsOn);
    }

    private async void OnSend(object sender, RoutedEventArgs e) => await App.Instance.PickFilesAsync(DeviceId, App.Instance.SettingsHost);
}
