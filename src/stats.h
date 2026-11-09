#pragma once 

#include <cstdint>
#include <string>

// hold disk usage on one mounted volume 
struct DiskInfo {
  std::string mount; // find mounted partition 
  uint64_t total; // total size in bytes (here we use uint64_t to signify unsigned 64 bit integer)
  uint64_t used;  // total used in bytes
  bool ok; // false if the system call has failed 
};

struct MemInfo {
  uint64_t total;
  uint64_t used;
  bool ok;
};

struct CpuInfo {
    std::string model;   // e.g. "Apple M2"
    int cores;
    double usage;        // percent
    bool ok;
};


DiskInfo getDiskInfo(const std::string& path);
MemInfo getMemInfo();
CpuInfo getCpuInfo();

