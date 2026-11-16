#include "net.h"

#include <net/if.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace {

// Physical links only, for the same reason as on macOS: loopback isn't real
// traffic, and VPN tunnels (wg0, tun0), Docker bridges and veth pairs carry
// packets that also cross a physical interface. Real network cards have a
// "device" link in sysfs pointing at their PCI/USB hardware; virtual ones don't.
bool counted(const std::string& name) {
  return access(("/sys/class/net/" + name + "/device").c_str(), F_OK) == 0;
}

}  // namespace

// /proc/net/dev looks like:
//   Inter-|   Receive                            |  Transmit
//    face |bytes    packets errs drop fifo ...   |bytes    packets ...
//    wlan0: 123456789  98765 0 0 0 0 0 0  23456789  54321 0 0 0 0 0 0
// 8 receive fields, then 8 transmit fields. The counters are 64-bit.
NetInfo NetSampler::sample() {
  NetInfo info;
  auto now = std::chrono::steady_clock::now();

  std::ifstream f("/proc/net/dev");
  if (!f) return info;

  std::string line;
  std::getline(f, line);  // skip the two header lines
  std::getline(f, line);

  std::map<unsigned, Counters> cur;
  while (std::getline(f, line)) {
    auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    auto start = line.find_first_not_of(' ');
    if (start == std::string::npos || start >= colon) continue;
    std::string name = line.substr(start, colon - start);

    if (!counted(name)) continue;
    unsigned index = if_nametoindex(name.c_str());
    if (index == 0) continue;  // interface vanished meanwhile

    // fields 0..7 = receive (bytes first), 8..15 = transmit (bytes first)
    std::istringstream nums(line.substr(colon + 1));
    uint64_t v[9];
    bool parsed = true;
    for (auto& x : v) {
      if (!(nums >> x)) { parsed = false; break; }
    }
    if (!parsed) continue;

    Counters c{v[0], v[8]};
    cur[index] = c;
    info.rxTotal += c.rx;
    info.txTotal += c.tx;
    if (c.rx || c.tx) info.names.push_back(name);
  }
  info.ok = true;

  if (havePrev_) {
    double secs = std::chrono::duration<double>(now - prevTime_).count();
    if (secs > 0) {
      uint64_t rx = 0, tx = 0;
      for (const auto& [index, c] : cur) {
        // New interface: no baseline yet. Counter went down: interface was
        // reset. Skip it this once rather than report a huge bogus spike.
        auto it = prev_.find(index);
        if (it == prev_.end()) continue;
        if (c.rx >= it->second.rx) rx += c.rx - it->second.rx;
        if (c.tx >= it->second.tx) tx += c.tx - it->second.tx;
      }
      info.rxRate = rx / secs;
      info.txRate = tx / secs;
      info.measured = true;
    }
  }

  prev_ = std::move(cur);
  prevTime_ = now;
  havePrev_ = true;
  return info;
}
