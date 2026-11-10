#include "sampler.h"

#include <utility>

Sampler::Sampler(std::string diskPath) : diskPath_(std::move(diskPath)) {}

void Sampler::tick() {
  latest_.cpu  = cpu_.sample();
  latest_.mem  = getMemInfo();
  latest_.disk = getDiskInfo(diskPath_);
  latest_.sys  = getSysInfo();
  latest_.procs = proc_.sample();
}
