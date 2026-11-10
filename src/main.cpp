#include "options.h"
#include "sampler.h"
#include "ui.h"

#include <chrono>
#include <csignal>
#include <cstdio>

// Set by Ctrl-C (SIGINT), SIGTERM or the terminal closing (SIGHUP). The handler
// only flips this flag; the main loop notices it within ~100 ms and returns,
// so Ui's destructor runs endwin() and the terminal is restored.
static volatile std::sig_atomic_t g_quit = 0;
static void onSignal(int) { g_quit = 1; }

int main(int argc, char** argv) {
  Options opts;
  std::string error;
  if (!parseArgs(argc, argv, opts, error)) {
    std::fprintf(stderr, "sysmon: %s\nTry 'sysmon --help' for more information.\n",
                 error.c_str());
    return 2;
  }
  if (opts.help) {
    std::fputs(helpText(), stdout);
    return 0;
  }

  // Catch a bad --disk now, with a clear message, rather than showing
  // "unavailable" in the panel forever
  if (!getDiskInfo(opts.disk).ok) {
    std::fprintf(stderr, "sysmon: cannot read disk usage for '%s'\n", opts.disk.c_str());
    return 1;
  }

  // Install before Ui starts, so ncurses keeps our handlers instead of its own
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGHUP, onSignal);

  using Clock = std::chrono::steady_clock;
  auto toDuration = [](double seconds) {
    return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
  };
  double interval = opts.interval;

  Sampler sampler(opts.disk);
  sampler.tick();
  auto lastSample = Clock::now();

  Ui ui(opts.color);
  while (!g_quit) {
    ui.draw(sampler.latest(), interval);

    // Blocks for up to ~100 ms, which paces the loop. Resize needs no special
    // handling: ncurses updates the screen size and the next draw() uses it.
    switch (ui.readKey()) {
      case Ui::Key::Quit:    g_quit = 1; continue;
      case Ui::Key::Longer:  interval = stepInterval(interval, +1); break;
      case Ui::Key::Shorter: interval = stepInterval(interval, -1); break;
      default: break;
    }

    // Measured from the last sample, so shortening the interval takes
    // effect straight away instead of after the old, longer wait
    auto now = Clock::now();
    if (now - lastSample >= toDuration(interval)) {
      sampler.tick();
      lastSample = now;
    }
  }
  return 0;
}
