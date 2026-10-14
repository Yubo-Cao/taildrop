using Microsoft.Windows.ApplicationModel.Resources;

namespace TailDrop.UI.Core;

/// <summary>Localized strings from Strings/&lt;lang&gt;/Resources.resw.</summary>
public static class Str
{
    private static readonly ResourceLoader Loader = new();

    public static string Get(string key)
    {
        try
        {
            string s = Loader.GetString(key);
            return string.IsNullOrEmpty(s) ? key : s;
        }
        catch
        {
            return key;
        }
    }

    /// <summary>Get(key) with {0}, {1}... replaced.</summary>
    public static string F(string key, params object[] args) => string.Format(Get(key), args);
}
