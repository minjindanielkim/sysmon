#include "stats.h"
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <algorithm>
#include <cstdlib>
#include <ctime>

MemInfo getMemInfo() {
    // Total RAM (same as before)
    uint64_t total = 0;
    size_t size = sizeof(total);
    if (sysctlbyname("hw.memsize", &total, &size, nullptr, 0) != 0) {
        return {0, 0, false};
    }

    // Page size in bytes (16 KB on Apple Silicon, 4 KB on Intel)
    vm_size_t pageSize = 0;
    host_page_size(mach_host_self(), &pageSize);

    // Ask the kernel for page counts in each memory state
    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    kern_return_t result = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                             (host_info64_t)&vm, &count);
    if (result != KERN_SUCCESS) {
        return {total, 0, false};
    }

    // Activity Monitor's "Memory Used" = App Memory + Wired + Compressed
    uint64_t appPages        = vm.internal_page_count - vm.purgeable_count;
    uint64_t wiredPages      = vm.wire_count;
    uint64_t compressedPages = vm.compressor_page_count;

    uint64_t used = (appPages + wiredPages + compressedPages) * pageSize;
    MemInfo mem{total, used, true};

    // Swap: macOS creates swap files on demand, so total is often 0
    xsw_usage swap{};
    size = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &size, nullptr, 0) == 0) {
        mem.swapTotal = swap.xsu_total;
        mem.swapUsed  = swap.xsu_used;
    }

    // Kernel's memory pressure level: 1 = normal, 2 = warning, 4 = critical
    int level = 0;
    size = sizeof(level);
    if (sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &size, nullptr, 0) == 0) {
        switch (level) {
            case 1: mem.pressure = MemPressure::Normal;   break;
            case 2: mem.pressure = MemPressure::Warning;  break;
            case 4: mem.pressure = MemPressure::Critical; break;
            default: break;  // leave Unknown
        }
    }
    return mem;
}

SysInfo getSysInfo() {
    SysInfo info{{0, 0, 0}, 0, false};

    if (getloadavg(info.load, 3) != 3) return info;  // from <stdlib.h>

    // Boot time; uptime = now - boot
    timeval boot{};
    size_t size = sizeof(boot);
    if (sysctlbyname("kern.boottime", &boot, &size, nullptr, 0) != 0) return info;

    std::time_t now = std::time(nullptr);
    info.uptimeSec = now > boot.tv_sec ? static_cast<uint64_t>(now - boot.tv_sec) : 0;
    info.ok = true;
    return info;
}

// Reads the raw CPU tick counters (summed across all cores).
// Returns false if the call fails.
static bool readCpuTicks(uint32_t ticks[CPU_STATE_MAX]) {
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&info, &count) != KERN_SUCCESS) {
        return false;
    }
    for (int i = 0; i < CPU_STATE_MAX; ++i) ticks[i] = info.cpu_ticks[i];
    return true;
}

CpuSampler::CpuSampler() : model_("Unknown") {
    static_assert(kStates == CPU_STATE_MAX, "tick array size mismatch");

    // CPU model name: a string, so we pass a character buffer
    char model[256];
    size_t size = sizeof(model);
    if (sysctlbyname("machdep.cpu.brand_string", model, &size, nullptr, 0) == 0) {
        model_ = model;
    }

    // Number of logical cores
    int cores = 0;
    size = sizeof(cores);
    if (sysctlbyname("hw.logicalcpu", &cores, &size, nullptr, 0) == 0) {
        cores_ = cores;
    }
}

CpuInfo CpuSampler::sample() {
    CpuInfo cpu{model_, cores_, 0.0, false};

    uint32_t now[kStates];
    if (!readCpuTicks(now)) return cpu;

    if (!havePrev_) {
        // First call: just record a baseline. Nothing to compare against
        // yet, so measured stays false and the UI shows "--".
        std::copy(now, now + kStates, prevTicks_);
        havePrev_ = true;
        cpu.ok = true;
        return cpu;
    }

    // The kernel counters are 32-bit and wrap around on long uptimes.
    // Subtracting in uint32_t arithmetic gives the right difference anyway.
    uint32_t d[kStates];
    for (int i = 0; i < kStates; ++i) d[i] = now[i] - prevTicks_[i];
    std::copy(now, now + kStates, prevTicks_);

    uint64_t busy  = uint64_t(d[CPU_STATE_USER]) + d[CPU_STATE_SYSTEM] + d[CPU_STATE_NICE];
    uint64_t total = busy + d[CPU_STATE_IDLE];

    cpu.ok = true;
    if (total > 0) {  // else no ticks elapsed: leave measured = false
        cpu.usage = 100.0 * busy / total;
        cpu.measured = true;
    }
    return cpu;
}
