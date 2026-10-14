using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using TailDrop.UI.Core;
using Windows.Graphics;

namespace TailDrop.UI.Views;

/// <summary>Content shown in a notification window.</summary>
public interface IToastContent
{
    FrameworkElement Root { get; }
    /// <summary>The window it is shown in (owner for folder pickers).</summary>
    Window? HostWindow { get; set; }
    /// <summary>Raised when the size changed or the toast wants to close.</summary>
    event Action? LayoutChanged;
    event Action? CloseRequested;
    /// <summary>Milliseconds until it should go away on its own (0: stay).</summary>
    int AutoDismissMs { get; }
    void OnClosed();
}

/// <summary>A borderless, non-activating Mica window holding one notification.</summary>
public sealed partial class ToastWindow : Window
{
    public const double WidthDips = 364;

    public IToastContent Toast { get; }
    public bool Ready { get; private set; }
    public bool Leaving { get; private set; }

    private readonly DispatcherQueueTimer _timer;
    private DateTime _dismissAt = DateTime.MaxValue;
    private bool _hover;

    public event Action? NeedsLayout;
    public event Action? Gone;

    public ToastWindow(IToastContent toast)
    {
        Toast = toast;
        toast.HostWindow = this;
        Content = toast.Root;
        Popups.MakePopup(this, noActivate: true);
        toast.Root.PointerEntered += (_, _) => _hover = true;
        toast.Root.PointerExited += (_, _) => _hover = false;
        toast.LayoutChanged += () =>
        {
            ArmDismiss();
            if (Ready) NeedsLayout?.Invoke();
        };
        toast.CloseRequested += () => Leave();
        _timer = DispatcherQueue.CreateTimer();
        _timer.Interval = TimeSpan.FromMilliseconds(500);
        _timer.Tick += (_, _) =>
        {
            if (_hover) _dismissAt = DateTime.Now.AddMilliseconds(Math.Max(1500, (_dismissAt - DateTime.Now).TotalMilliseconds));
            else if (DateTime.Now >= _dismissAt) Leave();
        };
        _timer.Start();
        Closed += (_, _) => _timer.Stop();
    }

    /// <summary>Shows it off screen so it can be measured; `ready` runs once laid out.</summary>
    public void Prepare(PointInt32 near, Action ready)
    {
        AppWindow.Move(Popups.OffscreenNear(near));
        AppWindow.Show(false);
        DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () =>
        {
            Ready = true;
            ArmDismiss();
            ready();
        });
    }

    public SizeInt32 PixelSize()
    {
        return Popups.FitContent(this, Toast.Root, WidthDips, Popups.LaidOutHeight(Toast.Root));
    }

    public void Enter(PointInt32 target, bool fromRight)
    {
        int offset = (int)(48 * Popups.Scale(this));
        AppWindow.Move(fromRight ? new PointInt32(target.X + offset, target.Y) : new PointInt32(target.X, target.Y + offset));
        Popups.Fade(Toast.Root, 0, 1, 200, Popups.Decelerate);
        Popups.Animate(AppWindow, target, 333, Popups.Decelerate);
    }

    public void MoveTo(PointInt32 target) => Popups.Animate(AppWindow, target, 250, Popups.Standard);

    public void SetHidden(bool hidden)
    {
        if (Leaving || !Ready) return;
        if (hidden) AppWindow.Hide();
        else AppWindow.Show(false);
    }

    public void Leave()
    {
        if (Leaving) return;
        Leaving = true;
        _timer.Stop();
        Toast.OnClosed();
        var p = AppWindow.Position;
        int offset = (int)(48 * Popups.Scale(this));
        Popups.Fade(Toast.Root, 1, 0, 167, Popups.Accelerate);
        Popups.Animate(AppWindow, new PointInt32(p.X + offset, p.Y), 167, Popups.Accelerate, () =>
        {
            Gone?.Invoke();
            Close();
        });
    }

    private void ArmDismiss()
    {
        int ms = Toast.AutoDismissMs;
        _dismissAt = ms > 0 ? DateTime.Now.AddMilliseconds(ms) : DateTime.MaxValue;
    }
}

/// <summary>Stacks notifications above the tray, newest at the bottom.</summary>
public sealed class ToastManager
{
    private readonly List<ToastWindow> _toasts = new();
    private readonly Dictionary<int, ToastWindow> _batches = new(), _jobs = new();
    private int[]? _tray;
    private bool _suppressed;

    public int Count => _toasts.Count;

    public void ShowBatch(int batch, int[]? tray)
    {
        if (_batches.ContainsKey(batch)) return;
        var w = new ToastWindow(new ReceiveToast(batch));
        _batches[batch] = w;
        w.Gone += () => _batches.Remove(batch);
        Add(w, tray);
    }

    public void ShowJob(int job, int[]? tray)
    {
        if (_jobs.ContainsKey(job)) return;
        var w = new ToastWindow(new SendToast(job));
        _jobs[job] = w;
        w.Gone += () => _jobs.Remove(job);
        Add(w, tray);
    }

    private void Add(ToastWindow w, int[]? tray)
    {
        if (tray is { Length: 4 } && tray[2] > tray[0]) _tray = tray;
        _toasts.Add(w);
        w.NeedsLayout += () => Layout(null);
        w.Gone += () =>
        {
            _toasts.Remove(w);
            Layout(null);
        };
        var work = Popups.WorkArea(_tray);
        w.Prepare(new PointInt32(work.X + work.Width - 1, work.Y + work.Height - 1), () =>
        {
            if (_suppressed) w.SetHidden(true);
            Layout(w);
        });
    }

    public void SetSuppressed(bool suppressed)
    {
        if (_suppressed == suppressed) return;
        _suppressed = suppressed;
        foreach (var t in _toasts) t.SetHidden(suppressed);
        if (!suppressed) Layout(null);
    }

    private void Layout(ToastWindow? entering)
    {
        var ready = _toasts.Where(t => t.Ready && !t.Leaving).ToList();
        if (ready.Count == 0) return;
        var work = Popups.WorkArea(_tray);
        bool top = Popups.TaskbarAtTop(_tray, work);
        double scale = Popups.Scale(ready[0]);
        int margin = (int)(12 * scale), gap = (int)(8 * scale);
        int y = top ? work.Y + margin : work.Y + work.Height - margin;
        for (int i = ready.Count - 1; i >= 0; i--)
        {
            var t = ready[i];
            var size = t.PixelSize();
            int x = work.X + work.Width - margin - size.Width;
            if (!top) y -= size.Height;
            if (!top && y < work.Y) break;
            var target = new PointInt32(x, top ? y : y);
            if (t == entering) t.Enter(target, fromRight: true);
            else t.MoveTo(target);
            if (top) y += size.Height + gap;
            else y -= gap;
        }
    }
}
