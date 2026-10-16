// Arduino String for Linux, backed by std::string.
// Only the API this firmware uses, with Arduino's semantics: indices are
// unsigned, a missing match is -1, out-of-range substring() clamps.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

class __FlashStringHelper;

class String {
 public:
  String() {}
  String(const char* s) { if (s) s_ = s; }
  String(const char* s, size_t n) { if (s) s_.assign(s, n); }
  String(const std::string& s) : s_(s) {}
  String(const String& o) = default;
  String(String&& o) noexcept = default;
  String(const __FlashStringHelper* s) { if (s) s_ = reinterpret_cast<const char*>(s); }
  explicit String(char c) : s_(1, c) {}
  explicit String(unsigned char v, unsigned char base = 10) { fromUnsigned(v, base); }
  explicit String(int v, unsigned char base = 10) { fromSigned(v, base); }
  explicit String(unsigned int v, unsigned char base = 10) { fromUnsigned(v, base); }
  explicit String(long v, unsigned char base = 10) { fromSigned(v, base); }
  explicit String(unsigned long v, unsigned char base = 10) { fromUnsigned(v, base); }
  explicit String(long long v, unsigned char base = 10) { fromSigned(v, base); }
  explicit String(unsigned long long v, unsigned char base = 10) { fromUnsigned(v, base); }
  explicit String(float v, unsigned int decimals = 2) { fromDouble(v, decimals); }
  explicit String(double v, unsigned int decimals = 2) { fromDouble(v, decimals); }

  String& operator=(const String& o) = default;
  String& operator=(String&& o) noexcept = default;
  String& operator=(const char* s) { if (s) s_ = s; else s_.clear(); return *this; }

  // Arduino's reserve() returns success as an unsigned char.
  unsigned char reserve(size_t n) { s_.reserve(n); return 1; }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  const char* c_str() const { return s_.c_str(); }
  char* begin() { return &s_[0]; }
  char* end() { return &s_[0] + s_.size(); }
  const char* begin() const { return s_.c_str(); }
  const char* end() const { return s_.c_str() + s_.size(); }
  explicit operator bool() const { return true; }
  const std::string& str() const { return s_; }

  // concat - every overload returns success like the Arduino core.
  bool concat(const String& o) { s_ += o.s_; return true; }
  bool concat(const char* s) { if (!s) return false; s_ += s; return true; }
  bool concat(const char* s, size_t n) { if (!s) return false; s_.append(s, n); return true; }
  bool concat(const uint8_t* s, size_t n) { return concat(reinterpret_cast<const char*>(s), n); }
  bool concat(char c) { s_ += c; return true; }
  bool concat(unsigned char v) { return concat(String(v)); }
  bool concat(int v) { return concat(String(v)); }
  bool concat(unsigned int v) { return concat(String(v)); }
  bool concat(long v) { return concat(String(v)); }
  bool concat(unsigned long v) { return concat(String(v)); }
  bool concat(long long v) { return concat(String(v)); }
  bool concat(unsigned long long v) { return concat(String(v)); }
  bool concat(float v) { return concat(String(v)); }
  bool concat(double v) { return concat(String(v)); }
  bool concat(const __FlashStringHelper* s) { return concat(reinterpret_cast<const char*>(s)); }

  template <typename T> String& operator+=(const T& v) { concat(v); return *this; }
  String& operator+=(const char* s) { concat(s); return *this; }

  bool equals(const String& o) const { return s_ == o.s_; }
  bool equals(const char* o) const { return o && s_ == o; }
  bool equalsIgnoreCase(const String& o) const;
  int compareTo(const String& o) const { return strcmp(c_str(), o.c_str()); }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return o ? s_ == o : s_.empty(); }
  bool operator!=(const String& o) const { return !(*this == o); }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return s_ < o.s_; }
  bool operator>(const String& o) const { return s_ > o.s_; }
  bool operator<=(const String& o) const { return s_ <= o.s_; }
  bool operator>=(const String& o) const { return s_ >= o.s_; }

  bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0 && s_.size() >= p.s_.size(); }
  bool startsWith(const String& p, unsigned int off) const {
    return off <= s_.size() && s_.compare(off, p.s_.size(), p.s_) == 0 && s_.size() - off >= p.s_.size();
  }
  bool endsWith(const String& p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }

  char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
  void setCharAt(unsigned int i, char c) { if (i < s_.size()) s_[i] = c; }
  char operator[](unsigned int i) const { return charAt(i); }
  char& operator[](unsigned int i) { static char dummy; if (i >= s_.size()) { dummy = 0; return dummy; } return s_[i]; }
  void getBytes(unsigned char* buf, unsigned int size, unsigned int idx = 0) const;
  void toCharArray(char* buf, unsigned int size, unsigned int idx = 0) const {
    getBytes(reinterpret_cast<unsigned char*>(buf), size, idx);
  }

  int indexOf(char c) const { return indexOf(c, 0); }
  int indexOf(char c, unsigned int from) const { return pos(s_.find(c, from)); }
  int indexOf(const String& s) const { return indexOf(s, 0); }
  int indexOf(const String& s, unsigned int from) const { return pos(s_.find(s.s_, from)); }
  int indexOf(const char* s) const { return indexOf(String(s), 0); }
  int indexOf(const char* s, unsigned int from) const { return indexOf(String(s), from); }
  int lastIndexOf(char c) const { return pos(s_.rfind(c)); }
  int lastIndexOf(char c, unsigned int from) const { return pos(s_.rfind(c, from)); }
  int lastIndexOf(const String& s) const { return pos(s_.rfind(s.s_)); }
  int lastIndexOf(const String& s, unsigned int from) const { return pos(s_.rfind(s.s_, from)); }

  String substring(unsigned int from) const { return substring(from, (unsigned int)s_.size()); }
  String substring(unsigned int from, unsigned int to) const;

  void replace(char a, char b) { for (auto& c : s_) if (c == a) c = b; }
  void replace(const String& a, const String& b);
  void remove(unsigned int idx) { if (idx < s_.size()) s_.erase(idx); }
  void remove(unsigned int idx, unsigned int count) { if (idx < s_.size()) s_.erase(idx, count); }
  void toLowerCase();
  void toUpperCase();
  void trim();

  long toInt() const { return atol(c_str()); }
  float toFloat() const { return (float)atof(c_str()); }
  double toDouble() const { return atof(c_str()); }

 private:
  static int pos(size_t p) { return p == std::string::npos ? -1 : (int)p; }
  void fromSigned(long long v, unsigned char base);
  void fromUnsigned(unsigned long long v, unsigned char base);
  void fromDouble(double v, unsigned int decimals);
  std::string s_;
};

// Arduino has StringSumHelper; a plain String does the same job here.
typedef String StringSumHelper;

inline String operator+(const String& a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, const char* b) { String r(a); r.concat(b); return r; }
inline String operator+(const char* a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, char b) { String r(a); r.concat(b); return r; }
inline String operator+(char a, const String& b) { String r; r.concat(a); r.concat(b); return r; }
inline String operator+(const String& a, int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, long long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned long long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, float b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, double b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, const __FlashStringHelper* b) { String r(a); r.concat(b); return r; }
inline bool operator==(const char* a, const String& b) { return b == a; }
inline bool operator!=(const char* a, const String& b) { return b != a; }
