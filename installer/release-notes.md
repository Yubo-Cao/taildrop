An AirDrop-style companion for Tailscale Taildrop on Windows 10 and 11 (x64).

**New in 1.1**

- Every window is now WinUI 3: the tray flyout, notifications, Settings and the send and sign-in dialogs, with WinUI's own controls and motion. They are drawn by a separate `TailDropUI.exe` that starts only when something is on screen. The part that keeps running in the background idles at about 2 MB.
- Settings uses the Windows 11 Settings layout: a sidebar with your Tailscale account, the title bar drawn into the window, and a page for each device where you can choose its icon and name and make it a favorite.
- The Explorer submenu shows favorites, then the devices you sent to recently, then the rest (online first).
- The send window ("Other devices…", or files dropped without a device) is a proper window with the same title bar, and you can add more files to it.

**Install:** download `TailDrop-<version>-Setup.exe` and run it. It installs for your user account only, with no administrator rights. You need Tailscale for Windows, signed in.

Prefer a script? Extract the `.zip` and run `powershell -ExecutionPolicy Bypass -File .\install.ps1`.

These builds are not code-signed yet (see the [code signing policy](https://github.com/Yubo-Cao/taildrop#code-signing-policy)):

- SmartScreen may warn on the first run: choose **More info → Run anyway**.
- Smart App Control blocks unsigned apps; it has to be off to run TailDrop.
- For "Send with TailDrop" in the Windows 11 context menu itself (rather than under "Show more options"), turn on Developer Mode before installing.

The uninstaller is in Settings → Apps. Files still waiting in TailDrop's inbox are kept.
