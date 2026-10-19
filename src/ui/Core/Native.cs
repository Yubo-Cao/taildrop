using System.Runtime.InteropServices;

namespace TailDrop.UI.Core;

internal static partial class Native
{
    public const int GWL_EXSTYLE = -20;
    public const long WS_EX_TOOLWINDOW = 0x80;
    public const long WS_EX_NOACTIVATE = 0x08000000;
    public const long WS_EX_TOPMOST = 0x8;
    public const int SW_SHOWNOACTIVATE = 4;
    public const int DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    public const int DWMWCP_ROUND = 2;

    [LibraryImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    public static partial nint GetWindowLongPtr(nint hwnd, int index);

    [LibraryImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    public static partial nint SetWindowLongPtr(nint hwnd, int index, nint value);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool ShowWindow(nint hwnd, int cmd);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetForegroundWindow(nint hwnd);

    [LibraryImport("user32.dll")]
    public static partial uint GetDpiForWindow(nint hwnd);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool GetCursorPos(out POINT pt);

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool IsWindowVisible(nint hwnd);

    public static readonly nint HWND_TOPMOST = -1;
    public const uint SWP_NOSIZE = 0x1, SWP_NOMOVE = 0x2, SWP_NOACTIVATE = 0x10;

    [LibraryImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetWindowPos(nint hwnd, nint after, int x, int y, int cx, int cy, uint flags);

    [LibraryImport("kernel32.dll")]
    private static partial nint GetCurrentProcess();

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool TerminateProcess(nint process, uint code);

    /// <summary>Ends the process now, without waiting on WinUI's shutdown.</summary>
    public static void ExitNow() => TerminateProcess(GetCurrentProcess(), 0);

    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_POWER_THROTTLING_STATE
    {
        public uint Version, ControlMask, StateMask;
    }

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetProcessInformation(nint process, int cls, ref PROCESS_POWER_THROTTLING_STATE info, int size);

    [LibraryImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool SetPriorityClass(nint process, uint priority);

    /// <summary>
    /// Keeps Windows from putting this mostly idle process into efficiency mode
    /// (EcoQoS, low priority), which makes the first window after a while slow.
    /// </summary>
    public static void DisableThrottling()
    {
        // ProcessPowerThrottling = 4; PROCESS_POWER_THROTTLING_EXECUTION_SPEED = 1; state 0 = off.
        var s = new PROCESS_POWER_THROTTLING_STATE { Version = 1, ControlMask = 1, StateMask = 0 };
        SetProcessInformation(GetCurrentProcess(), 4, ref s, Marshal.SizeOf<PROCESS_POWER_THROTTLING_STATE>());
        SetPriorityClass(GetCurrentProcess(), 0x20);  // NORMAL_PRIORITY_CLASS
    }

    [LibraryImport("dwmapi.dll")]
    public static partial int DwmSetWindowAttribute(nint hwnd, int attr, ref int value, int size);

    [StructLayout(LayoutKind.Sequential)]
    public struct POINT
    {
        public int X, Y;
    }
}
