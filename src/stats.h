#pragma once 

#include <cstdint>
#include <string>

// hold disk usage on one mounted volume 
struct DiskInfo {
  std::string mount; // find mounted partition 
  uint64_t total = 0; // total size in bytes (here we use uint64_t to signify unsigned 64 bit integer)
  uint64_t used = 0;  // total used in bytes
  bool ok = false; // false if the system call has failed 
};

// macOS's own judgement of memory health (what Activity Monitor's
// "Memory Pressure" graph shows). More meaningful than "% used", because
// macOS deliberately fills spare RAM with cache.
enum class MemPressure { Unknown, Normal, Warning, Critical };

struct MemInfo {
  uint64_t total;
  uint64_t used;
  bool ok;
  uint64_t swapTotal = 0;  // macOS grows swap on demand; 0 = no swap in use
  uint64_t swapUsed = 0;
  MemPressure pressure = MemPressure::Unknown;
};

struct CpuInfo {
    std::string model;   // e.g. "Apple M2"
    int cores;
    double usage;        // percent
    bool ok;
    bool measured = false;  // false until two readings exist to compare
};

// System-wide figures for the header line.
struct SysInfo {
  double load[3];      // 1, 5 and 15 minute load averages
  uint64_t uptimeSec;  // seconds since boot
  bool ok;
};

// CPU usage is a rate (busy ticks / total ticks over some interval), so it
// needs two readings. CpuSampler remembers the previous reading, and each
// sample() call reports usage since the last call, with no sleeping.
class CpuSampler {
public:
  CpuSampler();      // reads model + core count
  CpuInfo sample();  // usage since the previous call; the first call only
                     // records a baseline and returns measured = false

private:
  std::string model_;
  int cores_ = 0;
#ifdef __APPLE__
  static constexpr int kStates = 4;   // user, system, idle, nice
  uint32_t prevTicks_[kStates] = {};  // 32-bit kernel counters, diffed per state
#else
  uint64_t prevBusy_ = 0;             // /proc/stat counters are 64-bit
  uint64_t prevTotal_ = 0;
#endif
  bool havePrev_ = false;
  CpuInfo last_{};  // returned again when the counters haven't moved
};

DiskInfo getDiskInfo(const std::string& path);
MemInfo getMemInfo();
SysInfo getSysInfo();
