#include "stats.h"
#include <cstdio>

int main() {

  CpuInfo c = getCpuInfo();

  if (c.ok) {
      std::printf("CPU: %s (%d cores) %.1f%%\n", c.model.c_str(), c.cores, c.usage);
  } else {
      std::puts("CPU: N/A");
  }

  DiskInfo d = getDiskInfo("/System/Volumes/Data");

  if(!d.ok) {
    std::puts("Disk: N/A");
    return 1;
  }

  const double GB = 1024.0 * 1024 * 1024;

  std::printf("Disk {%s}: %.1f / %.1f GB (%.0f%%)\n",
      d.mount.c_str(),
      d.used / GB,
      d.total / GB,
      100.0 * d.used / d.total);

  MemInfo m = getMemInfo();
  if (m.ok) {
      std::printf("RAM: %.1f / %.1f GB (%.0f%%)\n",
                  m.used / GB, m.total / GB, 100.0 * m.used / m.total);
  } else {
      std::puts("RAM: N/A");
  }  
  return 0;
}

