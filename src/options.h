#pragma once

#include <string>

// Settings from the command line (and the NO_COLOR environment variable).
struct Options {
  double interval = 1.0;  // seconds between samples
  std::string disk;       // volume to report; set to the platform default
  bool color = true;
  bool graphs = false;    // start with graphs instead of bars
  bool help = false;
};

constexpr double kMinInterval = 0.25;
constexpr double kMaxInterval = 60.0;

// Fills `out` from argv. On a bad argument returns false and sets `error`
// to a one-line message (without the "sysmon: " prefix).
bool parseArgs(int argc, char** argv, Options& out, std::string& error);

// The text printed by --help.
const char* helpText();

// Next interval step for the +/- keys: direction > 0 picks the next longer
// preset, direction < 0 the next shorter one. Stays put at either end.
// Works from any starting value, including ones not in the preset list.
double stepInterval(double current, int direction);
