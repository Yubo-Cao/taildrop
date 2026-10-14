<p align="center"><img src="assets/app-256.png" width="96" alt=""></p>

<h1 align="center">TailDrop</h1>

<p align="center">An AirDrop-style companion for <a href="https://tailscale.com/kb/1106/taildrop">Tailscale Taildrop</a> on Windows.<br>
Native Win32, about 2&nbsp;MB of memory when idle.</p>

TailDrop sits in the system tray. When a phone, tablet or another computer sends this PC a file over Taildrop, a notification slides up with a preview of the file and asks where to put it. You can save it, open it, or drag the preview straight into any folder or app, because it's a real file. Sending works the same way in reverse: drop files on the tray icon, or right-click them in File Explorer and pick a device.

TailDrop is unofficial and not affiliated with Tailscale Inc. "Tailscale" and "Taildrop" are trademarks of Tailscale Inc.

## Features

**Receiving**
- A notification with a rendered preview (Explorer's own thumbnails, so photos, videos, PDFs and documents all work), the file's size and type, and a progress bar while it arrives.
- **Save** puts it in your default folder (Downloads unless you change it). The "Save to" line shows where it will go, and you can pick another folder for that transfer.
- Drag the preview anywhere: Explorer, the desktop, a chat window, an upload field. It is moved out of TailDrop's inbox like any other file.
- Several files sent together arrive in one notification. Anything you don't deal with stays in the inbox, shown in the tray flyout and marked on the tray icon.
- Optional automatic saving.

**Sending**
- Left-click the tray icon for the flyout: drop files on a device tile, or click a tile to choose files.
- Drag files onto the tray icon itself and a drop shelf opens above it.
- Right-click files or folders in File Explorer and choose **Send with TailDrop**. Up to three favorite devices appear directly in the submenu; **Other devices…** opens a picker with every device.
- Folders are sent as a `.zip` that is built while it uploads, so there is no temporary copy.
- Progress, speed, cancel and retry in the send notification.

**Everything else**
- If Tailscale isn't signed in or connected, TailDrop shows a screen asking you to sign in, and the tray icon shows it.
- Windows 11 look and behavior: Mica and acrylic backdrops, rounded corners, Segoe UI Variable and Fluent icons, light and dark mode, your accent color, per-monitor DPI, and native menus and file dialogs.

## Requirements

- Windows 10 2004 or later, or Windows 11, on x64.
- [Tailscale for Windows](https://tailscale.com/download/windows), signed in to your tailnet.

## Install

From a release: download the zip from [Releases](https://github.com/Yubo-Cao/taildrop/releases), extract it, and run in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

From source, with Visual Studio 2022 or later (Desktop C++ workload and a Windows 11 SDK), run the same command in the repository; it builds first.

The installer works per user and needs no administrator rights. It copies TailDrop to `%LOCALAPPDATA%\Programs\TailDrop`, adds the Explorer menu, a Start menu shortcut (**TailDrop (native)**), an Apps entry and start at sign-in, then starts it.

- **Windows 11 menu.** The new context menu only shows commands from apps with package identity, which TailDrop gets from a small sparse package. Windows accepts it only signed, or with **Developer Mode** on (Settings > System > For developers). Without either, the command is under **Show more options**. Run `install.ps1` again after turning Developer Mode on.
- **Smart App Control.** While release binaries are unsigned (see [Code signing policy](#code-signing-policy)), Smart App Control blocks them. Turn it off in Windows Security > App & browser control > Smart App Control, or wait for a signed release.
- **Two notifications?** The Tailscale app shows its own "file received" notification. You can turn that off in Settings > System > Notifications > Tailscale.

To uninstall, use Settings > Apps, or run `uninstall.ps1`. Files still waiting in the inbox are kept unless you pass `-RemoveInbox`.

## How it works

- TailDrop talks to the Tailscale service through its LocalAPI named pipe, so it never starts `tailscale.exe`. It subscribes to the service's notification stream for incoming transfers and device changes, so it does no polling.
- The Tailscale app for Windows saves every received file to Downloads right away. TailDrop matches the finished transfer to that file by name, size and time, moves it into its inbox (`%LOCALAPPDATA%\YuboCao\TailDrop\Inbox`, an instant rename on the same drive) and shows the notification. If the Tailscale app isn't running, TailDrop downloads waiting files from the service itself.
- Sending streams each file to the service (`PUT /localapi/v0/file-put/…`). Folders go through a small ZIP writer: stored entries, UTF-8 names, ZIP64 for large files.
- Notification-area icons can't receive drag-and-drop, so a low-level mouse hook notices a drag that reaches the icon and opens the drop shelf. You can turn this off in Settings.
- `TailDropShell.dll` implements `IExplorerCommand`. The classic menu loads it through a per-user COM registration; the Windows 11 menu loads it through the sparse package (`package/AppxManifest.xml`). The DLL only hands the selection to `TailDrop.exe`.

### Memory

The UI is drawn with Direct2D in software mode, which keeps GPU drivers out of the process. All graphics objects are released and the working set is trimmed when nothing is on screen. TailDrop also uses the segment heap, has no IME (it has no text fields), and links the C runtime statically. There is no framework or runtime. Measured on Windows 11:

| State | Working set | Private bytes |
| --- | --- | --- |
| Started in the background | 11.4 MB | 2.1 MB |
| After opening and closing the flyout | 2.3 MB | 5.9 MB |
| Flyout open | 35 MB | 17 MB |

## Building

```powershell
.\build.ps1             # Release build into build\
.\build.ps1 -Config Debug
```

The output is `TailDrop.exe`, `TailDropShell.dll` and `TailDrop.msix` (the sparse package). Icons are generated: `python tools/make_icons.py` (needs Pillow) rewrites `assets/` and `package/Assets/`.

Set `TAILDROP_LOG=1` before starting TailDrop to log to `%LOCALAPPDATA%\YuboCao\TailDrop\taildrop.log`.

## Code signing policy

Release builds are produced by the [GitHub Actions workflow](.github/workflows/build.yml) from this repository's source. Signing them through the free open-source program of [SignPath Foundation](https://signpath.org) (free code signing provided by SignPath.io, certificate by SignPath Foundation) is being set up. Until it is active, releases are unsigned.

Team roles:

- Committers and reviewers: [Yubo Cao](https://github.com/Yubo-Cao)
- Approvers: [Yubo Cao](https://github.com/Yubo-Cao)

### Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. TailDrop only talks to the Tailscale service on your PC; files are sent to the devices you choose, through Taildrop.

## License

[MIT](LICENSE)
