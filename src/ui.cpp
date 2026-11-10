#include "ui.h"

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <strings.h>  // strcasecmp
#include <vector>

// Wide-character ncurses API (needed for █ ▏ ░ etc.). Must come before
// curses.h, and curses.h comes last because it defines short macros.
#define NCURSES_WIDECHAR 1
#include <curses.h>

namespace {

// Colour pair ids
enum Pair : short { kGreen = 1, kYellow, kRed, kAccent, kDim };

constexpr int kMinWidth = 40;
constexpr int kPanelHeight = 4;     // border, info line, bar line, border
constexpr int kNetPanelHeight = 3;  // border, rates line, border (no bar: no maximum)
constexpr int kNumBarPanels = 3;    // CPU, memory, disk
// header + panels + footer
constexpr int kMinHeight = 1 + kNumBarPanels * kPanelHeight + kNetPanelHeight + 1;
constexpr int kMinProcPanel = 4;  // border, column header, one row, border

bool g_colors = false;

void color_on(short pair) { if (g_colors) attron(COLOR_PAIR(pair)); }
void color_off(short pair) { if (g_colors) attroff(COLOR_PAIR(pair)); }

short levelColor(double fraction) {
  if (fraction < 0.60) return kGreen;
  if (fraction < 0.85) return kYellow;
  return kRed;
}

std::string fmtBytes(uint64_t bytes) {
  static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
  char buf[32];
  std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
  return buf;
}

// "512 B/s", "35.0 KB/s", "1.2 MB/s"
std::string fmtRate(double bytesPerSec) {
  static const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
  int u = 0;
  while (bytesPerSec >= 1024.0 && u < 3) { bytesPerSec /= 1024.0; ++u; }
  char buf[32];
  std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", bytesPerSec, units[u]);
  return buf;
}

std::string fmtPercent(double pct) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%.1f%%", pct);
  return buf;
}

// "3d 4h", "4h 12m" or "12m": the two most significant units.
std::string fmtUptime(uint64_t sec) {
  uint64_t d = sec / 86400, h = sec % 86400 / 3600, m = sec % 3600 / 60;
  char buf[32];
  if (d > 0)      std::snprintf(buf, sizeof(buf), "%llud %lluh", (unsigned long long)d, (unsigned long long)h);
  else if (h > 0) std::snprintf(buf, sizeof(buf), "%lluh %llum", (unsigned long long)h, (unsigned long long)m);
  else            std::snprintf(buf, sizeof(buf), "%llum", (unsigned long long)m);
  return buf;
}

// Width in terminal columns of a UTF-8 string. Every character we draw is
// one column wide, so this counts characters (bytes that aren't UTF-8
// continuation bytes), not bytes: "·" is 2 bytes but 1 column.
int cols(const std::string& s) {
  int n = 0;
  for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n;
  return n;
}

// Cuts a UTF-8 string to at most `max` columns without splitting a
// multi-byte character (which would print as garbage).
std::string fit(const std::string& s, int max) {
  if (max <= 0) return "";
  int n = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    bool startsChar = (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
    if (startsChar && n++ == max) return s.substr(0, i);
  }
  return s;
}

// Bordered panel with a bold title on the top edge.
// Drawn with Unicode box characters rather than ncurses' ACS_* line drawing:
// ACS relies on the terminal's alternate-charset mode, which some terminals
// and multiplexers mishandle (it shows up as "lqqqk").
void drawBox(int y, int x, int h, int w, const std::string& title) {
  std::wstring horiz(w - 2, L'─');
  mvaddwstr(y, x, (L"┌" + horiz + L"┐").c_str());
  mvaddwstr(y + h - 1, x, (L"└" + horiz + L"┘").c_str());
  for (int r = y + 1; r < y + h - 1; ++r) {
    mvaddwstr(r, x, L"│");
    mvaddwstr(r, x + w - 1, L"│");
  }

  std::string t = fit(" " + title + " ", w - 4);
  attron(A_BOLD);
  mvaddstr(y, x + 2, t.c_str());
  attroff(A_BOLD);
}

// One line of text: `left` on the left, `right` (bold) flush right.
void drawInfoLine(int y, int x, int w, const std::string& left, const std::string& right) {
  int rw = cols(right);
  mvaddstr(y, x, fit(left, w - rw - 1).c_str());
  attron(A_BOLD);
  mvaddstr(y, x + w - rw, right.c_str());
  attroff(A_BOLD);
}

