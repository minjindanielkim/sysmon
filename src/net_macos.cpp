#include "net.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <net/if_types.h>

#include <utility>

namespace {

// Physical links only. Loopback isn't real traffic, and VPN tunnels (utun),
// bridges and the like carry packets that also cross a physical interface,
// so counting them would count the same bytes twice. Wi-Fi reports itself
// as Ethernet on macOS.
bool counted(const ifmibdata& d) {
  if (d.ifmd_flags & IFF_LOOPBACK) return false;
  return d.ifmd_data.ifi_type == IFT_ETHER || d.ifmd_data.ifi_type == IFT_CELLULAR;
}

}  // namespace

// Counters come from the interface MIB (net.link.generic.ifdata.<index>),
// which gives exact 64-bit byte counts. The more common route-table sysctl
// (NET_RT_IFLIST2, also behind getifaddrs) rounds them down to whole KB and
// wraps them at 4 GB for processes that aren't root.
NetInfo NetSampler::sample() {
  NetInfo info;
  auto now = std::chrono::steady_clock::now();

  struct if_nameindex* list = if_nameindex();
  if (!list) return info;

  std::map<unsigned, Counters> cur;
  for (struct if_nameindex* i = list; i->if_index != 0; ++i) {
    int mib[6] = {CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_IFDATA,
                  static_cast<int>(i->if_index), IFDATA_GENERAL};
    ifmibdata d{};
    size_t len = sizeof(d);
    if (sysctl(mib, 6, &d, &len, nullptr, 0) != 0 || !counted(d)) continue;

    Counters c{d.ifmd_data.ifi_ibytes, d.ifmd_data.ifi_obytes};
    cur[i->if_index] = c;
    info.rxTotal += c.rx;
    info.txTotal += c.tx;
    if (c.rx || c.tx) info.names.emplace_back(d.ifmd_name);
  }
  if_freenameindex(list);
  info.ok = true;

  if (havePrev_) {
    double secs = std::chrono::duration<double>(now - prevTime_).count();
    if (secs > 0) {
      uint64_t rx = 0, tx = 0;
      for (const auto& [index, c] : cur) {
        // An interface that just appeared has no baseline yet. A counter
        // that went down means the interface was reset (or its index reused):
        // skip it this once rather than report a huge bogus spike.
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
