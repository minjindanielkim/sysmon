#include "options.h"

#include <cmath>
#include <cstdlib>
#include <iterator>

namespace {

const char* defaultDiskPath() {
#ifdef __APPLE__
  return "/System/Volumes/Data";  // where user data lives on macOS
#else
  return "/";
#endif
}

// Parses a positive decimal number of seconds within the allowed range.
bool parseSeconds(const std::string& text, double& out, std::string& error) {
  char* end = nullptr;
  double v = std::strtod(text.c_str(), &end);
  if (text.empty() || *end != '\0' || !std::isfinite(v)) {
    error = "invalid interval '" + text + "' (expected a number of seconds)";
    return false;
  }
  if (v < kMinInterval || v > kMaxInterval) {
    error = "interval must be between 0.25 and 60 seconds";
    return false;
  }
  out = v;
  return true;
}

}  // namespace

bool parseArgs(int argc, char** argv, Options& out, std::string& error) {
  out.disk = defaultDiskPath();

  // https://no-color.org: any non-empty NO_COLOR disables colour
  const char* noColor = std::getenv("NO_COLOR");
  if (noColor && *noColor) out.color = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    // Split "--name=value" into name and an inline value
    std::string inlineValue;
    bool hasInline = false;
    if (arg.rfind("--", 0) == 0) {
      auto eq = arg.find('=');
      if (eq != std::string::npos) {
        inlineValue = arg.substr(eq + 1);
        arg = arg.substr(0, eq);
        hasInline = true;
      }
    }

    // Value for an option that takes one: inline, or the next argument
    auto takeValue = [&](std::string& value) {
      if (hasInline) { value = inlineValue; return true; }
      if (i + 1 < argc) { value = argv[++i]; return true; }
      error = "option '" + arg + "' needs a value";
      return false;
    };

    if (arg == "-h" || arg == "--help") {
      out.help = true;
    } else if (arg == "-i" || arg == "--interval") {
      std::string v;
      if (!takeValue(v) || !parseSeconds(v, out.interval, error)) return false;
    } else if (arg == "-d" || arg == "--disk") {
      std::string v;
      if (!takeValue(v)) return false;
      if (v.empty()) { error = "disk path is empty"; return false; }
      out.disk = v;
    } else if (arg == "-g" || arg == "--graphs") {
      if (hasInline) { error = "option '--graphs' takes no value"; return false; }
      out.graphs = true;
    } else if (arg == "--no-color") {
      if (hasInline) { error = "option '--no-color' takes no value"; return false; }
      out.color = false;
    } else {
      error = "unknown option '" + std::string(argv[i]) + "'";
      return false;
    }
  }
  return true;
}

const char* helpText() {
  return
      "Usage: sysmon [options]\n"
      "\n"
      "Live terminal dashboard for CPU, memory, disk, network and processes.\n"
      "\n"
      "Options:\n"
      "  -i, --interval SECONDS  time between updates (default 1, range 0.25-60)\n"
      "  -d, --disk PATH         volume to show disk usage for\n"
#ifdef __APPLE__
      "                          (default /System/Volumes/Data)\n"
#else
      "                          (default /)\n"
#endif
      "  -g, --graphs            start with graphs instead of bars\n"
      "      --no-color          disable colours (so does setting NO_COLOR)\n"
      "  -h, --help              show this help and exit\n"
      "\n"
      "Keys:\n"
      "  q          quit\n"
      "  + / -      lengthen / shorten the update interval\n"
      "  g          switch between bars and graphs\n"
      "  c m p n    sort processes by CPU, memory, PID or name (again to reverse)\n"
      "  arrows, PgUp/PgDn, Home/End   scroll the process list\n";
}

double stepInterval(double current, int direction) {
  static const double presets[] = {0.25, 0.5, 1, 2, 3, 5, 10, 30, 60};
  const double eps = 1e-9;
  if (direction > 0) {
    for (double p : presets) if (p > current + eps) return p;
  } else if (direction < 0) {
    for (int i = static_cast<int>(std::size(presets)) - 1; i >= 0; --i)
      if (presets[i] < current - eps) return presets[i];
  }
  return current;
}
