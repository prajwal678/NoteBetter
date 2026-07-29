#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "output.h"
#include "config.h"
#include "buffer.h"
#include "highlight.h"
#include "editor.h"


static int digitsOf(int n) {
    int d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }

    return d;
}

void editorUpdateGutter(void) {
    int want = digitsOf(CONFIG.numrows > 0 ? CONFIG.numrows : 1) + 1;

    // never let line numbers eat the text area on a narrow terminal
    if (want > CONFIG.screen_columns / 2) {
        want = 0;
    }

    CONFIG.gutter = want;
    CONFIG.text_columns = CONFIG.screen_columns - want;
    if (CONFIG.text_columns < 1) {
        CONFIG.text_columns = 1;
    }
}

// draw the line number, or a tilde past the end of the file
static void drawGutter(APPEND_BUFFER *ab, int filerow) {
    if (CONFIG.gutter == 0) {
        return;
    }

    char buf[32];
    int n;
    appendBufferAppend(ab, "\x1b[90m", 5);
    if (filerow >= CONFIG.numrows) {
        n = snprintf(buf, sizeof(buf), "%*s ", CONFIG.gutter - 1, "~");
    }
    else {
        n = snprintf(buf, sizeof(buf), "%*d ", CONFIG.gutter - 1, filerow + 1);
    }
    appendBufferAppend(ab, buf, n);
    appendBufferAppend(ab, "\x1b[39m", 5);
}

void editorDrawRows(APPEND_BUFFER *ab) {
    for (int y = 0; y < CONFIG.screen_rows; y++) {
        int filerow = y + CONFIG.row_offset;
        drawGutter(ab, filerow);

        if (filerow >= CONFIG.numrows) {
            if (CONFIG.numrows == 0 && y == CONFIG.screen_rows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "NoteBetter editor -- version %s", NOTEBETTER_VERSION);
                if (welcomelen > CONFIG.text_columns) welcomelen = CONFIG.text_columns;
                int padding = (CONFIG.text_columns - welcomelen) / 2;
                if (padding) {
                    appendBufferAppend(ab, "-", 1);
                    padding--;
                }
                while (padding--) appendBufferAppend(ab, " ", 1);
                appendBufferAppend(ab, welcome, welcomelen);
            }
            else {
                appendBufferAppend(ab, "-", 1);
            }
        } 
        else {
            if (CONFIG.row[filerow].render == NULL) {
                appendBufferAppend(ab, "[NULL]", 6);
                appendBufferAppend(ab, "\x1b[K", 3);
                appendBufferAppend(ab, "\r\n", 2);
                continue;
            }
            
            int len = CONFIG.row[filerow].render_size - CONFIG.column_offset;
            if (len < 0) len = 0;
            if (len > CONFIG.text_columns) len = CONFIG.text_columns;
            
            char *c = &CONFIG.row[filerow].render[CONFIG.column_offset];
            
            unsigned char *hl = NULL;
            if (CONFIG.row[filerow].hl != NULL) {
                hl = &CONFIG.row[filerow].hl[CONFIG.column_offset];
            }
            
            int current_color = -1;
            for (int j = 0; j < len; j++) {
                if (hl == NULL || hl[j] == HL_NORMAL) {
                    if (current_color != -1) {
                        appendBufferAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                    appendBufferAppend(ab, &c[j], 1);
                }
                else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        appendBufferAppend(ab, buf, clen);
                    }
                    appendBufferAppend(ab, &c[j], 1);
                }
            }
            appendBufferAppend(ab, "\x1b[39m", 5);
        }

        appendBufferAppend(ab, "\x1b[K", 3);
        appendBufferAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(APPEND_BUFFER *ab) {
    appendBufferAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        CONFIG.filename ? CONFIG.filename : "[No Name]", CONFIG.numrows,
        CONFIG.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
        CONFIG.syntax ? CONFIG.syntax->filetype : "no ft",
        CONFIG.cursor_y + 1, CONFIG.numrows);
    if (len > CONFIG.screen_columns) len = CONFIG.screen_columns;
    appendBufferAppend(ab, status, len);
    while (len < CONFIG.screen_columns) {
        if (CONFIG.screen_columns - len == rlen) {
            appendBufferAppend(ab, rstatus, rlen);
            break;
        }
        else {
            appendBufferAppend(ab, " ", 1);
            len++;
        }
    }
    appendBufferAppend(ab, "\x1b[m", 3);
    appendBufferAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(APPEND_BUFFER *ab) {
    appendBufferAppend(ab, "\x1b[K", 3);
    int msglen = strlen(CONFIG.status_message);
    if (msglen > CONFIG.screen_columns) msglen = CONFIG.screen_columns;
    if (msglen && time(NULL) - CONFIG.status_message_time < 5)
        appendBufferAppend(ab, CONFIG.status_message, msglen);
}

void editorRefreshScreen(void) {
    editorUpdateGutter();
    editorScroll();

    APPEND_BUFFER ab = ABUF_INIT;

    appendBufferAppend(&ab, "\x1b[?25l", 6);
    appendBufferAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (CONFIG.cursor_y - CONFIG.row_offset) + 1,
             (CONFIG.render_x - CONFIG.column_offset) + 1 + CONFIG.gutter);
    appendBufferAppend(&ab, buf, strlen(buf));

    appendBufferAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.buffer, ab.length);
    appendBufferFree(&ab);
}

void setStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(CONFIG.status_message, sizeof(CONFIG.status_message), fmt, ap);
    va_end(ap);
    CONFIG.status_message_time = time(NULL);
}