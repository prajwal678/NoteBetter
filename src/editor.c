#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "editor.h"
#include "buffer.h"
#include "row.h"
#include "terminal.h"
#include "input.h"
#include "output.h"
#include "fileio.h"
#include "highlight.h"
#include "highlight_thread.h"


void editorInsertChar(int c) {
    if (CONFIG.cursor_y == CONFIG.numrows) {
        editorInsertRow(CONFIG.numrows, "", 0);
    }
    if (CONFIG.cursor_y >= CONFIG.numrows) {
        return;
    }

    rowInsertChar(&CONFIG.row[CONFIG.cursor_y], CONFIG.cursor_x, c);
    CONFIG.cursor_x++;
}

void editorInsertNewline(void) {
    if (CONFIG.cursor_x == 0) {
        editorInsertRow(CONFIG.cursor_y, "", 0);
    }
    else {
        ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];
        // s points into row->string, not into CONFIG.row, so the realloc inside editorInsertRow cannot invalidate it
        editorInsertRow(CONFIG.cursor_y + 1, &row->string[CONFIG.cursor_x], (size_t)(row->size - CONFIG.cursor_x));

        row = &CONFIG.row[CONFIG.cursor_y];
        if (!rowMakeOwned(row)) {
            return;
        }
        row->size = CONFIG.cursor_x;
        row->string[row->size] = '\0';
        updateRow(row);
    }
    CONFIG.cursor_y++;
    CONFIG.cursor_x = 0;
}

void editorDelChar(void) {
    if (CONFIG.cursor_y == CONFIG.numrows) {
        return;
    }
    if (CONFIG.cursor_x == 0 && CONFIG.cursor_y == 0) {
        return;
    }

    ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];
    if (CONFIG.cursor_x > 0) {
        rowDelChar(row, CONFIG.cursor_x - 1);
        CONFIG.cursor_x--;
    }
    else {
        CONFIG.cursor_x = CONFIG.row[CONFIG.cursor_y - 1].size;
        rowAppendString(&CONFIG.row[CONFIG.cursor_y - 1], row->string, (size_t)row->size);
        editorDelRow(CONFIG.cursor_y);
        CONFIG.cursor_y--;
    }
}

static int saved_hl_line = -1;
static unsigned char *saved_hl;

static void restoreSearchHighlight(void) {
    if (saved_hl == NULL) {
        return;
    }

    if (saved_hl_line >= 0 && saved_hl_line < CONFIG.numrows) {
        ROW_DATA *row = &CONFIG.row[saved_hl_line];
        if (row->hl != NULL && row->render_size > 0) {
            memcpy(row->hl, saved_hl, (size_t)row->render_size);
        }
    }

    free(saved_hl);
    saved_hl = NULL;
    saved_hl_line = -1;
}

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

    restoreSearchHighlight();

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;

        return;
    }
    else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    }
    else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    }
    else {
        last_match = -1;
        direction = 1;
    }

    size_t qlen = strlen(query);
    if (qlen == 0) {
        return;
    }
    if (last_match == -1) {
        direction = 1;
    }

    int current = last_match;

    for (int i = 0; i < CONFIG.numrows; i++) {
        current += direction;
        if (current == -1) {
            current = CONFIG.numrows - 1;
        }
        else if (current == CONFIG.numrows) {
            current = 0;
        }

        ROW_DATA *row = &CONFIG.row[current];
        // row->string is a slice of the mmap and has no nul, so memmem not strstr
        char *match = memmem(row->string, (size_t)row->size, query, qlen);
        if (match == NULL) {
            continue;
        }

        last_match = current;
        CONFIG.cursor_y = current;
        CONFIG.cursor_x = (int)(match - row->string);
        CONFIG.row_offset = CONFIG.numrows;  // force scroll to put hit on screen

        editorRowEnsureRender(row);
        editorHighlightRow(row);

        if (row->hl != NULL && row->render_size > 0) {
            saved_hl = malloc((size_t)row->render_size);
            if (saved_hl != NULL) {
                memcpy(saved_hl, row->hl, (size_t)row->render_size);
                saved_hl_line = current;

                int rx = editorRowCxToRx(row, CONFIG.cursor_x);
                int n = (int)qlen;
                if (rx + n > row->render_size) {
                    n = row->render_size - rx;
                }
                if (n > 0) {
                    memset(&row->hl[rx], HL_MATCH, (size_t)n);
                }
            }
        }
        break;
    }
}

