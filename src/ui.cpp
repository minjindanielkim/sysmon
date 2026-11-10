#include "ui.h"

#include <algorithm>
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

  std::string t = " " + title + " ";
  if (static_cast<int>(t.size()) > w - 4) t = t.substr(0, std::max(0, w - 4));
  attron(A_BOLD);
  mvaddstr(y, x + 2, t.c_str());
  attroff(A_BOLD);
}

// One line of text: `left` on the left, `right` (bold) flush right.
void drawInfoLine(int y, int x, int w, std::string left, const std::string& right) {
  int room = w - static_cast<int>(right.size()) - 1;
  if (room < 0) room = 0;
  if (static_cast<int>(left.size()) > room) left = left.substr(0, room);
  mvaddstr(y, x, left.c_str());
  attron(A_BOLD);
  mvaddstr(y, x + w - static_cast<int>(right.size()), right.c_str());
  attroff(A_BOLD);
}

// Horizontal bar using eighth-blocks for a smooth end: ████▌░░░░
void drawBar(int y, int x, int w, double fraction) {
  static const wchar_t* partials[] = {L"", L"▏", L"▎", L"▍", L"▌", L"▋", L"▊", L"▉"};

  fraction = std::clamp(fraction, 0.0, 1.0);
  int eighths = static_cast<int>(std::lround(fraction * w * 8));
  int full = eighths / 8;
  int part = eighths % 8;

  std::wstring filled(full, L'█');
  filled += partials[part];
  int used = full + (part ? 1 : 0);

  short c = levelColor(fraction);
  color_on(c);
  mvaddwstr(y, x, filled.c_str());
  color_off(c);

  color_on(kDim);
  mvaddwstr(y, x + used, std::wstring(w - used, L'░').c_str());
  color_off(kDim);
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
  drawInfoLine(y + 1, ix, iw, c.model + " · " + std::to_string(c.cores) + " cores",
               fmtPercent(c.usage));
  drawBar(y + 2, ix, iw, c.usage / 100.0);
}

void drawMemory(int y, int w, const MemInfo& m) {
  drawBox(y, 0, kPanelHeight, w, "Memory");
  int ix = 2, iw = w - 4;
  if (!m.ok || m.total == 0) { drawUnavailable(y + 1, ix); return; }
  double frac = static_cast<double>(m.used) / m.total;
  drawInfoLine(y + 1, ix, iw, fmtBytes(m.used) + " used of " + fmtBytes(m.total),
               fmtPercent(frac * 100));
  drawBar(y + 2, ix, iw, frac);
}

void drawDisk(int y, int w, const DiskInfo& d) {
  drawBox(y, 0, kPanelHeight, w, "Disk · " + d.mount);
  int ix = 2, iw = w - 4;
  if (!d.ok || d.total == 0) { drawUnavailable(y + 1, ix); return; }
  double frac = static_cast<double>(d.used) / d.total;
  drawInfoLine(y + 1, ix, iw, fmtBytes(d.used) + " used of " + fmtBytes(d.total),
               fmtPercent(frac * 100));
  drawBar(y + 2, ix, iw, frac);
}

void drawHeader(int w) {
  attron(A_BOLD);
  color_on(kAccent);
  mvaddstr(0, 1, "sysmon");
  color_off(kAccent);
  attroff(A_BOLD);

  char clock[16];
  std::time_t now = std::time(nullptr);
  std::strftime(clock, sizeof(clock), "%H:%M:%S", std::localtime(&now));
  mvaddstr(0, w - 1 - static_cast<int>(std::string(clock).size()), clock);
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

  drawHeader(w);
  int y = 1;
  drawCpu(y, w, s.cpu);       y += kPanelHeight;
  drawMemory(y, w, s.mem);    y += kPanelHeight;
  drawDisk(y, w, s.disk);
  drawFooter(h - 1);

  refresh();
}
