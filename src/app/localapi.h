// Minimal HTTP/1.1 client for tailscaled's LocalAPI named pipe.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace lapi {

// One request/response exchange over \\.\pipe\...\tailscaled. All I/O is
// overlapped so an optional cancel event can abort a blocked read or write.
class Conn {
 public:
  explicit Conn(HANDLE cancel = nullptr) : cancel_(cancel) {}
  ~Conn();
  Conn(const Conn&) = delete;
  Conn& operator=(const Conn&) = delete;

  bool Open();
  bool Write(const void* data, size_t len);
  // Writes the request line and headers. contentLength < 0 sends no body.
  bool SendHead(const char* method, const std::string& path, int64_t contentLength,
                const char* contentType = nullptr);
  bool ReadHead(int& status);
  // Decoded body bytes: >0 count, 0 at end of body, -1 on error.
  int ReadBody(char* buf, int cap);
  // One '\n'-terminated line of the body, newline stripped.
  bool ReadLine(std::string& line);
  bool ReadAll(std::string& body, size_t limit = 16 << 20);
  DWORD error() const { return err_; }

 private:
  int RawRead(char* buf, int cap);
  bool Fill();
  bool Wait(OVERLAPPED& ov, DWORD& n);

  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE event_ = nullptr;
  HANDLE cancel_;
  std::string buf_;
  size_t pos_ = 0;
  std::string line_;
  bool chunked_ = false;
  bool eof_ = false;
  bool bodyDone_ = false;
  bool needChunkHeader_ = false;
  int64_t remaining_ = -1;
  DWORD err_ = 0;
};

struct Result {
  bool connected = false;  // false when the pipe could not be opened at all
  int status = 0;
  std::string body;
  bool ok() const { return connected && status >= 200 && status < 300; }
};

Result Get(const std::string& path, HANDLE cancel = nullptr);
Result Send(const char* method, const std::string& path, const std::string& body,
            const char* contentType = nullptr, HANDLE cancel = nullptr);

// Percent-escapes a UTF-8 string for use as one URL path segment.
std::string EscapeSegment(const std::string& utf8);

}  // namespace lapi
