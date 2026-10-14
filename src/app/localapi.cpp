#include "localapi.h"

#include <algorithm>
#include <cstdlib>

namespace lapi {
namespace {

constexpr wchar_t kPipe[] = L"\\\\.\\pipe\\ProtectedPrefix\\Administrators\\Tailscale\\tailscaled";

bool IEquals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (tolower(static_cast<unsigned char>(a[i])) != tolower(static_cast<unsigned char>(b[i]))) return false;
  return true;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
  return s;
}

}  // namespace

Conn::~Conn() {
  if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
  if (event_) CloseHandle(event_);
}

bool Conn::Open() {
  for (int attempt = 0; attempt < 8; ++attempt) {
    // tailscaled identifies the caller by impersonating the pipe client, which
    // needs at least the Identification level.
    pipe_ = CreateFileW(kPipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipe_ != INVALID_HANDLE_VALUE) break;
    err_ = GetLastError();
    if (err_ != ERROR_PIPE_BUSY) return false;
    WaitNamedPipeW(kPipe, 500);
  }
  if (pipe_ == INVALID_HANDLE_VALUE) return false;
  event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  return event_ != nullptr;
}

bool Conn::Wait(OVERLAPPED& ov, DWORD& n) {
  if (cancel_) {
    HANDLE hs[2] = {event_, cancel_};
    if (WaitForMultipleObjects(2, hs, FALSE, INFINITE) != WAIT_OBJECT_0) {
      CancelIoEx(pipe_, &ov);
      GetOverlappedResult(pipe_, &ov, &n, TRUE);
      err_ = ERROR_CANCELLED;
      return false;
    }
  }
  if (!GetOverlappedResult(pipe_, &ov, &n, TRUE)) {
    err_ = GetLastError();
    return false;
  }
  return true;
}

bool Conn::Write(const void* data, size_t len) {
  const char* p = static_cast<const char*>(data);
  while (len > 0) {
    OVERLAPPED ov{};
    ov.hEvent = event_;
    DWORD chunk = static_cast<DWORD>(std::min<size_t>(len, 1 << 20));
    DWORD n = 0;
    if (!WriteFile(pipe_, p, chunk, nullptr, &ov) && GetLastError() != ERROR_IO_PENDING) {
      err_ = GetLastError();
      return false;
    }
    if (!Wait(ov, n)) return false;
    p += n;
    len -= n;
  }
  return true;
}

bool Conn::SendHead(const char* method, const std::string& path, int64_t contentLength, const char* contentType) {
  std::string h = std::string(method) + " " + path +
                  " HTTP/1.1\r\nHost: local-tailscaled.sock\r\nConnection: close\r\n";
  if (contentLength >= 0) h += "Content-Length: " + std::to_string(contentLength) + "\r\n";
  if (contentType) h += std::string("Content-Type: ") + contentType + "\r\n";
  h += "\r\n";
  return Write(h.data(), h.size());
}

int Conn::RawRead(char* buf, int cap) {
  OVERLAPPED ov{};
  ov.hEvent = event_;
  DWORD n = 0;
  if (!ReadFile(pipe_, buf, cap, nullptr, &ov)) {
    DWORD e = GetLastError();
    if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF || e == ERROR_PIPE_NOT_CONNECTED) return 0;
    if (e != ERROR_IO_PENDING) {
      err_ = e;
      return -1;
    }
  }
  if (!Wait(ov, n)) {
    if (err_ == ERROR_BROKEN_PIPE || err_ == ERROR_HANDLE_EOF || err_ == ERROR_PIPE_NOT_CONNECTED) return 0;
    return -1;
  }
  return static_cast<int>(n);
}

bool Conn::Fill() {
  if (eof_) return false;
  if (pos_ == buf_.size()) {
    buf_.clear();
    pos_ = 0;
  } else if (pos_ > 64 * 1024) {
    buf_.erase(0, pos_);
    pos_ = 0;
  }
  size_t old = buf_.size();
  buf_.resize(old + 16 * 1024);
  int n = RawRead(&buf_[old], 16 * 1024);
  buf_.resize(old + (n > 0 ? n : 0));
  if (n <= 0) {
    eof_ = true;
    return false;
  }
  return true;
}

