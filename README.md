# sysmon

A live terminal dashboard for CPU, memory, disk, network and processes on
macOS, written in C++17 with ncurses.

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
┌─ Network · en0 ──────────────────────────────────────────────┐
│ ↓ 98.7 KB/s    ↑ 3.6 KB/s          total ↓ 7.0 GB ↑ 407.2 MB │
└──────────────────────────────────────────────────────────────┘
┌─ Processes · 733 (423 readable) · sort: CPU ▼ ───────────────┐
│    PID   CPU%       MEM  THR  NAME                           │
│  27461   99.8  832.2 KB    1  yes                            │
│  26119    1.4  610.4 MB   27  Claude Helper (Renderer)       │
│  21565    0.3  283.6 MB   22  Microsoft Outlook              │
└───────────────────────────────────────────────── 1-3 of 733 ─┘
 q quit · +/- interval 1s · c/m/p/n sort · ↑↓ scroll · g graphs
```

- **CPU:** model, core count and overall usage
- **Memory:** used vs total, swap, and macOS's memory pressure level. The bar
  colour follows pressure rather than percentage, since macOS deliberately
  fills spare RAM with cache.
- **Disk:** used vs total for one volume
- **Network:** download and upload speed, combined across Wi-Fi and
  Ethernet, plus the totals since each interface came up
- **Processes:** every running process with its CPU %, memory and thread
  count, sortable and scrollable (see below)
- **Header:** 1/5/15-minute load averages, uptime and the time

The bars are drawn in the style of pacman's `ILoveCandy` option: Pac-Man
eats the pellets as usage rises.

## Graphs

Press `g` (or start with `--graphs`) to swap the CPU and memory bars for
graphs of recent history, and to add one to the network panel. They are
drawn with braille characters, so each character cell holds two samples
and four levels:

```
┌─ Network · en0 ──────────────────────┐
│ ↓ 67.0 KB/s    ↑ 2.8 KB/s            │
│ ⣀⣀⣀⣤⣄⣀⣄⣠⣀⣀⣀⣄⣀⣀⣄⣀⣀⣄⣄⣀⣀⣀⣀⣀⣄⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀⣀ │
│ ⠉⠉⠈⠘⠁⠉⠁⠈⠈⠉⠈⠁⠈⠉⠉⠉ ⠉⠉⠉⠉⠉⠙⠈⠁⠁⠁⠋⠉⠉⠉⠉⠉⠉⠉⠈ │
└──────── scale ↓ 500 KB/s ↑ 200 KB/s ─┘
```

- The newest sample is at the right; one sample is taken per update, so
  the time a graph covers depends on the interval.
- CPU shades from green to red going up. Memory uses its pressure colour.
- Network shows download above the midline and upload below it. Each half
  scales to the largest value on screen, rounded up, and the scale is
  shown on the panel's bottom border.
- Graphs grow taller (up to 8 rows) on taller terminals while leaving room
  for the process list. If the terminal is too short for them, the bars
  stay.

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
| `-g`, `--graphs` | Start with graphs instead of bars. |
| `--no-color` | Disable colours. Setting the `NO_COLOR` environment variable does the same. |
| `-h`, `--help` | Show help and exit. |

Example: `sysmon --interval 2 --disk /Volumes/External`

## Keys

| Key | Action |
| --- | --- |
| `q` | Quit (Ctrl-C also works) |
| `+` / `-` | Lengthen / shorten the update interval |
| `g` | Switch between bars and graphs |
| `c` `m` `p` `n` | Sort processes by CPU, memory, PID or name (press again to reverse) |
| `↑` `↓` | Scroll the process list one row |
| `PgUp` `PgDn` | Scroll one page |
| `Home` `End` | Jump to the top / bottom |

## About the network panel

- Speeds are averaged over the last update interval, in bytes per second
  (as Activity Monitor shows them, not bits).
- Only physical interfaces (Wi-Fi, Ethernet) are counted. Loopback traffic
  never leaves the Mac, and VPN tunnels carry packets that also cross
  Wi-Fi or Ethernet, so including them would count the same bytes twice.
- The title lists the interfaces that have carried traffic.

## About the process list

- **CPU %** is per core, as in `top` and Activity Monitor, so a busy
  multi-threaded app can show more than 100%. A process that has only just
  started shows `--` until the next update.
- **MEM** is the memory footprint, the same figure as Activity Monitor's
  Memory column. It differs from the resident size (RSS) that `ps` and `top`
  show.
- **Other users' processes** (root, system services such as WindowServer)
  can only be listed by name and owner unless sysmon runs as root, so they
  appear dimmed with `-` and stay at the bottom of the list. Run
  `sudo sysmon` to see their figures too.

## Project layout

| File | Purpose |
| --- | --- |
| `src/stats.h` | Data structures and the functions that read system stats |
| `src/stats_macos.cpp` | CPU, memory, swap, pressure, load and uptime on macOS |
| `src/stats_posix.cpp` | Disk usage (portable) |
| `src/net.h`, `src/net_macos.cpp` | Network rates and totals via the interface MIB |
| `src/proc.h`, `src/proc_macos.cpp` | Process list via libproc, per-process CPU % |
| `src/sampler.*` | Collects one snapshot of everything per update, plus graph history |
| `src/options.*` | Command-line parsing and interval steps |
| `src/ui.*` | All ncurses drawing |
| `src/main.cpp` | Main loop: draw, read keys, sample |
