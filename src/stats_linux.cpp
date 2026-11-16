#include "stats.h"
#include <fstream>
#include <string>
#include <limits>
#include <thread>
#include <chrono>
#include <unistd.h>

MemInfo getMemInfo() {
  std::ifstream f("/proc/meminfo");
  if (!f) return {0, 0, false};

  uint64_t totalKb = 0;
  uint64_t availKb = 0;
  bool haveTotal = false;
  bool haveAvail = false;

  std::string key;
  uint64_t value;
  while(f >> key >> value) {
    f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    if(key == "MemTotal:") {
      totalKb = value; 
      haveTotal = true;
    } else if (key == "MemAvailable:") {
      availKb = value;
      haveAvail = true;
    }
    if (haveTotal && haveAvail) break;
  }

  if (!haveTotal || !haveAvail || availKb > totalKb) {
    return {0, 0, false};
  }

  uint64_t total = totalKb * 1024;
  uint64_t used = (totalKb - availKb) * 1024;
  return {total, used, true};
}

static bool readCpuTicks(uint64_t& busy, uint64_t& total) {
  std::ifstream f("/proc/stat");
  if(!f) return false; 

  std::string label;
  uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
  if(!(f >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal) || label != "cpu") {
    return false;
  }
  
  busy = user + nice + system + irq + softirq + steal;
  total = busy + idle + iowait; 
  return true;
}

CpuInfo getCpuInfo() {
  CpuInfo cpu {"Unknown", 0, 0.0, false};

  std::ifstream info("/proc/cpuinfo");
  std::string line;
  while(std::getline(info, line)) {
    if(line.rfind("model name", 0) == 0) {
      auto colon = line.find(':');
      if(colon != std::string::npos) {
        auto start = line.find_first_not_of(" \t", colon+1);
        if (start != std::string::npos) {
          cpu.model = line.substr(start);
        }
      }
      break;
     }
  }

  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n > 0) cpu.cores = static_cast<int>(n);

  uint64_t busy1, total1, busy2, total2;
  if (!readCpuTicks(busy1, total1)) return cpu; 
  std::this_thread::sleep_for(std::chrono::seconds(1));
  if (!readCpuTicks(busy2, total2)) return cpu;

  if (total2 <= total1 || busy2 < busy1) return cpu;
  
  uint64_t busyDiff = busy2 - busy1;
  uint64_t totalDiff = total2 - total1;

  cpu.usage = 100.0 * busyDiff / totalDiff;
  if (cpu.usage > 100.0) cpu.usage = 100.0;
  cpu.ok = true;
  return cpu;
}




















