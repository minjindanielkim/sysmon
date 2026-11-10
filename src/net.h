#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Network traffic summed over the physical interfaces (Wi-Fi, Ethernet).
struct NetInfo {
  double rxRate = 0;     // bytes per second received over the last interval
  double txRate = 0;     // bytes per second sent
  uint64_t rxTotal = 0;  // bytes received since each interface came up
  uint64_t txTotal = 0;
  std::vector<std::string> names;  // counted interfaces that have seen traffic
  bool ok = false;
  bool measured = false;  // false until two samples exist to diff
};

// Rates need two readings, so this keeps the previous counters per interface.
class NetSampler {
public:
  NetInfo sample();

private:
  struct Counters { uint64_t rx, tx; };
  std::map<unsigned, Counters> prev_;  // keyed by interface index
  std::chrono::steady_clock::time_point prevTime_;
  bool havePrev_ = false;
};
