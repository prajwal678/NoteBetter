#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "config.h"
#include "terminal.h"
#include "input.h"
#include "output.h"
#include "fileio.h"
#include "editor.h"
#include "highlight_thread.h"

EDITOR_CONFIG CONFIG;

volatile sig_atomic_t nb_winch_pending;

static void onWinch(int sig) {
    (void)sig;
    nb_winch_pending = 1;
}

static void onTerm(int sig) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &CONFIG.pre_editor_terminal_config);
    _exit(128 + sig);
}

static void installHandler(int sig, void (*fn)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, NULL);
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    installHandler(SIGTERM, onTerm);
    installHandler(SIGHUP, onTerm);
    installHandler(SIGWINCH, onWinch);

    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    setStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    for (;;) {
        highlightThreadProcess();

        if (nb_winch_pending) {
            nb_winch_pending = 0;
            editorResize();
        }

        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
