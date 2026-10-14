using System.Text.Json.Serialization;

namespace TailDrop.UI.Core;

// Messages from TailDrop.exe (see src/app/app.cpp, *Json methods).

public sealed class StateMsg
{
    public string Conn { get; set; } = "unknown";
    public string Self { get; set; } = "";
    public string Tailnet { get; set; } = "";
}

public sealed class SettingsInfo
{
    public string DownloadDir { get; set; } = "";
    public string EffectiveDir { get; set; } = "";
    public string Downloads { get; set; } = "";
    public bool AutoSave { get; set; }
    public bool TrayDrop { get; set; } = true;
    public bool RunAtLogin { get; set; } = true;
    public bool Win11Menu { get; set; }
    public string Inbox { get; set; } = "";
    public string Version { get; set; } = "";
}

public sealed class DeviceInfo
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string HostName { get; set; } = "";
    public string Nickname { get; set; } = "";
    public string Dns { get; set; } = "";
    public string Ip { get; set; } = "";
    public string Os { get; set; } = "";
    public string Kind { get; set; } = "other";
    public string AutoKind { get; set; } = "other";
    public bool Custom { get; set; }
    public bool Online { get; set; }
    public bool Favorite { get; set; }
}

public sealed class ItemInfo
{
    public int Id { get; set; }
    public int Batch { get; set; }
    public string Name { get; set; } = "";
    public string Path { get; set; } = "";
    public long Size { get; set; }
    public long Received { get; set; }
    public string State { get; set; } = "";
    public string Note { get; set; } = "";

    public bool IsWaiting => State is "receiving" or "landing";
    public bool IsPending => State is "pending" or "saving";
    public bool HasFile => State is "pending" or "saved" && Path.Length > 0;
}

public sealed class JobInfo
{
    public int Id { get; set; }
    public string Device { get; set; } = "";
    public string DeviceName { get; set; } = "";
    public string Kind { get; set; } = "other";
    public string State { get; set; } = "";
    public long Total { get; set; }
    public long Sent { get; set; }
    public int FileIndex { get; set; }
    public int FileCount { get; set; }
    public string Current { get; set; } = "";
    public string Error { get; set; } = "";
    public string ErrorDetail { get; set; } = "";
    public double Rate { get; set; }
    public string First { get; set; } = "";

    public bool IsRunning => State is "preparing" or "sending";
}

public sealed class DevicesMsg { public List<DeviceInfo> List { get; set; } = new(); }
public sealed class ItemsMsg { public List<ItemInfo> List { get; set; } = new(); }
public sealed class JobsMsg { public List<JobInfo> List { get; set; } = new(); }

public sealed class ShowMsg
{
    public string What { get; set; } = "";
    public bool DropMode { get; set; }
    public int[]? Tray { get; set; }
    public List<string>? Paths { get; set; }
    public string? Device { get; set; }
}

public sealed class NotifyMsg
{
    public int Batch { get; set; }
    public int Job { get; set; }
    public int[]? Tray { get; set; }
}

[JsonSourceGenerationOptions(PropertyNamingPolicy = JsonKnownNamingPolicy.CamelCase)]
[JsonSerializable(typeof(StateMsg))]
[JsonSerializable(typeof(SettingsInfo))]
[JsonSerializable(typeof(DevicesMsg))]
[JsonSerializable(typeof(ItemsMsg))]
[JsonSerializable(typeof(JobsMsg))]
[JsonSerializable(typeof(ShowMsg))]
[JsonSerializable(typeof(NotifyMsg))]
internal sealed partial class MsgContext : JsonSerializerContext
{
}
