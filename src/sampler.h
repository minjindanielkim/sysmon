#pragma once

#include "net.h"
#include "proc.h"
#include "stats.h"

#include <cstddef>
#include <deque>
#include <string>

// Recent readings for the graphs, oldest first. Kept by sample count, not
// time, so changing the interval changes how much time a graph spans.
struct History {
  static constexpr size_t kMax = 1024;  // enough for 2 samples per column of a very wide terminal
  std::deque<double> cpu;  // usage, 0..1
  std::deque<double> mem;  // fraction used, 0..1
  std::deque<double> rx;   // bytes per second received
  std::deque<double> tx;   // bytes per second sent
};

// Everything the UI needs for one frame.
struct Snapshot {
  CpuInfo cpu{};
  MemInfo mem{};
  DiskInfo disk{};
  NetInfo net{};
  SysInfo sys{};
  ProcList procs{};
  History history{};
};

// Collects a fresh Snapshot on each tick(). The main loop decides how often
// to call it. Each tick also appends to the graph history.
class Sampler {
public:
  explicit Sampler(std::string diskPath);

  void tick();
  const Snapshot& latest() const { return latest_; }

private:
  CpuSampler cpu_;
  NetSampler net_;
  ProcSampler proc_;
  std::string diskPath_;
  Snapshot latest_;
};
