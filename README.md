# sysmon

A live terminal dashboard for CPU, memory and disk usage on macOS, written in
C++17 with ncurses.

```
 sysmon   load 1.91 1.54 1.47 · up 1d 3h               11:30:22
┌─ CPU ────────────────────────────────────────────────────────┐
│ Apple M5 · 10 cores                                     9.6% │
│ [------Co  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  o  ] │
└──────────────────────────────────────────────────────────────┘
┌─ Memory · pressure normal ───────────────────────────────────┐
│ 10.9 GB used of 16.0 GB · no swap                      68.3% │
│ [----------------------------------------C  o  o  o  o  o  ] │
└──────────────────────────────────────────────────────────────┘
┌─ Disk · /System/Volumes/Data ────────────────────────────────┐
│ 228.1 GB used of 460.4 GB                              49.6% │
│ [-----------------------------C o  o  o  o  o  o  o  o  o  ] │
└──────────────────────────────────────────────────────────────┘
 q quit · +/- interval 1s
```

- **CPU:** model, core count and overall usage
- **Memory:** used vs total, swap, and macOS's memory pressure level. The bar
  colour follows pressure rather than percentage, since macOS deliberately
  fills spare RAM with cache.
- **Disk:** used vs total for one volume
- **Header:** 1/5/15-minute load averages, uptime and the time

The bars are drawn in the style of pacman's `ILoveCandy` option: Pac-Man
eats the pellets as usage rises.

## Requirements

- macOS (Linux support is planned but not there yet)
- CMake 3.16 or newer
- A C++17 compiler (the Xcode Command Line Tools are enough)

ncurses ships with macOS, so there is nothing else to install.

## Build and run

```bash
cmake -S . -B build
cmake --build build
./build/sysmon
```

## Install

```bash
cmake --install build --prefix ~/.local
```

This puts `sysmon` in `~/.local/bin`; make sure that directory is on your
`PATH`. Without `--prefix` it installs to `/usr/local/bin`, which may need
`sudo`.

## Options

| Option | Meaning |
| --- | --- |
| `-i`, `--interval SECONDS` | Time between updates. Default 1, range 0.25–60. |
| `-d`, `--disk PATH` | Volume to show disk usage for. Default `/System/Volumes/Data`. |
| `--no-color` | Disable colours. Setting the `NO_COLOR` environment variable does the same. |
| `-h`, `--help` | Show help and exit. |

Example: `sysmon --interval 2 --disk /Volumes/External`

## Keys

| Key | Action |
| --- | --- |
| `q` | Quit (Ctrl-C also works) |
| `+` / `-` | Lengthen / shorten the update interval |

## Project layout

| File | Purpose |
| --- | --- |
| `src/stats.h` | Data structures and the functions that read system stats |
| `src/stats_macos.cpp` | CPU, memory, swap, pressure, load and uptime on macOS |
| `src/stats_posix.cpp` | Disk usage (portable) |
| `src/sampler.*` | Collects one snapshot of everything per update |
| `src/options.*` | Command-line parsing and interval steps |
| `src/ui.*` | All ncurses drawing |
| `src/main.cpp` | Main loop: draw, read keys, sample |
