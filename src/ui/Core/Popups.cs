using Microsoft.UI.Composition;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;

namespace TailDrop.UI.Core;

/// <summary>
/// Mica that always looks like an active window. Popups (the flyout and the
/// notifications) are rarely focused, and Mica on an inactive window turns
/// into a flat solid colour.
/// </summary>
public sealed partial class ActiveMicaBackdrop : SystemBackdrop
{
    private MicaController? _controller;
    private SystemBackdropConfiguration? _config;

    protected override void OnTargetConnected(ICompositionSupportsSystemBackdrop target, XamlRoot xamlRoot)
    {
        base.OnTargetConnected(target, xamlRoot);
        _config = new SystemBackdropConfiguration { IsInputActive = true };
        UpdateTheme(xamlRoot);
        _controller = new MicaController();
        _controller.SetSystemBackdropConfiguration(_config);
        _controller.AddSystemBackdropTarget(target);
        if (xamlRoot.Content is FrameworkElement fe) fe.ActualThemeChanged += (_, _) => UpdateTheme(xamlRoot);
    }

    protected override void OnTargetDisconnected(ICompositionSupportsSystemBackdrop target)
    {
        base.OnTargetDisconnected(target);
        _controller?.RemoveSystemBackdropTarget(target);
        _controller?.Dispose();
        _controller = null;
    }

    private void UpdateTheme(XamlRoot root)
    {
        if (_config == null) return;
        _config.Theme = (root.Content as FrameworkElement)?.ActualTheme switch
        {
            ElementTheme.Dark => SystemBackdropTheme.Dark,
            ElementTheme.Light => SystemBackdropTheme.Light,
            _ => SystemBackdropTheme.Default,
        };
    }
}

/// <summary>Helpers for TailDrop's borderless popups and window motion.</summary>
public static class Popups
{
    public static nint Hwnd(Window w) => WinRT.Interop.WindowNative.GetWindowHandle(w);

    public static double Scale(Window w) => Native.GetDpiForWindow(Hwnd(w)) / 96.0;

    /// <summary>Turns a window into a rounded, borderless, always-on-top tool window.</summary>
    public static void MakePopup(Window w, bool noActivate)
    {
        var aw = w.AppWindow;
        if (aw.Presenter is OverlappedPresenter p)
        {
            p.SetBorderAndTitleBar(true, false);
            p.IsResizable = false;
            p.IsMaximizable = false;
            p.IsMinimizable = false;
            p.IsAlwaysOnTop = true;
        }
        aw.IsShownInSwitchers = false;
        nint hwnd = Hwnd(w);
        long ex = Native.GetWindowLongPtr(hwnd, Native.GWL_EXSTYLE);
        ex |= Native.WS_EX_TOOLWINDOW;
        if (noActivate) ex |= Native.WS_EX_NOACTIVATE;
        Native.SetWindowLongPtr(hwnd, Native.GWL_EXSTYLE, (nint)ex);
        int corner = Native.DWMWCP_ROUND;
        Native.DwmSetWindowAttribute(hwnd, Native.DWMWA_WINDOW_CORNER_PREFERENCE, ref corner, sizeof(int));
        w.SystemBackdrop = new ActiveMicaBackdrop();
    }

    /// <summary>
    /// A small dialog-like window: WinUI title bar over Mica (as in Settings), close
    /// button only, sized to its content.
    /// </summary>
    public static void MakeDialog(Window w, Microsoft.UI.Xaml.Controls.TitleBar titleBar)
    {
        // A dialog presenter: not resizable, and only the close caption button.
        w.AppWindow.SetPresenter(OverlappedPresenter.CreateForDialog());
        w.ExtendsContentIntoTitleBar = true;
        w.SetTitleBar(titleBar);
        string icon = System.IO.Path.Combine(AppContext.BaseDirectory, "Assets", "app.ico");
        if (System.IO.File.Exists(icon)) w.AppWindow.SetIcon(icon);
        w.SystemBackdrop = new MicaBackdrop();
    }

