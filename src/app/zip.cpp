#include "zip.h"

#include "util.h"

namespace {

constexpr uint32_t kMax32 = 0xFFFFFFFFu;
constexpr uint16_t kFlags = (1 << 3) | (1 << 11);  // data descriptor, UTF-8 names

uint32_t Crc32(uint32_t crc, const uint8_t* p, size_t n) {
  static uint32_t table[256] = {};
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
  }
  crc = ~crc;
  while (n--) crc = table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

struct Buf {
  std::vector<uint8_t> b;
  void u16(uint32_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    u16(v & 0xFFFF);
    u16(v >> 16);
  }
  void u64(uint64_t v) {
    u32(static_cast<uint32_t>(v));
    u32(static_cast<uint32_t>(v >> 32));
  }
  void str(const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }
};

int64_t LocalSize(size_t nameLen, bool big, bool dir) {
  return 30 + static_cast<int64_t>(nameLen) + (big ? 20 : 0) + (dir ? 0 : (big ? 24 : 16));
}

int64_t CentralSize(size_t nameLen, bool big, bool bigOffset) {
  int extra = (big ? 16 : 0) + (bigOffset ? 8 : 0);
  return 46 + static_cast<int64_t>(nameLen) + (extra ? 4 + extra : 0);
}

}  // namespace

