An AirDrop-style companion for Tailscale Taildrop on Windows 10 and 11 (x64).

**New in 1.1.3**

- Paste to send: with the flyout open, press Ctrl+V to send the clipboard to the device under the pointer, or choose a device for it. A device's right-click menu has **Paste and send** too. The flyout shows a hint when there's something to paste.
- Besides files, you can now paste or drop a screenshot or copied image (sent as a PNG), copied text (sent as a .txt), and files that exist only inside another app, such as a mail attachment or a browser download. These keep their name and type instead of being ignored.
- Click a received file in the flyout to copy it to the clipboard, the same as Copy in File Explorer; double-click opens it. The right-click menu also has **Copy**.

**Also in 1.1.2**

- The tray flyout opens at once even when you haven't used TailDrop for hours. The interface now stays running in the background instead of exiting a minute after its last window closes, and Windows no longer puts it in efficiency mode. This uses more memory (about 100 MB) in exchange for no cold start.
- Fixed an error that could close the interface when the flyout opened right after TailDrop started.

**Also in 1.1.1**

- After the PC wakes from sleep or hibernation, TailDrop reconnects to Tailscale, re-adds its tray icon and restarts the drag-to-tray watch, so tray clicks and incoming files no longer go unnoticed.
- A click on the tray icon always gets an answer: if the interface stops responding, TailDrop restarts it within a few seconds and shows what you asked for.
- Files no longer sit at "Finishing": if the Tailscale app hasn't moved a received file into Downloads after a few seconds, TailDrop fetches it itself.
- The receiving notification shows speed, time left, "n of m" for several files and when the sender has paused, and has a Hide button. Notifications stay on screen at least 30 seconds and above other windows.

**Also in 1.1**

- Every window is now WinUI 3: the tray flyout, notifications, Settings and the send and sign-in dialogs, with WinUI's own controls and motion. They are drawn by a separate `TailDropUI.exe`; the Tailscale connection and transfers run in a native part that idles at about 2 MB.
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
