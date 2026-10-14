using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using Microsoft.UI.Dispatching;

namespace TailDrop.UI.Core;

/// <summary>
/// The pipe to TailDrop.exe: newline-delimited JSON. Messages from the host
/// are applied to <see cref="Store"/> on the UI thread; commands go back
/// through the small helpers below.
/// </summary>
public static class HostLink
{
    private static NamedPipeClientStream? _pipe;
    private static readonly object WriteLock = new();
    private static DispatcherQueue? _ui;

    public static event Action? Disconnected;

    public static async Task<bool> ConnectAsync(string pipeName, DispatcherQueue ui)
    {
        _ui = ui;
        string name = pipeName.StartsWith(@"\\.\pipe\", StringComparison.OrdinalIgnoreCase) ? pipeName[9..] : pipeName;
        _pipe = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.Asynchronous);
        try
        {
            await _pipe.ConnectAsync(5000);
        }
        catch
        {
            return false;
        }
        _ = Task.Run(ReadLoopAsync);
        return true;
    }

    private static async Task ReadLoopAsync()
    {
        try
        {
            using var reader = new StreamReader(_pipe!, new UTF8Encoding(false), false, 64 * 1024, leaveOpen: true);
            string? line;
            while ((line = await reader.ReadLineAsync()) != null)
            {
                string msg = line;
                _ui?.TryEnqueue(() => Store.Apply(msg));
            }
        }
        catch
        {
        }
        _ui?.TryEnqueue(() => Disconnected?.Invoke());
    }

    private static void Send(Action<Utf8JsonWriter> body)
    {
        if (_pipe is not { IsConnected: true }) return;
        var buffer = new MemoryStream();
        using (var w = new Utf8JsonWriter(buffer))
        {
            w.WriteStartObject();
            body(w);
            w.WriteEndObject();
        }
        buffer.WriteByte((byte)'\n');
        lock (WriteLock)
        {
            try
            {
                _pipe.Write(buffer.GetBuffer(), 0, (int)buffer.Length);
                _pipe.Flush();
            }
            catch
            {
            }
        }
    }

    private static void Cmd(string c) => Send(w => w.WriteString("c", c));

    private static void Cmd(string c, string key, int value) => Send(w =>
    {
        w.WriteString("c", c);
        w.WriteNumber(key, value);
    });

    public static void Save(int id, string dir = "") => Send(w =>
    {
        w.WriteString("c", "save");
        w.WriteNumber("id", id);
        w.WriteString("dir", dir);
    });

    public static void SaveBatch(int batch, string dir = "") => Send(w =>
    {
        w.WriteString("c", "saveBatch");
        w.WriteNumber("batch", batch);
        w.WriteString("dir", dir);
    });

    public static void Open(int id) => Cmd("open", "id", id);
    public static void Reveal(int id) => Cmd("reveal", "id", id);
    public static void Discard(int id) => Cmd("discard", "id", id);
    public static void Forget(int id) => Cmd("forget", "id", id);
    public static void DragDone() => Cmd("dragDone");
    public static void Cancel(int job) => Cmd("cancel", "job", job);
    public static void Retry(int job) => Cmd("retry", "job", job);
    public static void DismissJob(int job) => Cmd("dismissJob", "job", job);
    public static void ToastClosed(int batch) => Cmd("toastClosed", "batch", batch);
    public static void Refresh() => Cmd("refresh");
    public static void OpenTailscale() => Cmd("openTailscale");
    public static void OpenInbox() => Cmd("openInbox");

    public static void SendFiles(string device, IEnumerable<string> paths) => Send(w =>
    {
        w.WriteString("c", "send");
        w.WriteString("device", device);
        w.WriteStartArray("paths");
        foreach (string p in paths) w.WriteStringValue(p);
        w.WriteEndArray();
    });

    public static void SetFavorite(string device, bool on) => Send(w =>
    {
        w.WriteString("c", "favorite");
        w.WriteString("device", device);
        w.WriteBoolean("on", on);
    });

    /// <summary>kind is a key ("pc", "laptop", ...) or "" for automatic.</summary>
    public static void SetKind(string device, string kind) => Send(w =>
    {
        w.WriteString("c", "kind");
        w.WriteString("device", device);
        w.WriteString("kind", kind);
    });

    public static void SetNickname(string device, string name) => Send(w =>
    {
        w.WriteString("c", "nickname");
        w.WriteString("device", device);
        w.WriteString("name", name);
    });

    public static void SetSetting(string key, bool value) => Send(w =>
    {
        w.WriteString("c", "set");
        w.WriteString("key", key);
        w.WriteBoolean("value", value);
    });

    public static void SetSetting(string key, string value) => Send(w =>
    {
        w.WriteString("c", "set");
        w.WriteString("key", key);
        w.WriteString("value", value);
    });
}
