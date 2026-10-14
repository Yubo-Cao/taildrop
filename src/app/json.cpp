#include "json.h"

#include <charconv>
#include <cstring>

namespace json {
namespace {

const Value kNull;
const std::string kEmpty;

struct Parser {
  const char* p;
  const char* e;
  int depth = 0;

  void ws() {
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }

  bool lit(const char* s) {
    size_t n = strlen(s);
    if (static_cast<size_t>(e - p) < n || memcmp(p, s, n) != 0) return false;
    p += n;
    return true;
  }

  static void utf8(std::string& o, uint32_t c) {
    if (c < 0x80) {
      o.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      o.push_back(static_cast<char>(0xC0 | (c >> 6)));
      o.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
      o.push_back(static_cast<char>(0xE0 | (c >> 12)));
      o.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      o.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      o.push_back(static_cast<char>(0xF0 | (c >> 18)));
      o.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
      o.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      o.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }

  bool hex4(uint32_t& v) {
    if (e - p < 4) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return false;
    }
    return true;
  }

  bool str(std::string& o) {
    if (p >= e || *p != '"') return false;
    ++p;
    while (p < e) {
      const char* run = p;
      while (p < e && *p != '"' && *p != '\\') ++p;
      o.append(run, p - run);
      if (p >= e) return false;
      if (*p++ == '"') return true;
      if (p >= e) return false;
      switch (*p++) {
        case '"': o.push_back('"'); break;
        case '\\': o.push_back('\\'); break;
        case '/': o.push_back('/'); break;
        case 'b': o.push_back('\b'); break;
        case 'f': o.push_back('\f'); break;
        case 'n': o.push_back('\n'); break;
        case 'r': o.push_back('\r'); break;
        case 't': o.push_back('\t'); break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            uint32_t lo;
            if (!hex4(lo)) return false;
            if (lo >= 0xDC00 && lo < 0xE000) {
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              utf8(o, 0xFFFD);
              cp = lo;
            }
          }
          utf8(o, cp);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  bool val(Value& v) {
    ws();
    if (p >= e || ++depth > 128) return false;
    bool ok = false;
    switch (*p) {
      case '{': {
        v.type = Value::Object;
        ++p;
        ws();
        if (p < e && *p == '}') {
          ++p;
          ok = true;
          break;
        }
        for (;;) {
          ws();
          v.members.emplace_back();
          Member& m = v.members.back();
          if (!str(m.key)) break;
          ws();
          if (p >= e || *p != ':') break;
          ++p;
          if (!val(m.value)) break;
          ws();
          if (p < e && *p == ',') {
            ++p;
            continue;
          }
          if (p < e && *p == '}') {
            ++p;
            ok = true;
          }
          break;
        }
        break;
      }
      case '[': {
        v.type = Value::Array;
        ++p;
        ws();
        if (p < e && *p == ']') {
          ++p;
          ok = true;
          break;
        }
        for (;;) {
          v.items.emplace_back();
          if (!val(v.items.back())) break;
          ws();
          if (p < e && *p == ',') {
            ++p;
            continue;
          }
          if (p < e && *p == ']') {
            ++p;
            ok = true;
          }
          break;
        }
        break;
      }
      case '"':
        v.type = Value::String;
        ok = str(v.s);
        break;
      case 't':
        v.type = Value::Bool;
        v.b = true;
        ok = lit("true");
        break;
      case 'f':
        v.type = Value::Bool;
        ok = lit("false");
        break;
      case 'n':
        ok = lit("null");
        break;
      default: {
        v.type = Value::Number;
        auto r = std::from_chars(p, e, v.n);
        ok = r.ec == std::errc();
        p = r.ptr;
        break;
      }
    }
    --depth;
    return ok;
  }
};

}  // namespace

const Value& Value::operator[](std::string_view key) const {
  if (type == Object)
    for (const Member& m : members)
      if (m.key == key) return m.value;
  return kNull;
}

const Value& Value::at(size_t i) const { return type == Array && i < items.size() ? items[i] : kNull; }

bool Value::has(std::string_view key) const {
  if (type == Object)
    for (const Member& m : members)
      if (m.key == key) return true;
  return false;
}

size_t Value::size() const {
  if (type == Array) return items.size();
  if (type == Object) return members.size();
  return 0;
}

const std::string& Value::str() const { return type == String ? s : kEmpty; }

bool Parse(std::string_view text, Value& out) {
  out = Value();
  Parser ps{text.data(), text.data() + text.size()};
  return ps.val(out);
}

}  // namespace json
