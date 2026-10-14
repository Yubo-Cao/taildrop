// A small DOM JSON parser; enough for LocalAPI responses.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace json {

struct Member;

struct Value {
  enum Type : uint8_t { Null, Bool, Number, String, Array, Object };
  Type type = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Value> items;     // Array
  std::vector<Member> members;  // Object

  const Value& operator[](std::string_view key) const;
  const Value& at(size_t i) const;
  bool has(std::string_view key) const;
  size_t size() const;
  bool isNull() const { return type == Null; }
  const std::string& str() const;
  double num(double fallback = 0) const { return type == Number ? n : fallback; }
  int64_t i64(int64_t fallback = 0) const { return type == Number ? static_cast<int64_t>(n) : fallback; }
  bool boolean(bool fallback = false) const { return type == Bool ? b : fallback; }
};

struct Member {
  std::string key;
  Value value;
};

bool Parse(std::string_view text, Value& out);

}  // namespace json
