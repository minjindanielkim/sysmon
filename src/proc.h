#pragma once

#include <cstdint>
#include <string>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

// One row of the process list.
struct ProcInfo {
  pid_t pid = 0;
  std::string name;
  std::string user;
  bool readable = false;  // false: owned by root/another user, so without
                          // sudo only the name and user are available
  bool measured = false;  // false until two samples exist to compare
  double cpu = 0;         // % of one core, so it can exceed 100 (like top)
  uint64_t mem = 0;       // memory footprint in bytes (as Activity Monitor)
  int threads = 0;
};

struct ProcList {
  std::vector<ProcInfo> procs;
  int readable = 0;  // how many of procs have readable == true
  bool ok = false;   // false if the process list itself couldn't be read
};

// CPU % of one core: CPU time used between two samples divided by the wall
// time between them. Returns false when there's no honest answer: no wall
// time passed, or CPU time went *down*, which means the PID was reused by a
// new process since the last sample.
inline bool cpuPercent(uint64_t prevCpuNs, uint64_t nowCpuNs, uint64_t wallNs, double& out) {
  if (wallNs == 0 || nowCpuNs < prevCpuNs) return false;
  out = 100.0 * static_cast<double>(nowCpuNs - prevCpuNs) / static_cast<double>(wallNs);
  return true;
}

// Like CpuSampler: remembers each process's CPU time from the previous
// sample so that sample() can report usage since then.
class ProcSampler {
public:
  ProcSampler();
  ProcList sample();

private:
  const std::string& userName(uid_t uid);

  std::unordered_map<pid_t, uint64_t> prevCpuNs_;  // per process, from last sample
  uint64_t prevWallNs_ = 0;
  uint32_t tbNumer_ = 1, tbDenom_ = 1;  // CPU time units -> ns (125/3 on Apple Silicon)
  std::unordered_map<uid_t, std::string> users_;  // uid -> name cache
};