void editorFind(void) {
    int saved_cx = CONFIG.cursor_x;
    int saved_cy = CONFIG.cursor_y;
    int saved_coloff = CONFIG.column_offset;
    int saved_rowoff = CONFIG.row_offset;

    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);

    if (query) {
        free(query);
    }
    else {
        CONFIG.cursor_x = saved_cx;
        CONFIG.cursor_y = saved_cy;
        CONFIG.column_offset = saved_coloff;
        CONFIG.row_offset = saved_rowoff;
    }
    restoreSearchHighlight();
}

void editorScroll(void) {
    CONFIG.render_x = 0;

    if (CONFIG.cursor_y < CONFIG.numrows) {
        CONFIG.render_x = editorRowCxToRx(&CONFIG.row[CONFIG.cursor_y], CONFIG.cursor_x);
    }
    if (CONFIG.cursor_y < CONFIG.row_offset) {
        CONFIG.row_offset = CONFIG.cursor_y;
    }
    if (CONFIG.cursor_y >= CONFIG.row_offset + CONFIG.screen_rows) {
        CONFIG.row_offset = CONFIG.cursor_y - CONFIG.screen_rows + 1;
    }
    if (CONFIG.render_x < CONFIG.column_offset) {
        CONFIG.column_offset = CONFIG.render_x;
    }
    if (CONFIG.render_x >= CONFIG.column_offset + CONFIG.text_columns) {
        CONFIG.column_offset = CONFIG.render_x - CONFIG.text_columns + 1;
    }
    if (CONFIG.row_offset < 0) {
        CONFIG.row_offset = 0;
    }
    if (CONFIG.column_offset < 0) {
        CONFIG.column_offset = 0;
    }
}

void editorMoveCursor(int key) {
    ROW_DATA *row = (CONFIG.cursor_y >= CONFIG.numrows) ? NULL : &CONFIG.row[CONFIG.cursor_y];

    switch (key) {
        case ARROW_LEFT:
            if (CONFIG.cursor_x != 0) {
                CONFIG.cursor_x--;
            }
            else if (CONFIG.cursor_y > 0) {
                CONFIG.cursor_y--;
                CONFIG.cursor_x = CONFIG.row[CONFIG.cursor_y].size;
            }
            break;

        case ARROW_RIGHT:
            if (row && CONFIG.cursor_x < row->size) {
                CONFIG.cursor_x++;
            }
            else if (row && CONFIG.cursor_x == row->size) {
                CONFIG.cursor_y++;
                CONFIG.cursor_x = 0;
            }
            break;

        case ARROW_UP:
            if (CONFIG.cursor_y != 0) {
                CONFIG.cursor_y--;
            }
            break;

        case ARROW_DOWN:
            if (CONFIG.cursor_y < CONFIG.numrows) {
                CONFIG.cursor_y++;
            }
            break;
    }

    row = (CONFIG.cursor_y >= CONFIG.numrows) ? NULL : &CONFIG.row[CONFIG.cursor_y];
    int rowlen = row ? row->size : 0;
    if (CONFIG.cursor_x > rowlen) {
        CONFIG.cursor_x = rowlen;
    }
}

static int isWordChar(int c) {
    return (c != -1 && (isalnum(c) || c == '_'));
}

static int charAt(void) {
    ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];

    return ((CONFIG.cursor_x < row->size) ? (unsigned char)row->string[CONFIG.cursor_x] : -1);
}

// one byte forward or back, crossing line ends; 0 if there is nowhere left to go; never leaves cursor_y == numrows, so charAt is always in range
static int stepChar(int dir) {
    if (dir > 0) {
        if (CONFIG.cursor_x < CONFIG.row[CONFIG.cursor_y].size) {
            CONFIG.cursor_x++;

            return 1;
        }
        if (CONFIG.cursor_y + 1 < CONFIG.numrows) {
            CONFIG.cursor_y++;
            CONFIG.cursor_x = 0;

            return 1;
        }

        return 0;
    }

    if (CONFIG.cursor_x > 0) {
        CONFIG.cursor_x--;

        return 1;
    }
    if (CONFIG.cursor_y > 0) {
        CONFIG.cursor_y--;
        CONFIG.cursor_x = CONFIG.row[CONFIG.cursor_y].size;

        return 1;
    }

    return 0;
}

