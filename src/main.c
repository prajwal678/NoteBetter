#include <stdio.h>
#include <stdlib.h>
#include "config.h"
#include "terminal.h"
#include "input.h"
#include "output.h"
#include "fileio.h"
#include "editor.h"

EDITOR_CONFIG CONFIG;


int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    
    if (argc >= 2) {
        editorOpen(argv[1]);
    }

    setStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}