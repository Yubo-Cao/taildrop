#include "ipc.h"

#include <sddl.h>

#include <cstdio>

#include "util.h"

bool UiLink::Start(HWND host) {
  host_ = host;
  DWORD session = 0;
  ProcessIdToSessionId(GetCurrentProcessId(), &session);
  name_ = L"\\\\.\\pipe\\YuboCao.TailDrop.UI." + std::to_wstring(session);
  stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  wake_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  thread_ = CreateThread(nullptr, 128 * 1024, ThreadMain, this, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
  return thread_ != nullptr;
}

void UiLink::Stop() {
  if (!thread_) return;
  SetEvent(stop_);
  WaitForSingleObject(thread_, 2000);
  CloseHandle(thread_);
  thread_ = nullptr;
}

void UiLink::Send(std::string line) {
  if (!connected_) return;
  line.push_back('\n');
  {
    std::lock_guard<std::mutex> lock(mu_);
    out_.push_back(std::move(line));
  }
  SetEvent(wake_);
}

DWORD WINAPI UiLink::ThreadMain(void* self) {
  static_cast<UiLink*>(self)->Loop();
  return 0;
}

void UiLink::Loop() {
  // Only this user (and SYSTEM) may open the pipe; never remote clients.
  SECURITY_ATTRIBUTES sa{sizeof(sa)};
  ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)(A;;GA;;;SY)", SDDL_REVISION_1,
                                                       &sa.lpSecurityDescriptor, nullptr);
  while (WaitForSingleObject(stop_, 0) != WAIT_OBJECT_0) {
    HANDLE pipe = CreateNamedPipeW(name_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                   64 * 1024, 64 * 1024, 0, &sa);
    if (pipe == INVALID_HANDLE_VALUE) {
      if (WaitForSingleObject(stop_, 1000) == WAIT_OBJECT_0) break;
      continue;
    }
    if (Serve(pipe)) {
      connected_ = false;
      {
        std::lock_guard<std::mutex> lock(mu_);
        out_.clear();
      }
      PostMessageW(host_, WM_APP_UI_DISCONNECTED, 0, 0);
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }
  LocalFree(sa.lpSecurityDescriptor);
}

// Returns true if a client connected (and has now gone away).
bool UiLink::Serve(HANDLE pipe) {
  HANDLE connEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  OVERLAPPED ov{};
  ov.hEvent = connEv;
  bool ok = ConnectNamedPipe(pipe, &ov) != FALSE;
  DWORD err = GetLastError();
  if (!ok && err == ERROR_IO_PENDING) {
    HANDLE hs[2] = {connEv, stop_};
    if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) != WAIT_OBJECT_0) {
      CancelIoEx(pipe, &ov);
      CloseHandle(connEv);
      return false;
    }
    DWORD n;
    ok = GetOverlappedResult(pipe, &ov, &n, FALSE) != FALSE;
  } else if (!ok && err == ERROR_PIPE_CONNECTED) {
    ok = true;
  }
  CloseHandle(connEv);
  if (!ok) return false;

  connected_ = true;
  PostMessageW(host_, WM_APP_UI_CONNECTED, 0, 0);

  HANDLE readEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  HANDLE writeEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  OVERLAPPED rov{};
  rov.hEvent = readEv;
  char buf[16 * 1024];
  std::string line;
  bool reading = false, alive = true;
  while (alive) {
    if (!reading) {
      ResetEvent(readEv);
      if (!ReadFile(pipe, buf, sizeof(buf), nullptr, &rov) && GetLastError() != ERROR_IO_PENDING) break;
      reading = true;
    }
    HANDLE hs[3] = {readEv, wake_, stop_};
    DWORD w = WaitForMultipleObjects(3, hs, FALSE, INFINITE);
    if (w == WAIT_OBJECT_0) {
      DWORD n = 0;
      reading = false;
      if (!GetOverlappedResult(pipe, &rov, &n, FALSE) || n == 0) break;
      for (DWORD i = 0; i < n; ++i) {
        if (buf[i] == '\n') {
          if (!line.empty()) {
            auto* cmd = new std::string(std::move(line));
            if (!PostMessageW(host_, WM_APP_UI_COMMAND, 0, reinterpret_cast<LPARAM>(cmd))) delete cmd;
          }
          line.clear();
        } else if (buf[i] != '\r') {
          line.push_back(buf[i]);
        }
      }
    } else if (w == WAIT_OBJECT_0 + 1) {
      std::deque<std::string> batch;
      {
        std::lock_guard<std::mutex> lock(mu_);
        batch.swap(out_);
      }
      for (const std::string& s : batch) {
        OVERLAPPED wov{};
        wov.hEvent = writeEv;
        ResetEvent(writeEv);
        DWORD n = 0;
        if (!WriteFile(pipe, s.data(), static_cast<DWORD>(s.size()), nullptr, &wov) &&
            GetLastError() != ERROR_IO_PENDING) {
          alive = false;
          break;
        }
        if (!GetOverlappedResult(pipe, &wov, &n, TRUE)) {
          alive = false;
          break;
        }
      }
    } else {
      break;
    }
  }
  if (reading) {
    CancelIoEx(pipe, &rov);
    DWORD n;
    GetOverlappedResult(pipe, &rov, &n, TRUE);
  }
  CloseHandle(readEv);
  CloseHandle(writeEv);
  return true;
}

// ---------------------------------------------------------------------------

void Json::comma() {
  if (!first_) s_.push_back(',');
  first_ = false;
}

void Json::key(std::string_view k) {
  comma();
  s_.push_back('"');
  s_.append(k);
  s_.append("\":");
}

void Json::escape(std::string& out, std::string_view v) {
  out.push_back('"');
  for (unsigned char c : v) {
    switch (c) {
      case '"': out.append("\\\""); break;
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (c < 0x20) {
          char tmp[8];
          snprintf(tmp, sizeof(tmp), "\\u%04x", c);
          out.append(tmp);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  out.push_back('"');
}

Json& Json::begin() {
  if (!s_.empty() && s_.back() != '[' && s_.back() != ':') comma();
  s_.push_back('{');
  first_ = true;
  return *this;
}

Json& Json::end() {
  s_.push_back('}');
  first_ = false;
  return *this;
}

Json& Json::beginArray(std::string_view k) {
  key(k);
  s_.push_back('[');
  first_ = true;
  return *this;
}

Json& Json::endArray() {
  s_.push_back(']');
  first_ = false;
  return *this;
}

Json& Json::item() {
  comma();
  s_.push_back('{');
  first_ = true;
  return *this;
}

Json& Json::raw(std::string_view k, std::string_view json) {
  key(k);
  s_.append(json);
  return *this;
}

Json& Json::str(std::string_view k, std::string_view v) {
  key(k);
  escape(s_, v);
  return *this;
}

Json& Json::str(std::string_view k, std::wstring_view v) { return str(k, util::Narrow(v)); }

Json& Json::num(std::string_view k, int64_t v) {
  key(k);
  s_.append(std::to_string(v));
  return *this;
}

Json& Json::dbl(std::string_view k, double v) {
  key(k);
  char tmp[32];
  snprintf(tmp, sizeof(tmp), "%.1f", v);
  s_.append(tmp);
  return *this;
}

Json& Json::boolean(std::string_view k, bool v) {
  key(k);
  s_.append(v ? "true" : "false");
  return *this;
}

Json& Json::strItem(std::wstring_view v) {
  comma();
  escape(s_, util::Narrow(v));
  return *this;
}
