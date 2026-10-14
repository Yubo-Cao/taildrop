// Link between TailDrop.exe (the background listener) and TailDropUI.exe
// (the WinUI 3 interface): one named pipe, newline-delimited JSON both ways.
#pragma once

#include <windows.h>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>

// Posted to the host window by the pipe thread.
enum : UINT {
  WM_APP_UI_CONNECTED = WM_APP + 50,
  WM_APP_UI_COMMAND,       // lParam = std::string* (one JSON line)
  WM_APP_UI_DISCONNECTED,
};

class UiLink {
 public:
  // Starts the pipe server thread; the UI connects with the returned name.
  bool Start(HWND host);
  void Stop();
  const std::wstring& pipeName() const { return name_; }
  bool connected() const { return connected_; }
  // Queues one JSON message (without the newline). Dropped if not connected.
  void Send(std::string line);

 private:
  static DWORD WINAPI ThreadMain(void* self);
  void Loop();
  bool Serve(HANDLE pipe);

  HWND host_ = nullptr;
  std::wstring name_;
  HANDLE thread_ = nullptr;
  HANDLE stop_ = nullptr;
  HANDLE wake_ = nullptr;  // signalled when the outgoing queue has data
  std::mutex mu_;
  std::deque<std::string> out_;
  volatile bool connected_ = false;
};

// Small JSON writer for host -> UI messages.
class Json {
 public:
  Json& begin();  // {
  Json& end();    // }
  Json& beginArray(std::string_view key);
  Json& endArray();
  Json& item();  // starts an object inside an array
  Json& raw(std::string_view key, std::string_view json);
  Json& str(std::string_view key, std::string_view utf8);
  Json& str(std::string_view key, std::wstring_view s);
  Json& num(std::string_view key, int64_t v);
  Json& dbl(std::string_view key, double v);
  Json& boolean(std::string_view key, bool v);
  Json& strItem(std::wstring_view s);  // string element of an array
  std::string take() { return std::move(s_); }

 private:
  void comma();
  void key(std::string_view k);
  static void escape(std::string& out, std::string_view utf8);
  std::string s_;
  bool first_ = true;
};