// Pac-Man's mouth opens and closes every 250 ms. Based on wall-clock time,
// so it animates even when a value (like disk usage) isn't changing.
bool mouthOpen() {
  using namespace std::chrono;
  auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
  return (ms / 250) % 2 == 0;
}

// Progress bar in the style of pacman's ILoveCandy option (on by default in
// CachyOS): Pac-Man eats a row of pellets, leaving a trail of dashes.
//   [-------------C  o  o  o  o ]
// The trail is coloured by level (green / yellow / red) unless `color` is
// given; Pac-Man is bold yellow.
void drawChompBar(int y, int x, int w, double fraction, short color = 0) {
  int inner = w - 2;  // space between the brackets
  fraction = std::clamp(fraction, 0.0, 1.0);
  int eaten = static_cast<int>(std::lround(fraction * inner));

  mvaddch(y, x, '[');

  short c = color ? color : levelColor(fraction);
  color_on(c);
  mvaddstr(y, x + 1, std::string(eaten, '-').c_str());
  color_off(c);

  if (eaten < inner) {  // at 100% the trail fills the bar, like pacman's
    attron(A_BOLD);
    color_on(kYellow);
    mvaddch(y, x + 1 + eaten, mouthOpen() ? 'C' : 'c');
    color_off(kYellow);
    attroff(A_BOLD);

    // Pellets sit at fixed columns (every third, counted from the right end
    // as pacman does), so Pac-Man visibly eats them as the value rises.
    for (int col = eaten + 1; col < inner; ++col) {
      mvaddch(y, x + 1 + col, (inner - col) % 3 == 0 ? 'o' : ' ');
    }
  }

  mvaddch(y, x + 1 + inner, ']');
}

void drawUnavailable(int y, int x) {
  color_on(kDim);
  mvaddstr(y, x, "unavailable");
  color_off(kDim);
}

void drawCpu(int y, int w, const CpuInfo& c) {
  drawBox(y, 0, kPanelHeight, w, "CPU");
  int ix = 2, iw = w - 4;
  if (!c.ok) { drawUnavailable(y + 1, ix); return; }
  // Until a second reading exists there's no honest number to show
  drawInfoLine(y + 1, ix, iw, c.model + " · " + std::to_string(c.cores) + " cores",
               c.measured ? fmtPercent(c.usage) : "--");
  drawChompBar(y + 2, ix, iw, c.measured ? c.usage / 100.0 : 0.0);
}

void drawMemory(int y, int w, const MemInfo& m) {
  // The bar colour follows macOS's memory pressure rather than % used:
  // high usage is normal on macOS (spare RAM is used as cache).
  std::string title = "Memory";
  short barColor = 0;  // 0 = fall back to % used thresholds
  switch (m.pressure) {
    case MemPressure::Normal:   title += " · pressure normal";   barColor = kGreen;  break;
    case MemPressure::Warning:  title += " · pressure warning";  barColor = kYellow; break;
    case MemPressure::Critical: title += " · pressure critical"; barColor = kRed;    break;
    case MemPressure::Unknown:  break;
  }

  drawBox(y, 0, kPanelHeight, w, title);
  int ix = 2, iw = w - 4;
  if (!m.ok || m.total == 0) { drawUnavailable(y + 1, ix); return; }
  double frac = static_cast<double>(m.used) / m.total;

  std::string pct = fmtPercent(frac * 100);
  std::string left = fmtBytes(m.used) + " used of " + fmtBytes(m.total);
  std::string swap = m.swapUsed > 0 ? " · swap " + fmtBytes(m.swapUsed) : " · no swap";
  // Add the swap part only if it fits whole; a half-cut "no s" reads badly
  if (cols(left + swap) <= iw - cols(pct) - 1) left += swap;
  drawInfoLine(y + 1, ix, iw, left, pct);
  drawChompBar(y + 2, ix, iw, frac, barColor);
}

void drawDisk(int y, int w, const DiskInfo& d) {
  drawBox(y, 0, kPanelHeight, w, "Disk · " + d.mount);
  int ix = 2, iw = w - 4;
  if (!d.ok || d.total == 0) { drawUnavailable(y + 1, ix); return; }
  double frac = static_cast<double>(d.used) / d.total;
  drawInfoLine(y + 1, ix, iw, fmtBytes(d.used) + " used of " + fmtBytes(d.total),
               fmtPercent(frac * 100));
  drawChompBar(y + 2, ix, iw, frac);
}

