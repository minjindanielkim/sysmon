#include "ui.h"

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

// Wide-character ncurses API (needed for █ ▏ ░ etc.). Must come before
// curses.h, and curses.h comes last because it defines short macros.
#define NCURSES_WIDECHAR 1
#include <curses.h>

namespace {

// Colour pair ids
enum Pair : short { kGreen = 1, kYellow, kRed, kAccent, kDim };

constexpr int kMinWidth = 40;
constexpr int kPanelHeight = 4;  // border, info line, bar line, border
constexpr int kNumPanels = 3;
constexpr int kMinHeight = 1 + kNumPanels * kPanelHeight + 1;  // header + panels + footer

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

void drawFooter(int y) {
  color_on(kDim);
  mvaddstr(y, 1, "q quit · updates every 1s");
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

Ui::Ui() {
  std::setlocale(LC_ALL, "");  // enables UTF-8 output; must precede initscr
  initscr();
  cbreak();              // keys arrive immediately; Ctrl-C still sends SIGINT
  noecho();
  keypad(stdscr, TRUE);  // arrow keys, KEY_RESIZE
  curs_set(0);
  timeout(100);          // getch waits at most 100 ms

  if (has_colors()) {
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
    case KEY_RESIZE: return Key::Resize;
    default:         return Key::Other;
  }
}

void Ui::draw(const Snapshot& s) {
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
  drawDisk(y, w, s.disk);
  drawFooter(h - 1);

  refresh();
}
