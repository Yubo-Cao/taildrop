An AirDrop-style companion for Tailscale Taildrop on Windows 10 and 11 (x64).

**Install:** download `TailDrop-<version>-Setup.exe` and run it. It installs for your user account only, with no administrator rights. You need Tailscale for Windows, signed in.

Prefer a script? Extract the `.zip` and run `powershell -ExecutionPolicy Bypass -File .\install.ps1`.

These builds are not code-signed yet (see the [code signing policy](https://github.com/Yubo-Cao/taildrop#code-signing-policy)):

- SmartScreen may warn on the first run: choose **More info → Run anyway**.
- Smart App Control blocks unsigned apps; it has to be off to run TailDrop.
- For "Send with TailDrop" in the Windows 11 context menu itself (rather than under "Show more options"), turn on Developer Mode before installing.

The uninstaller is in Settings → Apps. Files still waiting in TailDrop's inbox are kept.
