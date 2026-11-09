#include "stats.h"
#include <sys/sysctl.h>
#include <mach/mach.h>
#include <thread>
#include <chrono>

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
    return {total, used, true};
}

// Reads the CPU tick counters. Returns false if the call fails.
static bool readCpuTicks(uint64_t& busy, uint64_t& total) {
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&info, &count) != KERN_SUCCESS) {
        return false;
    }

    uint64_t user = info.cpu_ticks[CPU_STATE_USER];
    uint64_t sys  = info.cpu_ticks[CPU_STATE_SYSTEM];
    uint64_t nice = info.cpu_ticks[CPU_STATE_NICE];
    uint64_t idle = info.cpu_ticks[CPU_STATE_IDLE];

    busy  = user + sys + nice;
    total = busy + idle;
    return true;
}

CpuInfo getCpuInfo() {
    CpuInfo cpu{"Unknown", 0, 0.0, false};

    // CPU model name: a string, so we pass a character buffer
    char model[256];
    size_t size = sizeof(model);
    if (sysctlbyname("machdep.cpu.brand_string", model, &size, nullptr, 0) == 0) {
        cpu.model = model;
    }

    // Number of logical cores
    int cores = 0;
    size = sizeof(cores);
    if (sysctlbyname("hw.logicalcpu", &cores, &size, nullptr, 0) == 0) {
        cpu.cores = cores;
    }

    // Sample the tick counters twice, one second apart
    uint64_t busy1, total1, busy2, total2;
    if (!readCpuTicks(busy1, total1)) return cpu;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!readCpuTicks(busy2, total2)) return cpu;

    uint64_t totalDiff = total2 - total1;
    if (totalDiff == 0) return cpu;   // avoid dividing by zero

    cpu.usage = 100.0 * (busy2 - busy1) / totalDiff;
    cpu.ok = true;
    return cpu;
}