bool Conn::ReadHead(int& status) {
  size_t end;
  while ((end = buf_.find("\r\n\r\n", pos_)) == std::string::npos)
    if (!Fill()) return false;
  std::string_view head(buf_.data() + pos_, end - pos_);
  pos_ = end + 4;
  size_t sp = head.find(' ');
  if (sp == std::string_view::npos || head.size() < sp + 4) return false;
  status = atoi(std::string(head.substr(sp + 1, 3)).c_str());
  chunked_ = false;
  remaining_ = -1;
  size_t i = head.find("\r\n");
  while (i != std::string_view::npos) {
    size_t j = head.find("\r\n", i + 2);
    std::string_view line = head.substr(i + 2, (j == std::string_view::npos ? head.size() : j) - (i + 2));
    size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
      std::string_view k = Trim(line.substr(0, colon)), v = Trim(line.substr(colon + 1));
      if (IEquals(k, "transfer-encoding") && v.find("chunked") != std::string_view::npos) chunked_ = true;
      if (IEquals(k, "content-length")) remaining_ = _atoi64(std::string(v).c_str());
    }
    i = j;
  }
  if (chunked_) remaining_ = 0;
  needChunkHeader_ = chunked_;
  bodyDone_ = !chunked_ && remaining_ == 0;
  return true;
}

int Conn::ReadBody(char* out, int cap) {
  if (bodyDone_) return 0;
  if (chunked_) {
    if (needChunkHeader_) {
      size_t eol;
      while ((eol = buf_.find("\r\n", pos_)) == std::string::npos)
        if (!Fill()) return -1;
      remaining_ = strtoll(buf_.c_str() + pos_, nullptr, 16);
      pos_ = eol + 2;
      needChunkHeader_ = false;
      if (remaining_ <= 0) {
        bodyDone_ = true;
        return 0;
      }
    }
    while (pos_ >= buf_.size())
      if (!Fill()) return -1;
    int n = static_cast<int>(std::min<int64_t>({static_cast<int64_t>(cap), remaining_,
                                                 static_cast<int64_t>(buf_.size() - pos_)}));
    memcpy(out, buf_.data() + pos_, n);
    pos_ += n;
    remaining_ -= n;
    if (remaining_ == 0) {
      while (buf_.size() - pos_ < 2)
        if (!Fill()) return n;
      pos_ += 2;
      needChunkHeader_ = true;
    }
    return n;
  }
  if (pos_ >= buf_.size() && !Fill()) {
    bodyDone_ = true;
    return remaining_ > 0 ? -1 : 0;
  }
  int64_t avail = static_cast<int64_t>(buf_.size() - pos_);
  int64_t want = remaining_ >= 0 ? std::min(avail, remaining_) : avail;
  int n = static_cast<int>(std::min<int64_t>(want, cap));
  memcpy(out, buf_.data() + pos_, n);
  pos_ += n;
  if (remaining_ > 0) {
    remaining_ -= n;
    if (remaining_ == 0) bodyDone_ = true;
  }
  return n;
}

bool Conn::ReadLine(std::string& line) {
  for (;;) {
    size_t nl = line_.find('\n');
    if (nl != std::string::npos) {
      line.assign(line_, 0, nl);
      line_.erase(0, nl + 1);
      return true;
    }
    char tmp[16 * 1024];
    int n = ReadBody(tmp, sizeof(tmp));
    if (n <= 0) return false;
    line_.append(tmp, n);
  }
}

bool Conn::ReadAll(std::string& body, size_t limit) {
  char tmp[16 * 1024];
  for (;;) {
    int n = ReadBody(tmp, sizeof(tmp));
    if (n < 0) return false;
    if (n == 0) return true;
    body.append(tmp, n);
    if (body.size() > limit) return false;
  }
}

Result Send(const char* method, const std::string& path, const std::string& body, const char* contentType,
            HANDLE cancel) {
  Result r;
  Conn c(cancel);
  if (!c.Open()) return r;
  r.connected = true;
  bool hasBody = !body.empty() || strcmp(method, "GET") != 0;
  if (!c.SendHead(method, path, hasBody ? static_cast<int64_t>(body.size()) : -1, contentType)) return r;
  if (!body.empty() && !c.Write(body.data(), body.size())) return r;
  if (!c.ReadHead(r.status)) return r;
  c.ReadAll(r.body);
  return r;
}

Result Get(const std::string& path, HANDLE cancel) { return Send("GET", path, {}, nullptr, cancel); }

std::string EscapeSegment(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
        c == '_' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

}  // namespace lapi
