#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "editor.h"
#include "row.h"
#include "terminal.h"
#include "input.h"
#include "output.h"
#include "highlight.h"
#include "fileio.h"
#include "highlight_thread.h"


void editorInsertChar(int c) {
    if (CONFIG.cursor_y == CONFIG.numrows) {
        editorInsertRow(CONFIG.numrows, "", 0);
    }
    rowInsertChar(&CONFIG.row[CONFIG.cursor_y], CONFIG.cursor_x, c);
    CONFIG.cursor_x++;
    CONFIG.dirty++;
}

void editorInsertNewline(void) {
    if (CONFIG.cursor_x == 0) {
        editorInsertRow(CONFIG.cursor_y, "", 0);
    }
    else {
        ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];
        editorInsertRow(CONFIG.cursor_y + 1, 
                       &row->string[CONFIG.cursor_x],
                       row->size - CONFIG.cursor_x);
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
    if (CONFIG.cursor_y == CONFIG.numrows) return;
    if (CONFIG.cursor_x == 0 && CONFIG.cursor_y == 0) return;

    ROW_DATA *row = &CONFIG.row[CONFIG.cursor_y];
    if (CONFIG.cursor_x > 0) {
        rowDelChar(row, CONFIG.cursor_x - 1);
        CONFIG.cursor_x--;
    }
    else {
        CONFIG.cursor_x = CONFIG.row[CONFIG.cursor_y - 1].size;
        rowAppendString(&CONFIG.row[CONFIG.cursor_y - 1],
                       row->string,
                       row->size);
        editorDelRow(CONFIG.cursor_y);
        CONFIG.cursor_y--;
    }
    CONFIG.dirty++;
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
}

void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;

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

    if (last_match == -1) direction = 1;
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
        char *match = memmem(row->string, (size_t)row->size, query, strlen(query));
        if (match) {
            last_match = current;
            CONFIG.cursor_y = current;
            CONFIG.cursor_x = match - row->string;
            CONFIG.row_offset = CONFIG.numrows;
            break;
        }
    }
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
    // the text area is the screen minus the line number gutter
    if (CONFIG.render_x >= CONFIG.column_offset + CONFIG.text_columns) {
        CONFIG.column_offset = CONFIG.render_x - CONFIG.text_columns + 1;
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
            
            /* Perform cleanup before exit */
            /* Re-enable thread shutdown */
            highlightThreadShutdown();
            
            /* Clear screen before exit */
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            
            /* Reset terminal mode */
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
            CONFIG.cursor_x = 0;
            break;
        case END_KEY:
            if (CONFIG.cursor_y < CONFIG.numrows)
                CONFIG.cursor_x = CONFIG.row[CONFIG.cursor_y].size;
            break;

        case BACKSPACE:
        case CTRL_KEY('h'):
        case DELETE_KEY:
            if (c == DELETE_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    CONFIG.cursor_y = CONFIG.row_offset;
                }
                else if (c == PAGE_DOWN) {
                    CONFIG.cursor_y = CONFIG.row_offset + CONFIG.screen_rows - 1;
                    if (CONFIG.cursor_y > CONFIG.numrows) CONFIG.cursor_y = CONFIG.numrows;
                }

                int times = CONFIG.screen_rows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;

        case CTRL_KEY('l'):
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
    CONFIG.row = NULL;
    CONFIG.dirty = 0;
    CONFIG.filename = NULL;
    CONFIG.status_message[0] = '\0';
    CONFIG.status_message_time = 0;
    CONFIG.syntax = NULL;
    
    if (getWindowSize(&CONFIG.screen_rows, &CONFIG.screen_columns) == -1)
        die("getWindowSize");
    CONFIG.screen_rows -= 2; // status bar + message bar
    editorUpdateGutter();
    
    highlightThreadInit();
}