// dir > 0: start of the next word, dir < 0: start of the previous word
void editorMoveWord(int dir) {
    if (CONFIG.numrows == 0) {
        return;
    }
    if (CONFIG.cursor_y >= CONFIG.numrows) {
        CONFIG.cursor_y = CONFIG.numrows - 1;
    }

    int c = charAt();
    if (dir > 0) {
        // out of the word we are standing in, then across the gap
        while (isWordChar(c) && stepChar(1)) {
            c = charAt();
        }
        while (!isWordChar(c) && stepChar(1)) {
            c = charAt();
        }

        return;
    }

    if (!stepChar(-1)) {
        return;
    }
    c = charAt();
    while (!isWordChar(c) && stepChar(-1)) {
        c = charAt();
    }

    ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];
    while (CONFIG.cursor_x > 0 && isWordChar((unsigned char)row->string[CONFIG.cursor_x - 1])) {
        CONFIG.cursor_x--;
    }
}

void editorScrollHalfPage(int dir) {
    int half = CONFIG.screen_rows / 2;
    if (half < 1) {
        half = 1;  // screen_rows floors at 1, see editorResize
    }

    int last = CONFIG.numrows ? CONFIG.numrows - 1 : 0;

    CONFIG.cursor_y += dir * half;
    if (CONFIG.cursor_y > last) {
        CONFIG.cursor_y = last;
    }
    if (CONFIG.cursor_y < 0) {
        CONFIG.cursor_y = 0;
    }

    CONFIG.row_offset += dir * half;
    if (CONFIG.row_offset > last) {
        CONFIG.row_offset = last;
    }
    if (CONFIG.row_offset < 0) {
        CONFIG.row_offset = 0;
    }

    int rowlen = (CONFIG.cursor_y < CONFIG.numrows) ? CONFIG.row[CONFIG.cursor_y].size : 0;
    if (CONFIG.cursor_x > rowlen) {
        CONFIG.cursor_x = rowlen;
    }
}

static APPEND_BUFFER yank_register = ABUF_INIT;
// appendBufferAppend is a no-op for len 0, so yanking an empty line leaves buffer NULL; this flag is what separates that from never having yanked
static int yank_valid;

void editorYankLine(void) {
    if (CONFIG.cursor_y >= CONFIG.numrows) {
        return;
    }

    ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];
    yank_register.length = 0;
    appendBufferAppend(&yank_register, row->string, row->size);
    yank_valid = 1;

    setStatusMessage("1 line yanked");
}

void editorCutLine(void) {
    if (CONFIG.cursor_y >= CONFIG.numrows) {
        return;
    }

    editorYankLine();  // copies first, editorDelRow frees row->string
    editorDelRow(CONFIG.cursor_y);

    if (CONFIG.cursor_y >= CONFIG.numrows) {
        CONFIG.cursor_y = CONFIG.numrows ? CONFIG.numrows - 1 : 0;
    }
    int rowlen = (CONFIG.cursor_y < CONFIG.numrows) ? CONFIG.row[CONFIG.cursor_y].size : 0;
    if (CONFIG.cursor_x > rowlen) {
        CONFIG.cursor_x = rowlen;
    }

    setStatusMessage("1 line katt");
}

void editorPasteLine(void) {
    if (!yank_valid) {
        return;
    }

    int at = (CONFIG.cursor_y >= CONFIG.numrows) ? CONFIG.numrows : CONFIG.cursor_y + 1;
    editorInsertRow(at, yank_register.buffer ? yank_register.buffer : "", (size_t)yank_register.length); // editorInsertRow memcpys len bytes and memcpy from NULL is undefined even for a length of 0
    CONFIG.cursor_y = at;
    CONFIG.cursor_x = 0;
}

void editorFreeYank(void) {
    appendBufferFree(&yank_register);
    yank_valid = 0;
}