    /// <summary>Sizes a dialog to `root` (laid out off screen first) and shows it in the
    /// middle of the screen the pointer is on, with the system's own open animation.</summary>
    public static void PresentDialog(Window w, FrameworkElement root, double widthDips, Action? shown = null)
    {
        var aw = w.AppWindow;
        Native.GetCursorPos(out var pt);
        var cursor = new PointInt32(pt.X, pt.Y);
        aw.Move(OffscreenNear(cursor));
        aw.Show(false);
        w.DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            var size = FitDialog(w, root, widthDips);
            aw.Hide();
            var work = DisplayArea.GetFromPoint(cursor, DisplayAreaFallback.Nearest).WorkArea;
            aw.Move(new PointInt32(work.X + (work.Width - size.Width) / 2, work.Y + (work.Height - size.Height) / 2));
            aw.Show(true);
            w.Activate();
            Native.SetForegroundWindow(Hwnd(w));
            shown?.Invoke();
        });
    }

    /// <summary>Sizes a dialog to its content; returns the outer size.</summary>
    public static SizeInt32 FitDialog(Window w, FrameworkElement root, double widthDips) =>
        FitContent(w, root, widthDips, LaidOutHeight(root));

    /// <summary>
    /// Makes the window's XAML area `widthDips` x `heightDips`; returns the outer size.
    /// The window is grown or shrunk by the difference between its XAML area and that,
    /// because the "client" size AppWindow works with doesn't match the XAML area for
    /// borderless popups or content extended into the title bar.
    /// </summary>
    public static SizeInt32 FitContent(Window w, FrameworkElement root, double widthDips, double heightDips)
    {
        double scale = Scale(w);
        var aw = w.AppWindow;
        var area = root.XamlRoot?.Size ?? new Windows.Foundation.Size(0, 0);
        if (area.Width <= 0)
        {
            aw.ResizeClient(new SizeInt32((int)Math.Ceiling(widthDips * scale), (int)Math.Ceiling(heightDips * scale)));
            return aw.Size;
        }
        int dw = (int)Math.Round((widthDips - area.Width) * scale), dh = (int)Math.Round((heightDips - area.Height) * scale);
        if (dw != 0 || dh != 0) aw.Resize(new SizeInt32(aw.Size.Width + dw, aw.Size.Height + dh));
        return aw.Size;
    }

    /// <summary>
    /// A spot just below the given display, off screen but nearest to it, so a window
    /// laid out there gets that display's DPI (and so its final size) before it appears.
    /// </summary>
    public static PointInt32 OffscreenNear(PointInt32 onDisplay)
    {
        var outer = DisplayArea.GetFromPoint(onDisplay, DisplayAreaFallback.Nearest).OuterBounds;
        return new PointInt32(outer.X + 8, outer.Y + outer.Height + 64);
    }

    /// <summary>The height a window's root element wants at its width, however tall the
    /// window is now (its laid-out height is capped by the window).</summary>
    public static double LaidOutHeight(FrameworkElement root)
    {
        double width = double.IsNaN(root.Width) ? root.ActualWidth : root.Width;
        root.Measure(new Windows.Foundation.Size(width > 0 ? width : double.PositiveInfinity, double.PositiveInfinity));
        return root.DesiredSize.Height;
    }

    public static RectInt32 WorkArea(int[]? tray)
    {
        PointInt32 p = tray is { Length: 4 }
            ? new PointInt32((tray[0] + tray[2]) / 2, (tray[1] + tray[3]) / 2)
            : default;
        var area = tray is { Length: 4 } && tray[2] > tray[0]
            ? DisplayArea.GetFromPoint(p, DisplayAreaFallback.Nearest)
            : DisplayArea.Primary;
        return area.WorkArea;
    }

    /// <summary>True when the taskbar (and so the tray) is at the top of that work area.</summary>
    public static bool TaskbarAtTop(int[]? tray, RectInt32 work) =>
        tray is { Length: 4 } && tray[2] > tray[0] && tray[3] <= work.Y + 4;

    // Fluent motion curves.
    public static readonly CubicBezier Decelerate = new(0.1, 0.9, 0.2, 1.0);
    public static readonly CubicBezier Accelerate = new(0.7, 0.0, 1.0, 0.5);
    public static readonly CubicBezier Standard = new(0.8, 0.0, 0.2, 1.0);

    private static readonly Dictionary<AppWindow, EventHandler<object>> Running = new();

    /// <summary>Moves a window from its position to `to`, frame by frame.</summary>
    public static void Animate(AppWindow aw, PointInt32 to, double ms, CubicBezier curve, Action? done = null)
    {
        Stop(aw);
        PointInt32 from = aw.Position;
        if (from.X == to.X && from.Y == to.Y || ms <= 0)
        {
            aw.Move(to);
            done?.Invoke();
            return;
        }
        var clock = System.Diagnostics.Stopwatch.StartNew();
        EventHandler<object>? tick = null;
        tick = (_, _) =>
        {
            double t = Math.Min(1, clock.Elapsed.TotalMilliseconds / ms);
            double k = curve.Ease(t);
            aw.Move(new PointInt32((int)Math.Round(from.X + (to.X - from.X) * k), (int)Math.Round(from.Y + (to.Y - from.Y) * k)));
            if (t >= 1)
            {
                Stop(aw);
                done?.Invoke();
            }
        };
        Running[aw] = tick;
        CompositionTarget.Rendering += tick;
    }

    public static void Stop(AppWindow aw)
    {
        if (Running.Remove(aw, out var tick)) CompositionTarget.Rendering -= tick;
    }

    /// <summary>Fades an element's visual from `from` to `to` opacity on the compositor.</summary>
    public static void Fade(UIElement element, float from, float to, double ms, CubicBezier curve)
    {
        var visual = Microsoft.UI.Xaml.Hosting.ElementCompositionPreview.GetElementVisual(element);
        var c = visual.Compositor;
        var anim = c.CreateScalarKeyFrameAnimation();
        anim.InsertKeyFrame(0f, from);
        anim.InsertKeyFrame(1f, to, c.CreateCubicBezierEasingFunction(
            new System.Numerics.Vector2((float)curve.X1, (float)curve.Y1),
            new System.Numerics.Vector2((float)curve.X2, (float)curve.Y2)));
        anim.Duration = TimeSpan.FromMilliseconds(ms);
        visual.Opacity = to;
        visual.StartAnimation("Opacity", anim);
    }
}

/// <summary>CSS-style cubic-bezier easing (x1, y1, x2, y2).</summary>
public readonly record struct CubicBezier(double X1, double Y1, double X2, double Y2)
{
    public double Ease(double t)
    {
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        // Solve x(u) = t for u with Newton's method, then return y(u).
        double u = t;
        for (int i = 0; i < 8; i++)
        {
            double x = Bezier(u, X1, X2) - t;
            double dx = Derivative(u, X1, X2);
            if (Math.Abs(x) < 1e-5 || Math.Abs(dx) < 1e-6) break;
            u -= x / dx;
        }
        return Bezier(Math.Clamp(u, 0, 1), Y1, Y2);
    }

    private static double Bezier(double u, double a, double b) =>
        3 * a * u * (1 - u) * (1 - u) + 3 * b * u * u * (1 - u) + u * u * u;

    private static double Derivative(double u, double a, double b) =>
        3 * a * (1 - u) * (1 - u) + 6 * (b - a) * u * (1 - u) + 3 * (1 - b) * u * u;
}
