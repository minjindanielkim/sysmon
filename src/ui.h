#pragma once

#include "sampler.h"

enum class SortKey { Cpu, Mem, Pid, Name };

// What the user has chosen to look at. Owned by main, changed by key
// presses, read (and scroll clamped) by Ui::draw.
struct ViewState {
  double interval = 1.0;        // seconds between samples, shown in the footer
  SortKey sort = SortKey::Cpu;
  bool descending = true;
  int scroll = 0;               // first visible row of the process table
};

// Owns the ncurses screen. Constructing it switches the terminal into
// full-screen mode; destroying it (including on Ctrl-C, via main's loop)
// restores the terminal. Only this file's .cpp includes curses.h, so its
// macros (erase, clear, move, ...) can't collide with the rest of the code.
class Ui {
public:
  enum class Key {
    None, Quit, Longer, Shorter, Resize, Other,
    SortCpu, SortMem, SortPid, SortName,
    Up, Down, PageUp, PageDown, Home, End,
  };

  explicit Ui(bool useColor);
  ~Ui();
  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  // Waits up to ~100 ms for a key press. Returns Key::None if nothing came.
  Key readKey();

  // Redraws the whole screen. Clamps view.scroll to the rows that exist.
  void draw(const Snapshot& s, ViewState& view);

  // Process rows visible in the last draw (for PgUp/PgDn).
  int pageRows() const { return pageRows_; }

private:
  int pageRows_ = 10;
};
