#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace util {

std::wstring Widen(std::string_view s);
std::string Narrow(std::wstring_view s);

std::wstring KnownFolder(const GUID& id);
std::wstring AppDataDir();  // %LOCALAPPDATA%\YuboCao\TailDrop (created on demand)
std::wstring InboxDir();    // AppDataDir()\Inbox
std::wstring ExePath();
std::wstring ExeDir();

std::wstring FileName(const std::wstring& path);
std::wstring Stem(const std::wstring& name);
std::wstring Extension(const std::wstring& name);  // including the dot
std::wstring ParentDir(const std::wstring& path);
std::wstring Join(const std::wstring& dir, const std::wstring& name);

bool Exists(const std::wstring& path);
bool IsDirectory(const std::wstring& path);
int64_t FileSize(const std::wstring& path);
// Returns dir\name, or dir\name (2).ext ... when that is taken.
std::wstring UniquePath(const std::wstring& dir, const std::wstring& name);
// Replaces characters Windows does not allow in file names.
std::wstring SafeFileName(std::wstring name);

bool IsProcessRunning(const wchar_t* exeName);

// Runs fn on a new detached thread with a small stack.
void RunAsync(std::function<void()> fn);

bool MoveFileTo(const std::wstring& from, const std::wstring& to);
bool RecycleFile(const std::wstring& path);
void OpenPath(const std::wstring& path);
void RevealInExplorer(const std::wstring& path);

// Appends a line to AppDataDir()\taildrop.log when the TAILDROP_LOG
// environment variable is set; always goes to OutputDebugString.
void Log(const wchar_t* fmt, ...);

}  // namespace util
