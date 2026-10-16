#include "pi_paths.h"

#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

static bool usable(const std::string& d) {
  mkdir(d.c_str(), 0755);
  return access(d.c_str(), W_OK) == 0;
}

const std::string& piDataDir() {
  static const std::string dir = [] {
    const char* env = getenv("SPOOLMANSCALE_DATA");
    if (env && *env && usable(env)) return std::string(env);
    const char* st = getenv("STATE_DIRECTORY");   // set by systemd
    if (st && *st && usable(st)) return std::string(st);
    if (usable("/var/lib/spoolmanscale")) return std::string("/var/lib/spoolmanscale");
    const char* home = getenv("HOME");
    std::string h = std::string(home ? home : "/tmp") + "/.spoolmanscale";
    usable(h);
    return h;
  }();
  return dir;
}

std::string piDataPath(const char* name) { return piDataDir() + "/" + name; }
