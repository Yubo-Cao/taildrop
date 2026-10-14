namespace TailDrop.UI.Core;

/// <summary>%TEMP%\TailDropUI.log, written only when TAILDROP_LOG is set (like the host's log).</summary>
public static class Log
{
    private static readonly bool Enabled = Environment.GetEnvironmentVariable("TAILDROP_LOG") != null;
    private static readonly object Lock = new();

    public static void Write(string message)
    {
        if (!Enabled) return;
        lock (Lock)
        {
            try
            {
                File.AppendAllText(Path.Combine(Path.GetTempPath(), "TailDropUI.log"),
                    DateTime.Now.ToString("HH:mm:ss.fff ") + message + Environment.NewLine);
            }
            catch
            {
            }
        }
    }
}
