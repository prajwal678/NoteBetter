# NoteBetter - A Terminal-Based Text Editor

NoteBetter is a lightweight, terminal-based text editor written in C, inspired by kilo editor

---

## Prerequisites

- A C11 compiler — GCC or Clang
- Make
- A POSIX system (Linux or macOS)
- OpenMP, for parallel syntax scanning. GCC has it built in; on macOS with Apple Clang you need `brew install libomp`

---

## Installation

Build and install:
```bash
sudo make install
```

To build without installing:
```bash
make
```
This will compile and add the binary to the `bin` directory.

## Usage

This will compile the source code and install the binary to `/usr/local/bin`.
Thus can be run from any terminal like
```bash
notebetter <filename>
```

---

## Uninstallation and cleaning

To remove NoteBetter from your system:
```bash
sudo make uninstall
```

To clean build files:
```bash
make clean
```

---

## Keyboard Shortcuts

- `Ctrl-S`: Save file
- `Ctrl-Q`: Quit (will warn if unsaved changes exist)
- `Ctrl-F`: Find text in file (matches are highlighted; Arrows cycle hits, Enter keeps, Esc cancels)

### Movement

Vim-like but Ctrl-prefixed (this aint modal dawg)
- `Ctrl-H/J/K/L` -> Left / down / up / right
- `Ctrl-A/E` -> Start / end of line
- `Ctrl-B/W` -> Start of previous / next word
- `Ctrl-T/G` -> First / last line of file
- `Ctrl-D/U` -> Half a screen down / up, view on cursor sticky
- Arrow keys still let you navigate
- `Page Up/Down`: Scroll page up/down
- `Home/End`: Move to start/end of line

### Editing
- `Ctrl-C/X/V`

## Features

- Syntax highlighting for C, C++, and Python files
- Line numbers
- Search (Ctrl-F) with the current match highlighted
- Vim-like Ctrl-prefixed motions, half-screen scroll, and a linewise copy/cut/paste register
- Status bar with filename, line count, modified flag, detected filetype and cursor line
- Multi-line editing support
- Atomic saves — writes a temp file and renames, so an interrupted save cannot truncate your file
- Live terminal resize (SIGWINCH), no keypress needed to reflow

---

## Development

```bash
make test
make bench      # build + run the benchmark, generates a 1M line corpus first
make tsan       # ThreadSanitizer build + run of both test and bench
make asan       # AddressSanitizer + UBSan build + run of both test and bench
make config     # to see compiler, openmp config
```

---

## Benchmarking

```bash
make bench                          # generates a 1M line corpus, then times it
make bench CORPUS_LINES=5000000     # bigger corpus
./bin/bench path/to/your/file.c     # time a real file
./bin/bench path/to/file.c 500      # 500 redraw reps instead of 200
```

`bin/bench` links the editor's own object files and drives them without a
terminal, so it reports the editor's internal timings with no process-startup
noise:

```
open          31.51 ms   (924.1 MB/s)   <- mmap, line split, comment scan
first frame    0.16 ms                  <- materialize + colour the visible rows
redraw         0.015 ms  (avg of 200)   <- steady state, nothing to rebuild
scroll        86.05 ms   (2000 pages)   <- cold path, new rows every frame
```

### Comparing against vim

NoteBetter:
```bash
./bin/bench big.c | grep open
```

vim, which has a startup profiler built in:
```bash
script -q /dev/null vim --startuptime /tmp/vim.txt -u NONE -c q big.c
grep 'opening buffers' /tmp/vim.txt     # time to read the file into a buffer
tail -1 /tmp/vim.txt                    # total startup
```

Measured here, 1,000,000 lines / 30 MB of C, Apple M-series, warm page cache:

| | time to open |
|---|---|
| NoteBetter (`bench`, open) | **~31 ms** |
| vim `-u NONE` (opening buffers) | ~49 ms |
| vim `-u NONE` (total startup) | ~62 ms |
| vim `-u NONE -c 'syntax on'` (total) | ~74 ms |
