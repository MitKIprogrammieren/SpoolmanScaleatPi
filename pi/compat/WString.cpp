#include "WString.h"

#include <cctype>
#include <cstdio>

bool String::equalsIgnoreCase(const String& o) const {
  if (s_.size() != o.s_.size()) return false;
  for (size_t i = 0; i < s_.size(); i++)
    if (tolower((unsigned char)s_[i]) != tolower((unsigned char)o.s_[i])) return false;
  return true;
}

void String::getBytes(unsigned char* buf, unsigned int size, unsigned int idx) const {
  if (!buf || size == 0) return;
  if (idx >= s_.size()) { buf[0] = 0; return; }
  size_t n = s_.size() - idx;
  if (n > size - 1) n = size - 1;
  memcpy(buf, s_.data() + idx, n);
  buf[n] = 0;
}

String String::substring(unsigned int from, unsigned int to) const {
  if (from > to) { unsigned int t = from; from = to; to = t; }
  if (from >= s_.size()) return String();
  if (to > s_.size()) to = (unsigned int)s_.size();
  return String(s_.substr(from, to - from));
}

void String::replace(const String& a, const String& b) {
  if (a.s_.empty()) return;
  size_t p = 0;
  while ((p = s_.find(a.s_, p)) != std::string::npos) {
    s_.replace(p, a.s_.size(), b.s_);
    p += b.s_.size();
  }
}

void String::toLowerCase() { for (auto& c : s_) c = (char)tolower((unsigned char)c); }
void String::toUpperCase() { for (auto& c : s_) c = (char)toupper((unsigned char)c); }

void String::trim() {
  size_t b = 0, e = s_.size();
  while (b < e && isspace((unsigned char)s_[b])) b++;
  while (e > b && isspace((unsigned char)s_[e - 1])) e--;
  s_ = s_.substr(b, e - b);
}

void String::fromUnsigned(unsigned long long v, unsigned char base) {
  if (base < 2 || base > 36) base = 10;
  char buf[72];
  int i = 70;
  buf[71] = 0;
  do {
    int d = (int)(v % base);
    buf[i--] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    v /= base;
  } while (v && i >= 0);
  s_ = &buf[i + 1];
}

void String::fromSigned(long long v, unsigned char base) {
  if (base == 10 && v < 0) {
    fromUnsigned((unsigned long long)(-(v + 1)) + 1, 10);
    s_.insert(s_.begin(), '-');
  } else {
    fromUnsigned((unsigned long long)v, base);
  }
}

void String::fromDouble(double v, unsigned int decimals) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.*f", (int)decimals, v);
  s_ = buf;
}
