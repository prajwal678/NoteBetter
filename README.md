# NoteBetter - A Terminal-Based Text Editor

NoteBetter is a lightweight, terminal-based text editor written in C, inspired by kilo editor.

---

## Features

- Syntax highlighting for C/C++ files
- Search functionality (Ctrl-F)
- Simple and intuitive keyboard shortcuts
- Status bar with file information
- Line numbers
- Auto-save warnings
- Multi-line editing support

---

## Prerequisites

- GCC compiler
- Make build system
- Linux/Unix-based operating system

---

## Installation

1. Clone the repository:
```bash
git clone https://github.com/prajwal678/NoteBetter.git
cd NoteBetter
```

2. Build and install:
```bash
sudo make install
```

3. To build without installing:
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
- `Ctrl-F`: Find text in file
- `Ctrl-H` or `Backspace`: Delete character before cursor
- `Arrow Keys`: Move cursor
- `Page Up/Down`: Scroll page up/down
- `Home/End`: Move to start/end of line