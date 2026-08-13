#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include "input.h"
#include "config.h"
#include "terminal.h"
#include "output.h"
#include "highlight_thread.h"


int editorReadKey(void) {
    int nread;
    unsigned char c;

    while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1) {
        // stdin closed; without this the loop spins forever on EOF
        if (nread == 0) {
            highlightThreadPause();

            return CTRL_KEY('q');
        }
        if (nread == -1 && errno == EINTR) {
            // a signal (SIGWINCH) cut the read short; hand back the noop
            // refresh key so the main loop repaints instead of hanging
            highlightThreadPause();

            return CTRL_KEY('l');
        }
        if (nread == -1 && errno != EAGAIN) {
            die("read");
        }
    }

    // single choke point: a key arrived, so the caller is about to mutate rows
    // park the prefetcher here and every caller is covered without having to remember and editorRefreshScreen restarts it
    highlightThreadPause();

    if (c != '\x1b') {
        return c;
    }

    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
        return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
        return '\x1b';
    }

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
            if (read(STDIN_FILENO, &seq[2], 1) != 1) {
                return '\x1b';
            }
            if (seq[2] == '~') {
                switch (seq[1]) {
                    case '1': return HOME_KEY;
                    case '3': return DELETE_KEY;
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                }
            }
        }
        else {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
    }
    else if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
    }

    return '\x1b';
}

// returns a malloc'd string the caller frees, or NULL if cancelled
char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = malloc(bufsize);
    if (buf == NULL) {
        return NULL;
    }

    size_t buflen = 0;
    buf[0] = '\0';

    for (;;) {
        setStatusMessage(prompt, buf);
        editorRefreshScreen();

        int c = editorReadKey();
        if (c == DELETE_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) {
                buf[--buflen] = '\0';
            }
        }
        else if (c == '\x1b') {
            setStatusMessage("");
            if (callback) {
                callback(buf, c);
            }
            free(buf);

            return NULL;
        }
        else if (c == '\r') {
            if (buflen != 0) {
                setStatusMessage("");
                if (callback) {
                    callback(buf, c);
                }

                return buf;
            }
        }
        else if (!iscntrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                char *tmp = realloc(buf, bufsize * 2);
                if (tmp == NULL) {
                    continue;  // keep what we have
                }
                buf = tmp;
                bufsize *= 2;
            }
            buf[buflen++] = (char)c;
            buf[buflen] = '\0';
        }

        if (callback) {
            callback(buf, c);
        }
    }
}
