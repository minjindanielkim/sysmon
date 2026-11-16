#include "stats.h"
#include <fstream>
#include <string>
#include <limits>
#include <cstdlib>
#include <unistd.h>

// ---------------------------------------------------------------- memory

// Linux's closest equivalent to macOS memory pressure is PSI (Pressure Stall
// Information). /proc/pressure/memory looks like:
//   some avg10=0.00 avg60=0.00 avg300=0.00 total=0
//   full avg10=0.00 avg60=0.00 avg300=0.00 total=0
// "some" = % of the last 10 s that at least one task was stalled waiting for memory
// "full" = % of the last 10 s that ALL runnable tasks were stalled at once
static MemPressure readMemPressure() {
    std::ifstream f("/proc/pressure/memory");
    if (!f) return MemPressure::Unknown;   // kernel built without PSI

    double someAvg10 = -1, fullAvg10 = -1;
    std::string kind, avg10;
    while (f >> kind >> avg10) {
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        auto eq = avg10.find('=');          // "avg10=1.23" -> 1.23
        if (eq == std::string::npos) continue;
        double v = std::strtod(avg10.c_str() + eq + 1, nullptr);
        if (kind == "some")      someAvg10 = v;
        else if (kind == "full") fullAvg10 = v;
    }

    if (someAvg10 < 0) return MemPressure::Unknown;
    if (fullAvg10 >= 10.0) return MemPressure::Critical;
    if (someAvg10 >= 10.0) return MemPressure::Warning;
    return MemPressure::Normal;
}

MemInfo getMemInfo() {
    MemInfo mem{};   // value-initialized: total/used = 0, ok = false

    std::ifstream f("/proc/meminfo");
    if (!f) return mem;

    uint64_t totalKb = 0, availKb = 0, swapTotalKb = 0, swapFreeKb = 0;
    bool haveTotal = false, haveAvail = false;

    std::string key;
    uint64_t value;
    while (f >> key >> value) {
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        if (key == "MemTotal:")          { totalKb = value; haveTotal = true; }
        else if (key == "MemAvailable:") { availKb = value; haveAvail = true; }
        else if (key == "SwapTotal:")    swapTotalKb = value;
        else if (key == "SwapFree:")     swapFreeKb  = value;
    }

    if (!haveTotal || !haveAvail || availKb > totalKb) return mem;

    // "kB" in /proc/meminfo is KiB (1024 bytes)
    mem.total = totalKb * 1024;
    mem.used  = (totalKb - availKb) * 1024;
    mem.ok    = true;

    mem.swapTotal = swapTotalKb * 1024;
    mem.swapUsed  = (swapFreeKb <= swapTotalKb) ? (swapTotalKb - swapFreeKb) * 1024 : 0;

    mem.pressure = readMemPressure();
    return mem;
}

// ---------------------------------------------------------------- system

SysInfo getSysInfo() {
    SysInfo info{{0, 0, 0}, 0, false};

    // /proc/loadavg: "0.52 0.48 0.41 2/1234 5678"; we want the first three
    std::ifstream la("/proc/loadavg");
    if (!(la >> info.load[0] >> info.load[1] >> info.load[2])) return info;

    // /proc/uptime: "12345.67 98765.43"; first number = seconds since boot
    std::ifstream up("/proc/uptime");
    double uptime = 0;
    if (!(up >> uptime) || uptime < 0) return info;

    info.uptimeSec = static_cast<uint64_t>(uptime);
    info.ok = true;
    return info;
}

// ---------------------------------------------------------------- cpu

// First line of /proc/stat (summed over all cores):
//   cpu  user nice system idle iowait irq softirq steal guest guest_nice
static bool readCpuTicks(uint64_t& busy, uint64_t& total) {
    std::ifstream f("/proc/stat");
    if (!f) return false;

    std::string label;
    uint64_t user, nice, system, idle, iowait, irq, softirq, steal;
    if (!(f >> label >> user >> nice >> system >> idle
            >> iowait >> irq >> softirq >> steal) || label != "cpu") {
        return false;
    }

    // guest/guest_nice are already included in user/nice, so they're not added
    busy  = user + nice + system + irq + softirq + steal;
    total = busy + idle + iowait;
    return true;
}

CpuSampler::CpuSampler() : model_("Unknown") {
    // "model name	: AMD Ryzen 7 7735HS with Radeon Graphics"
    std::ifstream info("/proc/cpuinfo");
    std::string line;
    while (std::getline(info, line)) {
        if (line.rfind("model name", 0) == 0) {
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                auto start = line.find_first_not_of(" \t", colon + 1);
                if (start != std::string::npos) model_ = line.substr(start);
            }
            break;
        }
    }

    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0) cores_ = static_cast<int>(n);

    // So an early "nothing changed yet" return still shows the right model
    last_ = CpuInfo{model_, cores_, 0.0, true};
}

CpuInfo CpuSampler::sample() {
    CpuInfo cpu{model_, cores_, 0.0, false};

    uint64_t busy, total;
    if (!readCpuTicks(busy, total)) return cpu;

    if (!havePrev_) {
        // First call: record a baseline only; the UI shows "--"
        prevBusy_ = busy;
        prevTotal_ = total;
        havePrev_ = true;
        cpu.ok = true;
        return cpu;
    }

    // iowait can occasionally go backwards (see proc(5)); restart from here
    if (total < prevTotal_ || busy < prevBusy_) {
        prevBusy_ = busy;
        prevTotal_ = total;
        last_.ok = true;
        return last_;
    }

    uint64_t busyDiff  = busy  - prevBusy_;
    uint64_t totalDiff = total - prevTotal_;

    // No tick elapsed since the last call: keep the old baseline and result
    if (totalDiff == 0) {
        last_.ok = true;
        return last_;
    }

    prevBusy_ = busy;
    prevTotal_ = total;

    cpu.ok = true;
    cpu.usage = 100.0 * busyDiff / totalDiff;
    if (cpu.usage > 100.0) cpu.usage = 100.0;
    cpu.measured = true;
    last_ = cpu;
    return cpu;
}