// ┌─ Network · en0 ─────────────────────────────────────────────┐
// │ ↓ 1.2 MB/s     ↑ 35.0 KB/s          total ↓ 7.0 GB ↑ 406.3 MB │
// └─────────────────────────────────────────────────────────────┘
void drawNetwork(int y, int w, const NetInfo& n) {
  std::string title = "Network";
  for (size_t i = 0; i < n.names.size(); ++i) title += (i ? ", " : " · ") + n.names[i];
  drawBox(y, 0, kNetPanelHeight, w, title);
  int ix = 2, iw = w - 4;
  if (!n.ok) { drawUnavailable(y + 1, ix); return; }

  // Each rate gets a fixed-width slot ("1023.9 KB/s" is the widest), so the
  // upload figure doesn't shift sideways as the download figure changes
  constexpr int kRateWidth = 11;
  auto rate = [&](double r) {
    std::string s = n.measured ? fmtRate(r) : "--";
    return s + std::string(std::max(0, kRateWidth - cols(s)), ' ');
  };
  const int upX = ix + 2 + kRateWidth + 2;  // "↓ " + slot + gap
  const int ratesWidth = upX + 2 + kRateWidth - ix;

  color_on(kGreen);
  mvaddstr(y + 1, ix, "↓");
  color_off(kGreen);
  color_on(kAccent);
  mvaddstr(y + 1, upX, "↑");
  color_off(kAccent);
  attron(A_BOLD);
  mvaddstr(y + 1, ix + 2, rate(n.rxRate).c_str());
  mvaddstr(y + 1, upX + 2, rate(n.txRate).c_str());
  attroff(A_BOLD);

  std::string total = "total ↓ " + fmtBytes(n.rxTotal) + " ↑ " + fmtBytes(n.txTotal);
  if (ratesWidth + 1 + cols(total) <= iw) {
    color_on(kDim);
    mvaddstr(y + 1, ix + iw - cols(total), total.c_str());
    color_off(kDim);
  }
}

// " sysmon   load 1.45 1.39 1.42 · up 1d 3h            11:29:04"
void drawHeader(int w, const SysInfo& sys) {
  attron(A_BOLD);
  color_on(kAccent);
  mvaddstr(0, 1, "sysmon");
  color_off(kAccent);
  attroff(A_BOLD);

  char clock[16];
  std::time_t now = std::time(nullptr);
  std::strftime(clock, sizeof(clock), "%H:%M:%S", std::localtime(&now));
  int clockX = w - 1 - cols(clock);
  mvaddstr(0, clockX, clock);

  if (!sys.ok) return;

  // Show as much as fits between "sysmon" and the clock: everything, then
  // just the load averages, then nothing.
  char load[48];
  std::snprintf(load, sizeof(load), "load %.2f %.2f %.2f", sys.load[0], sys.load[1], sys.load[2]);
  const int startX = 1 + 6 + 3;            // after " sysmon" plus a gap
  const int room = clockX - 2 - startX;    // keep a gap before the clock
  for (const std::string& s : {std::string(load) + " · up " + fmtUptime(sys.uptimeSec),
                               std::string(load)}) {
    if (cols(s) <= room) {
      mvaddstr(0, startX, s.c_str());
      break;
    }
  }
}

// ---- Process table ---------------------------------------------------------

const char* sortName(SortKey k) {
  switch (k) {
    case SortKey::Cpu:  return "CPU";
    case SortKey::Mem:  return "memory";
    case SortKey::Pid:  return "PID";
    case SortKey::Name: return "name";
  }
  return "";
}

// Processes in display order. Readable processes come first, sorted by the
// chosen key; unreadable ones (no CPU/memory without sudo) always go last,
// since sorting blanks against numbers would scatter them through the list.
// Ties fall back to PID so rows don't jump around between frames.
std::vector<const ProcInfo*> sortedProcs(const ProcList& list, const ViewState& v) {
  std::vector<const ProcInfo*> rows;
  rows.reserve(list.procs.size());
  for (const ProcInfo& p : list.procs) rows.push_back(&p);

  auto cmp3 = [](auto x, auto y) { return (x < y) ? -1 : (x > y) ? 1 : 0; };

  auto less = [&](const ProcInfo* a, const ProcInfo* b) {
    if (a->readable != b->readable) return a->readable;  // readable first, always

    SortKey key = v.sort;
    bool desc = v.descending;
    // Unreadable rows have no CPU/memory figures, so sort those by name A→Z
    if (!a->readable && (key == SortKey::Cpu || key == SortKey::Mem)) {
      key = SortKey::Name;
      desc = false;
    }

    int c = 0;  // ascending three-way comparison
    switch (key) {
      case SortKey::Cpu:  c = cmp3(a->cpu, b->cpu); break;
      case SortKey::Mem:  c = cmp3(a->mem, b->mem); break;
      case SortKey::Pid:  c = cmp3(a->pid, b->pid); break;
      case SortKey::Name: c = strcasecmp(a->name.c_str(), b->name.c_str()); break;
    }
    if (c != 0) return desc ? c > 0 : c < 0;
    return a->pid < b->pid;
  };
  std::sort(rows.begin(), rows.end(), less);
  return rows;
}