void editorResize(void) {
    if (getWindowSize(&CONFIG.screen_rows, &CONFIG.screen_columns) == -1) {
        return;
    }
    CONFIG.screen_rows -= 2;
    if (CONFIG.screen_rows < 1) {
        CONFIG.screen_rows = 1;
    }
    editorUpdateGutter();
}

void editorCleanup(void) {
    highlightThreadShutdown();  // first so nothing else may touch it later
    editorCloseFile();
    editorFreeOutput();
    editorFreeYank();
}

void editorProcessKeypress(void) {
    static int quit_times = QUIT_TIMES;
    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewline();
            break;

        case CTRL_KEY('q'):
            if (CONFIG.dirty && quit_times > 0) {
                setStatusMessage("WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;

                return;
            }

            editorCleanup();
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            disableRawMode();
            exit(0);
            break;

        case CTRL_KEY('s'):
            editorSave();
            break;

        case CTRL_KEY('f'):
            editorFind();
            break;

        case HOME_KEY:
        case CTRL_KEY('a'):
            CONFIG.cursor_x = 0;
            break;
        case END_KEY:
        case CTRL_KEY('e'):
            if (CONFIG.cursor_y < CONFIG.numrows) {
                CONFIG.cursor_x = CONFIG.row[CONFIG.cursor_y].size;
            }
            break;

        case CTRL_KEY('w'):
            editorMoveWord(1);
            break;
        case CTRL_KEY('b'):
            editorMoveWord(-1);
            break;

        case CTRL_KEY('t'):
            CONFIG.cursor_y = 0;
            CONFIG.cursor_x = 0;
            break;
        case CTRL_KEY('g'):
            CONFIG.cursor_y = CONFIG.numrows ? CONFIG.numrows - 1 : 0;
            CONFIG.cursor_x = 0;
            break;

        case CTRL_KEY('d'):
            editorScrollHalfPage(1);
            break;
        case CTRL_KEY('u'):
            editorScrollHalfPage(-1);
            break;

        case CTRL_KEY('c'):
            editorYankLine();
            break;
        case CTRL_KEY('x'):
            editorCutLine();
            break;
        case CTRL_KEY('v'):
            editorPasteLine();
            break;

        case BACKSPACE:
        case DELETE_KEY:
            if (c == DELETE_KEY) {
                editorMoveCursor(ARROW_RIGHT);
            }
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    CONFIG.cursor_y = CONFIG.row_offset;
                }
                else {
                    CONFIG.cursor_y = CONFIG.row_offset + CONFIG.screen_rows - 1;
                    if (CONFIG.cursor_y > CONFIG.numrows) {
                        CONFIG.cursor_y = CONFIG.numrows;
                    }
                }

                int times = CONFIG.screen_rows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('k'):
            editorMoveCursor(ARROW_UP);
            break;
        case CTRL_KEY('j'):
            editorMoveCursor(ARROW_DOWN);
            break;
        case CTRL_KEY('h'):
            editorMoveCursor(ARROW_LEFT);
            break;
        case CTRL_KEY('l'):
            editorMoveCursor(ARROW_RIGHT);
            break;

        case REFRESH_KEY:
        case '\x1b':
            break;

        default:
            editorInsertChar(c);
            break;
    }

    quit_times = QUIT_TIMES;
}

void initEditor(void) {
    CONFIG.cursor_x = 0;
    CONFIG.cursor_y = 0;
    CONFIG.render_x = 0;
    CONFIG.row_offset = 0;
    CONFIG.column_offset = 0;
    CONFIG.numrows = 0;
    CONFIG.rowcap = 0;
    CONFIG.row = NULL;
    CONFIG.dirty = 0;
    CONFIG.filename = NULL;
    CONFIG.map = NULL;
    CONFIG.map_len = 0;
    CONFIG.status_message[0] = '\0';
    CONFIG.status_message_time = 0;
    CONFIG.syntax = NULL;

    if (getWindowSize(&CONFIG.screen_rows, &CONFIG.screen_columns) == -1) {
        die("getWindowSize");
    }
    CONFIG.screen_rows -= 2; // status bar + message bar
    editorUpdateGutter();

    highlightThreadInit();
}
