#include "proc.h"

#include <libproc.h>
#include <mach/mach_time.h>
#include <pwd.h>
#include <sys/proc_info.h>
#include <sys/resource.h>

#include <chrono>
#include <cstring>

namespace {

uint64_t wallNowNs() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

ProcSampler::ProcSampler() {
  // CPU times from proc_pidinfo are in Mach absolute-time units, not ns.
  // On Intel the ratio is 1/1; on Apple Silicon it's 125/3 (24 MHz ticks),
  // so forgetting this makes every figure ~40x too small.
  mach_timebase_info_data_t tb{};
  if (mach_timebase_info(&tb) == KERN_SUCCESS && tb.denom != 0) {
    tbNumer_ = tb.numer;
    tbDenom_ = tb.denom;
  }
}

const std::string& ProcSampler::userName(uid_t uid) {
  auto it = users_.find(uid);
  if (it != users_.end()) return it->second;

  const passwd* pw = getpwuid(uid);
  std::string name = pw ? pw->pw_name : std::to_string(uid);
  return users_.emplace(uid, std::move(name)).first->second;
}

ProcList ProcSampler::sample() {
  ProcList list;

  // Ask how many PIDs there are, then fetch them, with some slack in case
  // processes start between the two calls. Returns the number of PIDs.
  int count = proc_listallpids(nullptr, 0);
  if (count <= 0) return list;
  std::vector<pid_t> pids(count + 64);
  count = proc_listallpids(pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
  if (count <= 0) return list;
  pids.resize(std::min<size_t>(count, pids.size()));

  uint64_t wallNow = wallNowNs();
  uint64_t wallDelta = prevWallNs_ ? wallNow - prevWallNs_ : 0;
  std::unordered_map<pid_t, uint64_t> cpuNow;  // becomes prevCpuNs_ at the end
  cpuNow.reserve(pids.size());

  list.procs.reserve(pids.size());
  for (pid_t pid : pids) {
    // Name, owner, etc. This "short" query isn't permission-checked, so it
    // works for every process. If it fails, the process has already exited.
    proc_bsdshortinfo bsd{};
    if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0, &bsd, sizeof(bsd)) != sizeof(bsd)) {
      continue;
    }

    ProcInfo p;
    p.pid = pid;
    p.user = userName(bsd.pbsi_uid);

    // proc_name gives up to 32 characters but is permission-checked; the
    // short info's name (16 characters) is the fallback.
    char name[2 * MAXCOMLEN + 1] = {};
    if (proc_name(pid, name, sizeof(name)) > 0) {
      p.name = name;
    } else {
      p.name.assign(bsd.pbsi_comm, strnlen(bsd.pbsi_comm, sizeof(bsd.pbsi_comm)));
    }

    // CPU time and threads. Fails with EPERM for other users' processes
    // unless we run as root; those stay readable = false.
    proc_taskinfo task{};
    if (proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task, sizeof(task)) == sizeof(task)) {
      p.readable = true;
      p.threads = task.pti_threadnum;

      uint64_t cpuUnits = task.pti_total_user + task.pti_total_system;
      uint64_t cpuNs = cpuUnits * tbNumer_ / tbDenom_;
      cpuNow[pid] = cpuNs;

      auto prev = prevCpuNs_.find(pid);
      if (prev != prevCpuNs_.end()) {
        p.measured = cpuPercent(prev->second, cpuNs, wallDelta, p.cpu);
      }

      // Footprint is what Activity Monitor's "Memory" column shows; fall
      // back to resident size if it's unavailable.
      rusage_info_v4 ru{};
      if (proc_pid_rusage(pid, RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t*>(&ru)) == 0) {
        p.mem = ru.ri_phys_footprint;
      } else {
        p.mem = task.pti_resident_size;
      }
      ++list.readable;
    }

    list.procs.push_back(std::move(p));
  }

  // Processes that exited simply don't appear in cpuNow, so they're dropped
  prevCpuNs_ = std::move(cpuNow);
  prevWallNs_ = wallNow;
  list.ok = true;
  return list;
}
