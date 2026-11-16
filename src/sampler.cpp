#include "sampler.h"

#include <utility>

namespace {

void push(std::deque<double>& series, double value) {
  series.push_back(value);
  if (series.size() > History::kMax) series.pop_front();
}

}  // namespace

Sampler::Sampler(std::string diskPath) : diskPath_(std::move(diskPath)) {}

void Sampler::tick() {
  latest_.cpu  = cpu_.sample();
  latest_.mem  = getMemInfo();
  latest_.disk = getDiskInfo(diskPath_);
  latest_.net  = net_.sample();
  latest_.sys  = getSysInfo();
  latest_.procs = proc_.sample();

  // Only real readings go into the graphs: the first CPU and network samples
  // are just baselines, and a failed read would draw as a misleading zero
  History& h = latest_.history;
  const Snapshot& s = latest_;
  if (s.cpu.ok && s.cpu.measured) push(h.cpu, s.cpu.usage / 100.0);
  if (s.mem.ok && s.mem.total > 0) push(h.mem, static_cast<double>(s.mem.used) / s.mem.total);
  if (s.net.ok && s.net.measured) {
    push(h.rx, s.net.rxRate);
    push(h.tx, s.net.txRate);
  }
}