// Which optional columns fit. NAME takes whatever is left.
struct Columns {
  bool user, threads;
};
Columns columnsFor(int width) {
  return {width >= 62, width >= 52};
}

// One table line, header or row, laid out to the same column widths:
//   "  PID USER        CPU%       MEM  THR  NAME"
std::string tableLine(const Columns& c, int width, const std::string& pid,
                      const std::string& user, const std::string& cpu,
                      const std::string& mem, const std::string& thr,
                      const std::string& name) {
  char buf[128];
  std::string line;
  std::snprintf(buf, sizeof(buf), "%6s ", pid.c_str());               line += buf;
  if (c.user) { std::snprintf(buf, sizeof(buf), "%-10s ", fit(user, 10).c_str()); line += buf; }
  std::snprintf(buf, sizeof(buf), "%6s %9s ", cpu.c_str(), mem.c_str()); line += buf;
  if (c.threads) { std::snprintf(buf, sizeof(buf), "%4s ", thr.c_str()); line += buf; }
  line += " ";
  line += fit(name, width - cols(line));
  return line;
}

std::string fmtCpu(const ProcInfo& p) {
  if (!p.readable) return "-";
  if (!p.measured) return "--";
  char buf[16];
  std::snprintf(buf, sizeof(buf), p.cpu < 1000 ? "%.1f" : "%.0f", p.cpu);
  return buf;
}

// Draws the process panel in rows [y, y+h). Returns how many process rows
// fit, so the caller can clamp scrolling and size PgUp/PgDn.
int drawProcesses(int y, int h, int w, const ProcList& list, ViewState& v) {
  // Longest title that fits: full, then without the readable count, then bare
  const char* arrow = v.descending ? "▼" : "▲";
  char full[96], mid[64];
  std::snprintf(full, sizeof(full), "Processes · %zu (%d readable) · sort: %s %s",
                list.procs.size(), list.readable, sortName(v.sort), arrow);
  std::snprintf(mid, sizeof(mid), "Processes · %zu · %s %s",
                list.procs.size(), sortName(v.sort), arrow);
  std::string title = "Processes";
  for (const char* t : {full, mid}) {
    if (cols(t) + 2 <= w - 4) { title = t; break; }  // +2: spaces around the title
  }
  drawBox(y, 0, h, w, title);
  int ix = 2, iw = w - 4;
  int rowsVisible = h - 3;  // minus top border, column header, bottom border

  if (!list.ok) { drawUnavailable(y + 1, ix); return rowsVisible; }

  Columns c = columnsFor(iw);
  attron(A_BOLD);
  mvaddstr(y + 1, ix, tableLine(c, iw, "PID", "USER", "CPU%", "MEM", "THR", "NAME").c_str());
  attroff(A_BOLD);

  std::vector<const ProcInfo*> rows = sortedProcs(list, v);
  int total = static_cast<int>(rows.size());
  v.scroll = std::clamp(v.scroll, 0, std::max(0, total - rowsVisible));

  for (int i = 0; i < rowsVisible && v.scroll + i < total; ++i) {
    const ProcInfo& p = *rows[v.scroll + i];
    std::string line = tableLine(c, iw, std::to_string(p.pid), p.user, fmtCpu(p),
                                 p.readable ? fmtBytes(p.mem) : "-",
                                 p.readable ? std::to_string(p.threads) : "-", p.name);
    if (!p.readable) color_on(kDim);
    mvaddstr(y + 2 + i, ix, line.c_str());
    if (!p.readable) color_off(kDim);
  }

  // Position on the bottom border: " 1-20 of 727 "
  if (total > 0) {
    char pos[48];
    int last = std::min(total, v.scroll + rowsVisible);
    std::snprintf(pos, sizeof(pos), " %d-%d of %d ", v.scroll + 1, last, total);
    int px = w - 2 - cols(pos);
    if (px > 2) mvaddstr(y + h - 1, px, pos);
  }
  return rowsVisible;
}

