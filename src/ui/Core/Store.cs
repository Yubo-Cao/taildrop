using System.Text.Json;

namespace TailDrop.UI.Core;

/// <summary>The host's state as last reported, plus change events.</summary>
public static class Store
{
    public static string Conn { get; private set; } = "unknown";
    public static string Self { get; private set; } = "";
    public static string Tailnet { get; private set; } = "";
    public static SettingsInfo Settings { get; private set; } = new();
    public static List<DeviceInfo> Devices { get; private set; } = new();
    public static List<ItemInfo> Items { get; private set; } = new();
    public static List<JobInfo> Jobs { get; private set; } = new();

    public static bool Connected => Conn == "running";

    public static event Action? StateChanged;
    public static event Action? SettingsChanged;
    public static event Action? DevicesChanged;
    public static event Action? ItemsChanged;
    public static event Action? JobsChanged;
    public static event Action<ShowMsg>? ShowRequested;
    public static event Action<NotifyMsg>? NotifyRequested;
    public static event Action? DragEnded;

    public static DeviceInfo? Device(string id) => Devices.Find(d => d.Id == id);
    public static ItemInfo? Item(int id) => Items.Find(i => i.Id == id);
    public static JobInfo? Job(int id) => Jobs.Find(j => j.Id == id);
    public static List<ItemInfo> Batch(int batch) => Items.FindAll(i => i.Batch == batch);

    public static void Apply(string line)
    {
        JsonElement root;
        try
        {
            using var doc = JsonDocument.Parse(line);
            root = doc.RootElement.Clone();
        }
        catch (JsonException)
        {
            return;
        }
        string t = root.TryGetProperty("t", out var tp) ? tp.GetString() ?? "" : "";
        var ctx = MsgContext.Default;
        switch (t)
        {
            case "state":
                var s = root.Deserialize(ctx.StateMsg)!;
                Conn = s.Conn;
                Self = s.Self;
                Tailnet = s.Tailnet;
                StateChanged?.Invoke();
                break;
            case "settings":
                Settings = root.Deserialize(ctx.SettingsInfo)!;
                SettingsChanged?.Invoke();
                break;
            case "devices":
                Devices = root.Deserialize(ctx.DevicesMsg)!.List;
                DevicesChanged?.Invoke();
                break;
            case "items":
                Items = root.Deserialize(ctx.ItemsMsg)!.List;
                ItemsChanged?.Invoke();
                break;
            case "jobs":
                Jobs = root.Deserialize(ctx.JobsMsg)!.List;
                JobsChanged?.Invoke();
                break;
            case "show":
                ShowRequested?.Invoke(root.Deserialize(ctx.ShowMsg)!);
                break;
            case "notify":
                NotifyRequested?.Invoke(root.Deserialize(ctx.NotifyMsg)!);
                break;
            case "dragEnd":
                DragEnded?.Invoke();
                break;
        }
    }
}