void ZipStream::Scan(const std::wstring& dir, const std::string& prefix) {
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(util::Join(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                              FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    std::wstring name = fd.cFileName;
    if (name == L"." || name == L"..") continue;
    // Skip links and junctions: they can loop and aren't portable anyway.
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
    Entry e;
    e.name = prefix + util::Narrow(name);
    FILETIME local;
    FileTimeToLocalFileTime(&fd.ftLastWriteTime, &local);
    FileTimeToDosDateTime(&local, &e.date, &e.time);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      e.dir = true;
      e.name += "/";
      std::string childPrefix = e.name;
      entries_.push_back(std::move(e));
      Scan(util::Join(dir, name), childPrefix);
    } else {
      e.path = util::Join(dir, name);
      e.size = (static_cast<int64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
      e.big = e.size >= kMax32;
      entries_.push_back(std::move(e));
    }
  } while (FindNextFileW(h, &fd));
  FindClose(h);
}

bool ZipStream::Prepare(const std::wstring& folder) {
  entries_.clear();
  if (!util::IsDirectory(folder)) return false;
  Entry root;
  root.dir = true;
  root.name = util::Narrow(util::FileName(folder)) + "/";
  WIN32_FILE_ATTRIBUTE_DATA d;
  if (GetFileAttributesExW(folder.c_str(), GetFileExInfoStandard, &d)) {
    FILETIME local;
    FileTimeToLocalFileTime(&d.ftLastWriteTime, &local);
    FileTimeToDosDateTime(&local, &root.date, &root.time);
  }
  std::string prefix = root.name;
  entries_.push_back(std::move(root));
  Scan(folder, prefix);

  int64_t off = 0;
  for (Entry& e : entries_) {
    e.offset = off;
    off += LocalSize(e.name.size(), e.big, e.dir) + e.size;
  }
  int64_t cdStart = off, cd = 0;
  for (const Entry& e : entries_) cd += CentralSize(e.name.size(), e.big, e.offset >= kMax32);
  bool zip64 = entries_.size() >= 0xFFFF || cdStart >= kMax32 || cd >= kMax32;
  total_ = cdStart + cd + (zip64 ? 56 + 20 : 0) + 22;
  return true;
}

bool ZipStream::Write(const std::function<bool(const void*, size_t)>& sink, std::wstring* error) {
  std::vector<uint8_t> data(256 * 1024);
  for (Entry& e : entries_) {
    Buf h;
    h.u32(0x04034b50);
    h.u16(e.big ? 45 : 20);
    h.u16(e.dir ? (1 << 11) : kFlags);
    h.u16(0);  // stored
    h.u16(e.time);
    h.u16(e.date);
    h.u32(0);  // crc and sizes follow in the data descriptor
    h.u32(e.big ? kMax32 : 0);
    h.u32(e.big ? kMax32 : 0);
    h.u16(static_cast<uint16_t>(e.name.size()));
    h.u16(e.big ? 20 : 0);
    h.str(e.name);
    if (e.big) {
      h.u16(0x0001);
      h.u16(16);
      h.u64(0);
      h.u64(0);
    }
    if (!sink(h.b.data(), h.b.size())) return false;
    if (e.dir) continue;

    HANDLE f = CreateFileW(e.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
      if (error) *error = util::Widen(e.name);
      return false;
    }
    uint32_t crc = 0;
    int64_t left = e.size;
    bool ok = true;
    while (left > 0) {
      DWORD n = 0;
      DWORD want = static_cast<DWORD>(left < static_cast<int64_t>(data.size()) ? left : data.size());
      if (!ReadFile(f, data.data(), want, &n, nullptr) || n == 0) {
        ok = false;
        if (error) *error = util::Widen(e.name);
        break;
      }
      crc = Crc32(crc, data.data(), n);
      if (!sink(data.data(), n)) {
        ok = false;
        break;
      }
      left -= n;
    }
    CloseHandle(f);
    if (!ok) return false;
    e.crc = crc;
    Buf dd;
    dd.u32(0x08074b50);
    dd.u32(crc);
    if (e.big) {
      dd.u64(e.size);
      dd.u64(e.size);
    } else {
      dd.u32(static_cast<uint32_t>(e.size));
      dd.u32(static_cast<uint32_t>(e.size));
    }
    if (!sink(dd.b.data(), dd.b.size())) return false;
  }

  Buf cd;
  int64_t cdStart = entries_.empty() ? 0 : entries_.back().offset + LocalSize(entries_.back().name.size(),
                                                                                 entries_.back().big,
                                                                                 entries_.back().dir) +
                                                 entries_.back().size;
  for (const Entry& e : entries_) {
    bool bigOff = e.offset >= kMax32;
    int extra = (e.big ? 16 : 0) + (bigOff ? 8 : 0);
    cd.u32(0x02014b50);
    cd.u16(45);  // made by: MS-DOS, spec 4.5
    cd.u16(e.big || bigOff ? 45 : 20);
    cd.u16(e.dir ? (1 << 11) : kFlags);
    cd.u16(0);
    cd.u16(e.time);
    cd.u16(e.date);
    cd.u32(e.crc);
    cd.u32(e.big ? kMax32 : static_cast<uint32_t>(e.size));
    cd.u32(e.big ? kMax32 : static_cast<uint32_t>(e.size));
    cd.u16(static_cast<uint16_t>(e.name.size()));
    cd.u16(static_cast<uint16_t>(extra ? 4 + extra : 0));
    cd.u16(0);                    // comment
    cd.u16(0);                    // disk
    cd.u16(0);                    // internal attributes
    cd.u32(e.dir ? 0x10 : 0x20);  // FILE_ATTRIBUTE_DIRECTORY / ARCHIVE
    cd.u32(bigOff ? kMax32 : static_cast<uint32_t>(e.offset));
    cd.str(e.name);
    if (extra) {
      cd.u16(0x0001);
      cd.u16(static_cast<uint16_t>(extra));
      if (e.big) {
        cd.u64(e.size);
        cd.u64(e.size);
      }
      if (bigOff) cd.u64(e.offset);
    }
  }
  int64_t cdSize = static_cast<int64_t>(cd.b.size());
  bool zip64 = entries_.size() >= 0xFFFF || cdStart >= kMax32 || cdSize >= kMax32;
  if (zip64) {
    int64_t eocd64 = cdStart + cdSize;
    cd.u32(0x06064b50);
    cd.u64(44);
    cd.u16(45);
    cd.u16(45);
    cd.u32(0);
    cd.u32(0);
    cd.u64(entries_.size());
    cd.u64(entries_.size());
    cd.u64(cdSize);
    cd.u64(cdStart);
    cd.u32(0x07064b50);
    cd.u32(0);
    cd.u64(eocd64);
    cd.u32(1);
  }
  cd.u32(0x06054b50);
  cd.u16(0);
  cd.u16(0);
  uint16_t n16 = entries_.size() >= 0xFFFF ? 0xFFFF : static_cast<uint16_t>(entries_.size());
  cd.u16(n16);
  cd.u16(n16);
  cd.u32(cdSize >= kMax32 ? kMax32 : static_cast<uint32_t>(cdSize));
  cd.u32(cdStart >= kMax32 ? kMax32 : static_cast<uint32_t>(cdStart));
  cd.u16(0);
  return sink(cd.b.data(), cd.b.size());
}
