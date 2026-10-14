// Streams a folder as a ZIP archive (stored entries, UTF-8 names, ZIP64 when
// needed) without a temporary file. The exact archive size is known before
// writing, so it can be sent with a Content-Length.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class ZipStream {
 public:
  // Scans `folder`; entries are named "<folder name>/...". False if unreadable.
  bool Prepare(const std::wstring& folder);
  int64_t size() const { return total_; }
  size_t count() const { return entries_.size(); }
  // Writes the archive through `sink`; stops (returning false) when the sink
  // fails or a file can't be read (its entry name goes to *failedEntry).
  bool Write(const std::function<bool(const void*, size_t)>& sink, std::wstring* failedEntry);

 private:
  struct Entry {
    std::wstring path;  // empty for directories
    std::string name;   // UTF-8, '/'-separated
    int64_t size = 0;
    uint16_t time = 0, date = 0;
    bool dir = false;
    bool big = false;
    uint32_t crc = 0;
    int64_t offset = 0;
  };
  void Scan(const std::wstring& dir, const std::string& prefix);
  bool scanFailed_ = false;
  std::vector<Entry> entries_;
  int64_t total_ = 0;
};
