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


// CPU usage is a rate (busy ticks / total ticks over some interval), so it
// needs two readings. CpuSampler remembers the previous reading, and each
// sample() call reports usage since the last call, with no sleeping.
class CpuSampler {
public:
  CpuSampler();      // reads model + core count and takes the first reading
  CpuInfo sample();  // usage since the previous call (0% on the very first)

private:
  static constexpr int kStates = 4;  // user, system, idle, nice
  std::string model_;
  int cores_ = 0;
  uint32_t prevTicks_[kStates] = {};
  bool havePrev_ = false;
};

DiskInfo getDiskInfo(const std::string& path);
MemInfo getMemInfo();

