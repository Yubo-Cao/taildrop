using Windows.ApplicationModel.DataTransfer;
using Windows.Graphics.Imaging;
using Windows.Storage;
using System.Runtime.InteropServices.WindowsRuntime;
using Windows.Storage.Streams;

namespace TailDrop.UI.Core;

/// <summary>
/// Turns what is dropped or pasted into files to send: files as they are; files
/// that exist only in the other app (an attachment, an image dragged out of a
/// browser) copied out first; a bitmap (a screenshot, "Copy image") as a PNG; text
/// as a .txt. Copies go to a staging folder that is cleared after a day.
/// </summary>
public static class Incoming
{
    private static string StagingRoot => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YuboCao", "TailDrop", "Outgoing");

    // A file that only exists inside another app (a mail attachment, a browser download).
    private const string VirtualNames = "FileGroupDescriptorW", VirtualContents = "FileContents";

    public static bool CanAccept(DataPackageView view) =>
        view.Contains(StandardDataFormats.StorageItems) || view.Contains(StandardDataFormats.Bitmap) ||
        view.Contains(StandardDataFormats.Text) || (view.Contains(VirtualNames) && view.Contains(VirtualContents));

    public static async Task<List<string>> PathsAsync(DataPackageView view)
    {
        var paths = new List<string>();
        string? stage = null;
        string Stage()
        {
            stage ??= Directory.CreateDirectory(Path.Combine(StagingRoot, DateTime.Now.ToString("yyyyMMdd-HHmmss-fff"))).FullName;
            return stage;
        }
        try
        {
            if (view.Contains(StandardDataFormats.StorageItems))
            {
                try
                {
                    foreach (var item in await view.GetStorageItemsAsync())
                    {
                        if (!string.IsNullOrEmpty(item.Path) && (File.Exists(item.Path) || Directory.Exists(item.Path)))
                            paths.Add(item.Path);
                        else if (item is StorageFile f)
                            paths.Add(await CopyOutAsync(f, Stage()));
                    }
                }
                catch (Exception e)
                {
                    Log.Write("storage items: " + e.Message);
                }
            }
            if (paths.Count == 0 && view.Contains(VirtualNames) && view.Contains(VirtualContents))
                if (await ReadVirtualAsync(view, Stage()) is { } v) paths.Add(v);
            // Office copies text with a picture of it; the text is what was meant.
            bool text = view.Contains(StandardDataFormats.Text);
            if (paths.Count == 0 && view.Contains(StandardDataFormats.Bitmap) && !text)
                paths.Add(await SavePngAsync(await view.GetBitmapAsync(), Path.Combine(Stage(), Str.F("PastedImageName", Stamp()) + ".png")));
            if (paths.Count == 0 && text)
            {
                string content = await view.GetTextAsync();
                if (!string.IsNullOrEmpty(content))
                {
                    string file = Path.Combine(Stage(), Str.F("PastedTextName", Stamp()) + ".txt");
                    await File.WriteAllTextAsync(file, content, new System.Text.UTF8Encoding(true));
                    paths.Add(file);
                }
            }
            if (paths.Count == 0 && view.Contains(StandardDataFormats.Bitmap))
                paths.Add(await SavePngAsync(await view.GetBitmapAsync(), Path.Combine(Stage(), Str.F("PastedImageName", Stamp()) + ".png")));
        }
        catch (Exception e)
        {
            Log.Write("incoming: " + e);
        }
        return paths;
    }

