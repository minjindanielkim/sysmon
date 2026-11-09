#include <iostream>
#include "stats.h"
#include <sys/statvfs.h>

DiskInfo getDiskInfo(const std::string& path) {
  struct statvfs s;

  // returns 0 on success and -1 in failure
  if(statvfs(path.c_str(), &s) != 0) {
    return {path, 0, 0, false};
  }


  // sizes are reported in blocks and not in bytes 
  uint64_t blockSize = s.f_frsize;
  uint64_t total = s.f_blocks * blockSize; // all blocks 
  uint64_t free = s.f_bfree * blockSize; // unused blocks
                                         
  return {path, total, total-free, true};
}
