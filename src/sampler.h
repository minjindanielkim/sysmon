#pragma once

#include "stats.h"
#include <string>

// Everything the UI needs for one frame.
struct Snapshot {
  CpuInfo cpu{};
  MemInfo mem{};
  DiskInfo disk{};
  SysInfo sys{};
};

// Collects a fresh Snapshot on each tick(). The main loop decides how often
// to call it; history buffers for graphs will live here in phase 2.
class Sampler {
public:
  explicit Sampler(std::string diskPath);

  void tick();
  const Snapshot& latest() const { return latest_; }

private:
  CpuSampler cpu_;
  std::string diskPath_;
  Snapshot latest_;
};
