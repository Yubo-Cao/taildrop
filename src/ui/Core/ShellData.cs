using System.Runtime.InteropServices;

namespace TailDrop.UI.Core;

/// <summary>
/// Files put on the clipboard the way Explorer's Copy does it: the Shell's own data
/// object (paths, shell item IDs, file names), left behind when this process exits.
/// </summary>
internal static partial class ShellData
{
    private static readonly Guid IID_IDataObject = new("0000010e-0000-0000-C000-000000000046");
    private static bool _oleReady;

    [LibraryImport("shell32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int SHParseDisplayName(string name, nint bindCtx, out nint pidl, uint sfgaoIn, out uint sfgaoOut);

    [LibraryImport("shell32.dll")]
    private static partial int SHCreateDataObject(nint pidlFolder, uint cidl, nint[] apidl, nint inner, in Guid riid, out nint obj);

    [LibraryImport("shell32.dll")]
    private static partial nint ILFindLastID(nint pidl);

    [LibraryImport("ole32.dll")]
    private static partial int OleInitialize(nint reserved);

    [LibraryImport("ole32.dll")]
    private static partial int OleSetClipboard(nint data);

    [LibraryImport("ole32.dll")]
    private static partial int OleFlushClipboard();

    /// <summary>A Shell data object for the files; 0 if none of them could be found.</summary>
    private static nint Create(IReadOnlyList<string> paths)
    {
        var abs = new List<nint>();
        nint folder = 0;
        try
        {
            foreach (string p in paths)
                if (SHParseDisplayName(p, 0, out nint pidl, 0, out _) >= 0) abs.Add(pidl);
            if (abs.Count == 0) return 0;
            // Usually all in one folder: the folder's ID plus each file's last ID, as
            // Explorer builds it. Otherwise full IDs under the desktop.
            var dirs = paths.Select(p => Path.GetDirectoryName(p) ?? "").Distinct(StringComparer.OrdinalIgnoreCase).ToList();
            nint[] items;
            if (dirs.Count == 1 && abs.Count == paths.Count && SHParseDisplayName(dirs[0], 0, out folder, 0, out _) >= 0)
                items = abs.Select(ILFindLastID).ToArray();
            else
                items = abs.ToArray();
            return SHCreateDataObject(folder, (uint)items.Length, items, 0, IID_IDataObject, out nint obj) >= 0 ? obj : 0;
        }
        finally
        {
            foreach (nint pidl in abs) Marshal.FreeCoTaskMem(pidl);
            if (folder != 0) Marshal.FreeCoTaskMem(folder);
        }
    }

    /// <summary>Puts the files on the clipboard as Explorer's Copy does; false if that failed.</summary>
    public static bool Copy(IReadOnlyList<string> paths)
    {
        if (!_oleReady) _oleReady = OleInitialize(0) >= 0;
        nint obj = Create(paths);
        if (obj == 0) return false;
        try
        {
            if (OleSetClipboard(obj) < 0) return false;
            // Render it now, so the data stays on the clipboard after this process exits.
            OleFlushClipboard();
            return true;
        }
        finally
        {
            Marshal.Release(obj);
        }
    }
}
