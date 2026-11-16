#include "proc.h"

#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

uint64_t wallNowNs() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

// The /proc files are tiny, so just read the whole thing.
// Fails if the process exited since we listed it.
bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

constexpr unsigned long kKernelThreadFlag = 0x00200000;  // PF_KTHREAD

struct StatFields {
  std::string comm;
  unsigned long flags = 0;
  uint64_t utime = 0, stime = 0;  // clock ticks
  long threads = 0;
  long rssPages = 0;
};

// /proc/<pid>/stat is one line: "pid (comm) state ppid ... utime stime ..."
// Field numbers below match `man 5 proc`.
bool parseStat(const std::string& s, StatFields& out) {
  // comm can itself contain spaces and ')', so take everything up to the LAST ')'
  auto open = s.find('(');
  auto close = s.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return false;
  out.comm = s.substr(open + 1, close - open - 1);

  std::istringstream rest(s.substr(close + 1));
  std::string tok;
  for (int field = 3; rest >> tok; ++field) {    // field 3 (state) comes right after ')'
    const char* t = tok.c_str();
    if      (field == 9)  out.flags    = std::strtoul(t, nullptr, 10);
    else if (field == 14) out.utime    = std::strtoull(t, nullptr, 10);
    else if (field == 15) out.stime    = std::strtoull(t, nullptr, 10);
    else if (field == 20) out.threads  = std::strtol(t, nullptr, 10);
    else if (field == 24) { out.rssPages = std::strtol(t, nullptr, 10); return true; }
  }
  return false;
}

// comm is cut off at 15 characters. If it looks truncated, use the
// basename of argv[0] instead, but only if it starts the same way
// (some programs rewrite argv[0] to something unrelated).
std::string fullName(const std::string& base, const std::string& comm) {
  if (comm.size() < 15) return comm;
  std::string cmd;
  if (!readFile(base + "/cmdline", cmd) || cmd.empty()) return comm;
  std::string argv0 = cmd.substr(0, cmd.find('\0'));
  auto slash = argv0.rfind('/');
  std::string name = (slash == std::string::npos) ? argv0 : argv0.substr(slash + 1);
  return name.rfind(comm, 0) == 0 ? name : comm;
}

}  // namespace

ProcSampler::ProcSampler() {
  // utime/stime are in clock ticks (USER_HZ, 100 per second on practically
  // every Linux system). The header's numer/denom pair converts CPU-time
  // units to ns, same job as the Mach timebase on macOS.
  long hz = sysconf(_SC_CLK_TCK);
  if (hz > 0) {
    tbNumer_ = static_cast<uint32_t>(1000000000L / hz);   // 10,000,000 ns per tick at 100 Hz
    tbDenom_ = 1;
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

  DIR* dir = opendir("/proc");
  if (!dir) return list;

  uint64_t wallNow = wallNowNs();
  uint64_t wallDelta = prevWallNs_ ? wallNow - prevWallNs_ : 0;
  std::unordered_map<pid_t, uint64_t> cpuNow;  // becomes prevCpuNs_ at the end

  const uint64_t pageSize = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));

  while (dirent* e = readdir(dir)) {
    // Every process is a directory named by its PID; skip ".", "self", "meminfo", ...
    char* end = nullptr;
    long v = std::strtol(e->d_name, &end, 10);
    if (*end != '\0' || v <= 0) continue;

    pid_t pid = static_cast<pid_t>(v);
    std::string base = std::string("/proc/") + e->d_name;

    std::string statText;
    StatFields st;
    if (!readFile(base + "/stat", statText) || !parseStat(statText, st)) continue;

    // Kernel threads (kworker, ksoftirqd, ...) aren't programs; htop hides them too
    if (st.flags & kKernelThreadFlag) continue;

    ProcInfo p;
    p.pid = pid;
    p.name = fullName(base, st.comm);

    // /proc/<pid> is owned by the process's user
    struct stat sb{};
    p.user = (::stat(base.c_str(), &sb) == 0) ? userName(sb.st_uid) : std::string("?");

    // /proc/<pid>/stat is world-readable, so unlike macOS we get CPU time
    // and memory for every process without root
    p.readable = true;
    p.threads = static_cast<int>(st.threads);
    p.mem = st.rssPages > 0 ? static_cast<uint64_t>(st.rssPages) * pageSize : 0;

    uint64_t cpuNs = (st.utime + st.stime) * tbNumer_ / tbDenom_;
    cpuNow[pid] = cpuNs;

    auto prev = prevCpuNs_.find(pid);
    if (prev != prevCpuNs_.end()) {
      p.measured = cpuPercent(prev->second, cpuNs, wallDelta, p.cpu);
    }

    ++list.readable;
    list.procs.push_back(std::move(p));
  }
  closedir(dir);

  // Processes that exited simply don't appear in cpuNow, so they're dropped
  prevCpuNs_ = std::move(cpuNow);
  prevWallNs_ = wallNow;
  list.ok = true;
  return list;
}
