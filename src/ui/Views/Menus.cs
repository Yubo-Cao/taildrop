using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.Windows.Storage.Pickers;
using TailDrop.UI.Core;

namespace TailDrop.UI.Views;

/// <summary>Right-click menu for a device: send, favorite, icon, details.</summary>
public static class DeviceMenu
{
    public static MenuFlyout Create(string deviceId, Action? beforeLeaving = null)
    {
        var menu = new MenuFlyout();
        var d = Store.Device(deviceId);
        if (d == null) return menu;

        var send = new MenuFlyoutItem { Text = Str.Get("SendFiles"), Icon = new FontIcon { Glyph = "" }, IsEnabled = d.Online };
        send.Click += async (_, _) =>
        {
            beforeLeaving?.Invoke();
            await App.Instance.PickFilesAsync(deviceId);
        };
        menu.Items.Add(send);

        var paste = new MenuFlyoutItem
        {
            Text = Str.Get("PasteAndSend"),
            Icon = new FontIcon { Glyph = "" },
            KeyboardAcceleratorTextOverride = "Ctrl+V",
            IsEnabled = d.Online && App.CanPaste(),
        };
        paste.Click += async (_, _) =>
        {
            beforeLeaving?.Invoke();
            await App.Instance.PasteAsync(deviceId);
        };
        menu.Items.Add(paste);

        var fav = new MenuFlyoutItem
        {
            Text = Str.Get(d.Favorite ? "RemoveFavorite" : "AddFavorite"),
            Icon = new FontIcon { Glyph = d.Favorite ? "" : "" },
        };
        fav.Click += (_, _) => HostLink.SetFavorite(deviceId, !d.Favorite);
        menu.Items.Add(fav);

        menu.Items.Add(IconSubMenu(d));
        menu.Items.Add(new MenuFlyoutSeparator());

        var details = new MenuFlyoutItem { Text = Str.Get("DeviceSettings"), Icon = new FontIcon { Glyph = "" } };
        details.Click += (_, _) =>
        {
            beforeLeaving?.Invoke();
            App.Instance.OpenSettings(deviceId);
        };
        menu.Items.Add(details);
        return menu;
    }

    public static MenuFlyoutSubItem IconSubMenu(DeviceInfo d)
    {
        var sub = new MenuFlyoutSubItem { Text = Str.Get("Icon"), Icon = new FontIcon { Glyph = Format.KindGlyph(d.Kind) } };
        var auto = new RadioMenuFlyoutItem
        {
            Text = Str.F("Automatic", Format.KindLabel(d.AutoKind)),
            GroupName = "kind",
            IsChecked = !d.Custom,
        };
        string id = d.Id;
        auto.Click += (_, _) => HostLink.SetKind(id, "");
        sub.Items.Add(auto);
        sub.Items.Add(new MenuFlyoutSeparator());
        foreach (string kind in Format.Kinds)
        {
            var item = new RadioMenuFlyoutItem
            {
                Text = Format.KindLabel(kind),
                Icon = new FontIcon { Glyph = Format.KindGlyph(kind) },
                GroupName = "kind",
                IsChecked = d.Custom && d.Kind == kind,
            };
            string k = kind;
            item.Click += (_, _) => HostLink.SetKind(id, k);
            sub.Items.Add(item);
        }
        return sub;
    }
}

/// <summary>Right-click menu for a received file.</summary>
public static class ItemMenu
{
    public static MenuFlyout Create(int itemId, Window owner)
    {
        var menu = new MenuFlyout();
        var i = Store.Item(itemId);
        if (i == null) return menu;
        bool pending = i.State == "pending";
        if (pending)
        {
            var save = new MenuFlyoutItem
            {
                Text = Str.F("SaveToFolder", Format.FolderNameOrPath(Store.Settings.EffectiveDir)),
                Icon = new FontIcon { Glyph = "" },
            };
            save.Click += (_, _) => HostLink.Save(itemId);
            menu.Items.Add(save);
            var saveTo = new MenuFlyoutItem { Text = Str.Get("SaveTo"), Icon = new FontIcon { Glyph = "" } };
            saveTo.Click += async (_, _) =>
            {
                string? dir = await App.PickFolderAsync(owner);
                if (dir != null) HostLink.Save(itemId, dir);
            };
            menu.Items.Add(saveTo);
            menu.Items.Add(new MenuFlyoutSeparator());
        }
        if (i.HasFile)
        {
            var copy = new MenuFlyoutItem { Text = Str.Get("Copy"), Icon = new FontIcon { Glyph = "" }, KeyboardAcceleratorTextOverride = "Ctrl+C" };
            copy.Click += (_, _) => ShellData.Copy(new[] { i.Path });
            menu.Items.Add(copy);
            var open = new MenuFlyoutItem { Text = Str.Get("Open"), Icon = new FontIcon { Glyph = "" } };
            open.Click += (_, _) => HostLink.Open(itemId);
            menu.Items.Add(open);
            var reveal = new MenuFlyoutItem { Text = Str.Get("ShowInFolder"), Icon = new FontIcon { Glyph = "" } };
            reveal.Click += (_, _) => HostLink.Reveal(itemId);
            menu.Items.Add(reveal);
            menu.Items.Add(new MenuFlyoutSeparator());
        }
        var remove = new MenuFlyoutItem
        {
            Text = Str.Get(pending ? "Delete" : "RemoveFromList"),
            Icon = new FontIcon { Glyph = pending ? "" : "" },
        };
        remove.Click += (_, _) =>
        {
            if (pending) HostLink.Discard(itemId);
            else HostLink.Forget(itemId);
        };
        menu.Items.Add(remove);
        return menu;
    }
}