// " q quit · +/- interval 1s · c/m/p/n sort · ↑↓ scroll", or a shorter
// form on narrow terminals (%g prints 0.25, 0.5, 1, 2 without noise).
void drawFooter(int y, int w, double interval) {
  char full[96], brief[64];
  std::snprintf(full, sizeof(full),
                "q quit · +/- interval %gs · c/m/p/n sort · ↑↓ scroll", interval);
  std::snprintf(brief, sizeof(brief), "q · +/- %gs · c/m/p/n · ↑↓", interval);
  std::string text = cols(full) <= w - 2 ? full : fit(brief, w - 2);
  color_on(kDim);
  mvaddstr(y, 1, text.c_str());
  color_off(kDim);
}

void drawTooSmall(int h, int w) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "Too small: need %dx%d", kMinWidth, kMinHeight);
  std::string msg = buf;
  if (static_cast<int>(msg.size()) > w) msg = msg.substr(0, w);  // never wrap
  int len = static_cast<int>(msg.size());
  mvaddstr(h / 2, std::max(0, (w - len) / 2), msg.c_str());
}

}  // namespace

Ui::Ui(bool useColor) {
  std::setlocale(LC_ALL, "");  // enables UTF-8 output; must precede initscr
  initscr();
  cbreak();              // keys arrive immediately; Ctrl-C still sends SIGINT
  noecho();
  keypad(stdscr, TRUE);  // arrow keys, KEY_RESIZE
  curs_set(0);
  timeout(100);          // getch waits at most 100 ms

  if (useColor && has_colors()) {
    start_color();
    use_default_colors();  // -1 = keep the terminal's own background
    init_pair(kGreen, COLOR_GREEN, -1);
    init_pair(kYellow, COLOR_YELLOW, -1);
    init_pair(kRed, COLOR_RED, -1);
    init_pair(kAccent, COLOR_CYAN, -1);
    init_pair(kDim, COLOR_BLACK + 8 < COLORS ? COLOR_BLACK + 8 : COLOR_WHITE, -1);
    g_colors = true;
  }
}

Ui::~Ui() { endwin(); }

Ui::Key Ui::readKey() {
  int ch = getch();
  switch (ch) {
    case ERR:        return Key::None;
    case 'q':
    case 'Q':        return Key::Quit;
    case '+':
    case '=':        return Key::Longer;   // '=' is unshifted '+' on US keyboards
    case '-':
    case '_':        return Key::Shorter;
    case 'c': case 'C': return Key::SortCpu;
    case 'm': case 'M': return Key::SortMem;
    case 'p': case 'P': return Key::SortPid;
    case 'n': case 'N': return Key::SortName;
    case KEY_UP:     return Key::Up;
    case KEY_DOWN:   return Key::Down;
    case KEY_PPAGE:  return Key::PageUp;
    case KEY_NPAGE:  return Key::PageDown;
    case KEY_HOME:   return Key::Home;
    case KEY_END:    return Key::End;
    case KEY_RESIZE: return Key::Resize;
    default:         return Key::Other;
  }
}

void Ui::draw(const Snapshot& s, ViewState& view) {
  erase();  // not clear(): erase lets ncurses send only what changed (no flicker)

  int h = LINES, w = COLS;
  if (w < kMinWidth || h < kMinHeight) {
    drawTooSmall(h, w);
    refresh();
    return;
  }

  drawHeader(w, s.sys);
  int y = 1;
  drawCpu(y, w, s.cpu);       y += kPanelHeight;
  drawMemory(y, w, s.mem);    y += kPanelHeight;
  drawDisk(y, w, s.disk);     y += kPanelHeight;
  drawNetwork(y, w, s.net);   y += kNetPanelHeight;

  // The process panel takes the rest of the screen above the footer, if
  // there's room for at least one row; otherwise it's left out.
  int procHeight = (h - 1) - y;
  if (procHeight >= kMinProcPanel) {
    pageRows_ = std::max(1, drawProcesses(y, procHeight, w, s.procs, view));
  }
  drawFooter(h - 1, w, view.interval);

  refresh();
}
