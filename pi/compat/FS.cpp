#include "FS.h"
#include "SD.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>

#include "pi_paths.h"

SDFS SD;
SPIClass SPI;

namespace fs {
struct File::H {
  FILE* f = nullptr;
  DIR* d = nullptr;
  bool dir = false;
  int peeked = -1;
  ~H() {
    if (f) fclose(f);
    if (d) closedir(d);
  }
};

File::File(const std::string& real, const std::string& vpath, const char* mode)
    : real_(real), vpath_(vpath) {
  size_t s = vpath_.rfind('/');
  name_ = s == std::string::npos ? vpath_ : vpath_.substr(s + 1);
  struct stat st;
  if (stat(real.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
    auto h = std::make_shared<H>();
    h->dir = true;
    h->d = opendir(real.c_str());
    if (h->d) h_ = h;
    return;
  }
  const char* m = (strcmp(mode, "a") == 0) ? "a+" : (strcmp(mode, "w") == 0 ? "w+" : "r");
  FILE* f = fopen(real.c_str(), m);
  if (!f) return;
  auto h = std::make_shared<H>();
  h->f = f;
  h_ = h;
}

size_t File::write(const uint8_t* buf, size_t n) { return (h_ && h_->f) ? fwrite(buf, 1, n, h_->f) : 0; }
int File::available() {
  if (!h_ || !h_->f) return 0;
  long pos = ftell(h_->f);
  long sz = (long)size();
  return pos >= 0 && sz > pos ? (int)(sz - pos) + (h_->peeked >= 0) : (h_->peeked >= 0);
}
int File::read() {
  uint8_t c;
  return read(&c, 1) == 1 ? c : -1;
}
int File::read(uint8_t* buf, size_t n) {
  if (!h_ || !h_->f || !n) return -1;
  size_t got = 0;
  if (h_->peeked >= 0) { buf[got++] = (uint8_t)h_->peeked; h_->peeked = -1; }
  got += fread(buf + got, 1, n - got, h_->f);
  return got ? (int)got : -1;
}
int File::peek() {
  if (!h_ || !h_->f) return -1;
  if (h_->peeked < 0) { int c = fgetc(h_->f); h_->peeked = c == EOF ? -1 : c; }
  return h_->peeked;
}
void File::flush() { if (h_ && h_->f) fflush(h_->f); }
bool File::seek(uint32_t pos, SeekMode mode) {
  if (!h_ || !h_->f) return false;
  h_->peeked = -1;
  return fseek(h_->f, (long)pos, mode == SeekSet ? SEEK_SET : mode == SeekCur ? SEEK_CUR : SEEK_END) == 0;
}
size_t File::position() const {
  if (!h_ || !h_->f) return 0;
  long p = ftell(h_->f);
  return p < 0 ? 0 : (size_t)p - (h_->peeked >= 0);
}
size_t File::size() const {
  if (h_ && h_->f) fflush(h_->f);
  struct stat st;
  return stat(real_.c_str(), &st) == 0 ? (size_t)st.st_size : 0;
}
void File::close() { h_.reset(); }
const char* File::name() const { return name_.c_str(); }
bool File::isDirectory() const { return h_ && h_->dir; }

File File::openNextFile(const char* mode) {
  if (!h_ || !h_->d) return File();
  while (struct dirent* e = readdir(h_->d)) {
    if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
    if (e->d_name[0] == '.') continue;
    std::string v = vpath_ == "/" ? "/" + std::string(e->d_name) : vpath_ + "/" + e->d_name;
    return File(real_ + "/" + e->d_name, v, mode);
  }
  return File();
}
void File::rewindDirectory() { if (h_ && h_->d) rewinddir(h_->d); }
time_t File::getLastWrite() {
  struct stat st;
  return stat(real_.c_str(), &st) == 0 ? st.st_mtime : 0;
}

std::string FS::real(const char* path) const {
  std::string root = piDataPath(root_name_.c_str());
  ::mkdir(root.c_str(), 0755);
  std::string p = path ? path : "/";
  // No way out of the root.
  if (p.find("..") != std::string::npos) p = "/";
  if (p.empty() || p[0] != '/') p = "/" + p;
  return p == "/" ? root : root + p;
}

File FS::open(const char* path, const char* mode, bool) {
  std::string v = path && *path ? path : "/";
  if (v[0] != '/') v = "/" + v;
  return File(real(v.c_str()), v, mode ? mode : "r");
}
bool FS::exists(const char* path) {
  struct stat st;
  return stat(real(path).c_str(), &st) == 0;
}
bool FS::remove(const char* path) { return ::unlink(real(path).c_str()) == 0; }
bool FS::rename(const char* a, const char* b) { return ::rename(real(a).c_str(), real(b).c_str()) == 0; }
bool FS::mkdir(const char* path) { return ::mkdir(real(path).c_str(), 0755) == 0; }
bool FS::rmdir(const char* path) { return ::rmdir(real(path).c_str()) == 0; }
}  // namespace fs

bool SDFS::begin(uint8_t, SPIClass&, uint32_t, const char*, uint8_t, bool) {
  std::string r = real("/");
  return access(r.c_str(), W_OK) == 0;
}
uint64_t SDFS::cardSize() {
  struct statvfs v;
  if (statvfs(real("/").c_str(), &v) != 0) return 0;
  return (uint64_t)v.f_blocks * v.f_frsize;
}
uint64_t SDFS::usedBytes() {
  struct statvfs v;
  if (statvfs(real("/").c_str(), &v) != 0) return 0;
  return (uint64_t)(v.f_blocks - v.f_bfree) * v.f_frsize;
}