    /// <summary>
    /// The first file of a FILEGROUPDESCRIPTORW / FileContents pair, read directly when
    /// WinRT can't turn it into a StorageFile; null if that didn't work either.
    /// </summary>
    private static async Task<string?> ReadVirtualAsync(DataPackageView view, string dir)
    {
        try
        {
            byte[] fgd = await ReadBytesAsync(await view.GetDataAsync(VirtualNames));
            const int nameAt = 4 + 72, nameBytes = 260 * 2;  // cItems, then FILEDESCRIPTORW.cFileName
            if (fgd.Length < nameAt + 2 || BitConverter.ToInt32(fgd, 0) < 1) return null;
            string name = System.Text.Encoding.Unicode.GetString(fgd, nameAt, Math.Min(nameBytes, fgd.Length - nameAt));
            name = Path.GetFileName(name.Split('\0')[0].Replace('/', '\\'));
            name = string.Join("_", name.Split(Path.GetInvalidFileNameChars()));
            if (name.Length == 0) name = Str.F("PastedFileName", Stamp());
            byte[] content = await ReadBytesAsync(await view.GetDataAsync(VirtualContents));
            string file = Path.Combine(dir, name);
            await File.WriteAllBytesAsync(file, content);
            return file;
        }
        catch (Exception e)
        {
            Log.Write("virtual file: " + e.Message);
            return null;
        }
    }

    private static async Task<byte[]> ReadBytesAsync(object data)
    {
        switch (data)
        {
            case IRandomAccessStream ras:
                using (var s = ras.AsStreamForRead())
                using (var m = new MemoryStream())
                {
                    await s.CopyToAsync(m);
                    return m.ToArray();
                }
            case IBuffer buffer:
                return buffer.ToArray();
            case byte[] bytes:
                return bytes;
            default:
                throw new InvalidDataException("unexpected " + data?.GetType().Name);
        }
    }

    private static string Stamp() => DateTime.Now.ToString("yyyy-MM-dd HHmmss");

    private static async Task<string> CopyOutAsync(StorageFile file, string dir)
    {
        string name = string.Join("_", file.Name.Split(Path.GetInvalidFileNameChars()));
        if (name.Length == 0) name = Str.F("PastedFileName", Stamp());
        // Make sure the type survives: a name without an extension gets one from the content type.
        if (Path.GetExtension(name).Length == 0 && ExtensionFor(file.ContentType) is { } ext) name += ext;
        var folder = await StorageFolder.GetFolderFromPathAsync(dir);
        var copy = await file.CopyAsync(folder, name, NameCollisionOption.GenerateUniqueName);
        return copy.Path;
    }

    private static string? ExtensionFor(string? contentType) => contentType?.ToLowerInvariant() switch
    {
        "image/png" => ".png",
        "image/jpeg" or "image/jpg" => ".jpg",
        "image/gif" => ".gif",
        "image/webp" => ".webp",
        "image/heic" => ".heic",
        "image/bmp" => ".bmp",
        "image/svg+xml" => ".svg",
        "application/pdf" => ".pdf",
        "text/plain" => ".txt",
        "text/html" => ".html",
        "application/zip" => ".zip",
        "video/mp4" => ".mp4",
        "video/quicktime" => ".mov",
        "audio/mpeg" => ".mp3",
        _ => null,
    };

    private static async Task<string> SavePngAsync(RandomAccessStreamReference bitmap, string file)
    {
        using var source = await bitmap.OpenReadAsync();
        var decoder = await BitmapDecoder.CreateAsync(source);
        using var pixels = await decoder.GetSoftwareBitmapAsync(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied);
        await using (var output = File.Create(file))
        {
            var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, output.AsRandomAccessStream());
            encoder.SetSoftwareBitmap(pixels);
            await encoder.FlushAsync();
        }
        return file;
    }

    /// <summary>Removes staged copies from earlier days; sends from them have long finished.</summary>
    public static void CleanUp()
    {
        try
        {
            if (!Directory.Exists(StagingRoot)) return;
            foreach (var dir in new DirectoryInfo(StagingRoot).GetDirectories())
                if (DateTime.Now - dir.CreationTime > TimeSpan.FromDays(1))
                    try
                    {
                        dir.Delete(true);
                    }
                    catch
                    {
                    }
        }
        catch
        {
        }
    }
}
