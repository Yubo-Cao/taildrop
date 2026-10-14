using Windows.Storage;
using Windows.Storage.FileProperties;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;

namespace TailDrop.UI.Core;

public static class Format
{
    public static string Size(long bytes)
    {
        if (bytes < 0) return "";
        if (bytes < 1000) return Str.F("SizeBytes", bytes);
        string[] units = { "KB", "MB", "GB", "TB" };
        double v = bytes / 1024.0;
        int u = 0;
        while (v >= 1000 && u < units.Length - 1)
        {
            v /= 1024;
            u++;
        }
        return (v < 10 ? v.ToString("0.0") : v.ToString("0")) + " " + units[u];
    }

    public static string Rate(double bytesPerSecond) => bytesPerSecond > 0 ? Size((long)bytesPerSecond) + "/s" : "";

    public static readonly string[] Kinds = { "pc", "laptop", "phone", "tablet", "server", "other" };

    public static string KindGlyph(string kind) => kind switch
    {
        "pc" => "",
        "laptop" => "",
        "phone" => "",
        "tablet" => "",
        "server" => "",
        _ => "",
    };

    public static string KindLabel(string kind) => Str.Get("Kind_" + kind);

    public static string OsLabel(string os) => os.ToLowerInvariant() switch
    {
        "windows" => "Windows",
        "macos" => "macOS",
        "ios" => "iOS",
        "android" => "Android",
        "linux" => "Linux",
        "freebsd" => "FreeBSD",
        _ => os,
    };

    public static string DeviceStatus(DeviceInfo d) =>
        (d.Online ? Str.Get("Online") : Str.Get("Offline")) + (d.Os.Length > 0 ? " · " + OsLabel(d.Os) : "");

    public static string FileName(string path) => System.IO.Path.GetFileName(path);

    private static readonly Dictionary<string, string> FolderNames = new(StringComparer.OrdinalIgnoreCase);

    /// <summary>Explorer's display name for a folder ("下载" for Downloads), cached.</summary>
    public static async Task<string> FolderNameAsync(string path)
    {
        if (string.IsNullOrEmpty(path)) return "";
        if (FolderNames.TryGetValue(path, out var cached)) return cached;
        string name = System.IO.Path.GetFileName(path.TrimEnd('\\'));
        try
        {
            var folder = await StorageFolder.GetFolderFromPathAsync(path);
            if (!string.IsNullOrEmpty(folder.DisplayName)) name = folder.DisplayName;
        }
        catch
        {
        }
        FolderNames[path] = name;
        return name;
    }

    public static string FolderNameOrPath(string path) =>
        FolderNames.TryGetValue(path, out var n) ? n : System.IO.Path.GetFileName(path.TrimEnd('\\'));
}

/// <summary>Explorer thumbnails (photos, video frames, document previews or the file icon).</summary>
public static class Thumbs
{
    public sealed record Thumb(ImageSource Source, bool IsPicture, double Aspect);

    private static readonly Dictionary<string, Thumb> Cache = new(StringComparer.OrdinalIgnoreCase);

    public static async Task<Thumb?> GetAsync(string path, uint size)
    {
        if (string.IsNullOrEmpty(path)) return null;
        string key = path + "|" + size;
        if (Cache.TryGetValue(key, out var t)) return t;
        try
        {
            var file = await StorageFile.GetFileFromPathAsync(path);
            using var thumb = await file.GetThumbnailAsync(ThumbnailMode.SingleItem, size, ThumbnailOptions.ResizeThumbnail);
            if (thumb == null) return null;
            var bmp = new BitmapImage();
            await bmp.SetSourceAsync(thumb);
            double aspect = thumb.OriginalHeight > 0 ? (double)thumb.OriginalWidth / thumb.OriginalHeight : 1;
            t = new Thumb(bmp, thumb.Type == ThumbnailType.Image, aspect);
            if (Cache.Count > 64) Cache.Clear();
            Cache[key] = t;
            return t;
        }
        catch
        {
            return null;
        }
    }
}
