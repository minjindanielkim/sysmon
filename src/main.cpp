#include "sampler.h"
#include "ui.h"

#include <chrono>
#include <csignal>

// Set by Ctrl-C (SIGINT), SIGTERM or the terminal closing (SIGHUP). The handler
// only flips this flag; the main loop notices it within ~100 ms and returns,
// so Ui's destructor runs endwin() and the terminal is restored.
static volatile std::sig_atomic_t g_quit = 0;
static void onSignal(int) { g_quit = 1; }

int main() {
  // Install before Ui starts, so ncurses keeps our handlers instead of its own
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGHUP, onSignal);

#ifdef __APPLE__
  const char* diskPath = "/System/Volumes/Data";  // where user data lives on macOS
#else
  const char* diskPath = "/";
#endif

  using Clock = std::chrono::steady_clock;
  const auto interval = std::chrono::seconds(1);

  Sampler sampler(diskPath);
  sampler.tick();
  auto nextSample = Clock::now() + interval;

  Ui ui;
  while (!g_quit) {
    ui.draw(sampler.latest());

    // Blocks for up to ~100 ms, which paces the loop. Resize needs no special
    // handling: ncurses updates the screen size and the next draw() uses it.
    if (ui.readKey() == Ui::Key::Quit) break;

    auto now = Clock::now();
    if (now >= nextSample) {
      sampler.tick();
      nextSample += interval;
      if (nextSample <= now) nextSample = now + interval;  // fell behind (e.g. suspended)
    }
  }
  return 0;
}
