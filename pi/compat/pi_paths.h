// Where the Pi build keeps its state. SPOOLMANSCALE_DATA overrides; the
// default is /var/lib/spoolmanscale (systemd StateDirectory) or, when that is
// not writable, ~/.spoolmanscale.
#pragma once
#include <string>
const std::string& piDataDir();
std::string piDataPath(const char* name);
