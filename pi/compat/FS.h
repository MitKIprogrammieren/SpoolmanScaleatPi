// Arduino FS File on top of a directory in the data dir.
#pragma once
#include <memory>
#include <string>
#include "Arduino.h"

#define FILE_READ "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a"

namespace fs {
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

class File : public Stream {
 public:
  File() {}
  File(const std::string& real, const std::string& vpath, const char* mode);
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* buf, size_t n) override;
  using Print::write;
  int available() override;
  int read() override;
  int read(uint8_t* buf, size_t n);
  using Stream::readBytes;
  size_t readBytes(char* buf, size_t len) override { int r = read((uint8_t*)buf, len); return r > 0 ? (size_t)r : 0; }
  int peek() override;
  void flush() override;
  bool seek(uint32_t pos, SeekMode mode = SeekSet);
  size_t position() const;
  size_t size() const;
  void close();
  operator bool() const { return (bool)h_; }
  const char* name() const;
  const char* path() const { return vpath_.c_str(); }
  bool isDirectory() const;
  File openNextFile(const char* mode = FILE_READ);
  void rewindDirectory();
  time_t getLastWrite();
 private:
  struct H;
  std::shared_ptr<H> h_;
  std::string real_, vpath_, name_;
};

class FS {
 public:
  explicit FS(std::string root_name) : root_name_(std::move(root_name)) {}
  File open(const char* path, const char* mode = FILE_READ, bool create = false);
  File open(const String& path, const char* mode = FILE_READ, bool create = false) { return open(path.c_str(), mode, create); }
  bool exists(const char* path);
  bool exists(const String& path) { return exists(path.c_str()); }
  bool remove(const char* path);
  bool remove(const String& path) { return remove(path.c_str()); }
  bool rename(const char* a, const char* b);
  bool rename(const String& a, const String& b) { return rename(a.c_str(), b.c_str()); }
  bool mkdir(const char* path);
  bool rmdir(const char* path);
  std::string real(const char* path) const;
 protected:
  std::string root_name_;
};
}  // namespace fs

using fs::File;
using fs::FS;
