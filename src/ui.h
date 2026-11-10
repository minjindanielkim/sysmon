#pragma once

#include "sampler.h"

// Owns the ncurses screen. Constructing it switches the terminal into
// full-screen mode; destroying it (including on Ctrl-C, via main's loop)
// restores the terminal. Only this file's .cpp includes curses.h, so its
// macros (erase, clear, move, ...) can't collide with the rest of the code.
class Ui {
public:
  enum class Key { None, Quit, Longer, Shorter, Resize, Other };

  explicit Ui(bool useColor);
  ~Ui();
  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  // Waits up to ~100 ms for a key press. Returns Key::None if nothing came.
  Key readKey();

  // Redraws the whole screen from the given snapshot; `interval` (seconds)
  // is shown in the footer.
  void draw(const Snapshot& s, double interval);
};
