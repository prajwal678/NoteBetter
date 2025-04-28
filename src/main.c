#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "config.h"
#include "terminal.h"
#include "input.h"
#include "output.h"
#include "fileio.h"
#include "editor.h"
#include "highlight_thread.h"

EDITOR_CONFIG CONFIG;

static void sigHandler(int sig) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    
    char debug_msg[80];
    int len = snprintf(debug_msg, sizeof(debug_msg), "\r\nExiting due to signal %d\r\n", sig);
    write(STDOUT_FILENO, debug_msg, len);
    
    highlightThreadShutdown();
    disableRawMode();
    
    exit(128 + sig);
}

int main(int argc, char *argv[]) {
    signal(SIGTERM, sigHandler);
    
    enableRawMode();
    initEditor();
    
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    setStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        highlightThreadProcess();
        
        editorRefreshScreen();
        editorProcessKeypress();
    }
    
    highlightThreadShutdown();
    disableRawMode();
    
    return 0;
}