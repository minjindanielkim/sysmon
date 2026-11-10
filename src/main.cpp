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

// Pressing the active sort key again flips the direction; a new key starts
// in its natural direction (biggest first for CPU/memory, A→Z / low→high
// for name/PID) and jumps back to the top of the list.
static void setSort(ViewState& v, SortKey key) {
  if (v.sort == key) {
    v.descending = !v.descending;
  } else {
    v.sort = key;
    v.descending = (key == SortKey::Cpu || key == SortKey::Mem);
  }
  v.scroll = 0;
}

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
  ViewState view;
  view.interval = opts.interval;
  view.graphs = opts.graphs;

  Sampler sampler(opts.disk);
  sampler.tick();
  auto lastSample = Clock::now();

  Ui ui(opts.color);
  while (!g_quit) {
    ui.draw(sampler.latest(), view);  // also clamps view.scroll

    // Blocks for up to ~100 ms, which paces the loop. Resize needs no special
    // handling: ncurses updates the screen size and the next draw() uses it.
    // Scrolling past either end is fine: draw() clamps it.
    switch (ui.readKey()) {
      case Ui::Key::Quit:     g_quit = 1; continue;
      case Ui::Key::Longer:   view.interval = stepInterval(view.interval, +1); break;
      case Ui::Key::Shorter:  view.interval = stepInterval(view.interval, -1); break;
      case Ui::Key::SortCpu:  setSort(view, SortKey::Cpu);  break;
      case Ui::Key::SortMem:  setSort(view, SortKey::Mem);  break;
      case Ui::Key::SortPid:  setSort(view, SortKey::Pid);  break;
      case Ui::Key::SortName: setSort(view, SortKey::Name); break;
      case Ui::Key::Up:       view.scroll -= 1; break;
      case Ui::Key::Down:     view.scroll += 1; break;
      case Ui::Key::PageUp:   view.scroll -= ui.pageRows(); break;
      case Ui::Key::PageDown: view.scroll += ui.pageRows(); break;
      case Ui::Key::Home:     view.scroll = 0; break;
      case Ui::Key::End:      view.scroll = 1 << 30; break;
      case Ui::Key::ToggleGraphs: view.graphs = !view.graphs; break;
      default: break;
    }

    // Measured from the last sample, so shortening the interval takes
    // effect straight away instead of after the old, longer wait
    auto now = Clock::now();
    if (now - lastSample >= toDuration(view.interval)) {
      sampler.tick();
      lastSample = now;
    }
  }
  return 0;
}
