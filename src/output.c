#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include "output.h"
#include "config.h"
#include "buffer.h"
#include "highlight.h"
#include "highlight_thread.h"
#include "row.h"
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

static void editorDrawRows(APPEND_BUFFER *ab) {
    editorSyntaxPrepare();

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
            // render and colours are built the first time a row is drawn
            ROW_DATA *row = &CONFIG.row[filerow];
            editorRowEnsureRender(row);
            editorHighlightRow(row);

            if (row->render == NULL) {
                appendBufferAppend(ab, "\x1b[K\r\n", 5);
                continue;
            }

            int len = CONFIG.row[filerow].render_size - CONFIG.column_offset;
            if (len < 0) len = 0;
            if (len > CONFIG.text_columns) len = CONFIG.text_columns;
            
            char *c = &CONFIG.row[filerow].render[CONFIG.column_offset];
            
            unsigned char *hl = NULL;
            if (row->hl != NULL && row->hl_valid) {
                hl = &row->hl[CONFIG.column_offset];
            }
            
            // worst case every char is its own run, 5 byte escape plus the char
            appendBufferReserve(ab, len * 6 + 8);

            int current_color = -1;
            int j = 0;
            while (j < len) {
                unsigned char h = hl ? hl[j] : HL_NORMAL;

                // one run of identical colour, one append, beats per char by ~10x
                int k = j + 1;
                while (k < len && (hl ? hl[k] : HL_NORMAL) == h) {
                    k++;
                }

                if (h == HL_NORMAL) {
                    if (current_color != -1) {
                        appendBufferAppend(ab, "\x1b[39m", 5);
                        current_color = -1;
                    }
                }
                else {
                    int color = editorSyntaxToColor(h);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
                        appendBufferAppend(ab, buf, clen);
                    }
                }

                appendBufferAppend(ab, &c[j], k - j);
                j = k;
            }
            appendBufferAppend(ab, "\x1b[39m", 5);
        }

        appendBufferAppend(ab, "\x1b[K", 3);
        appendBufferAppend(ab, "\r\n", 2);
    }
}

static void editorDrawStatusBar(APPEND_BUFFER *ab) {
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

static void editorDrawMessageBar(APPEND_BUFFER *ab) {
    appendBufferAppend(ab, "\x1b[K", 3);
    int msglen = strlen(CONFIG.status_message);
    if (msglen > CONFIG.screen_columns) msglen = CONFIG.screen_columns;
    if (msglen && time(NULL) - CONFIG.status_message_time < 5)
        appendBufferAppend(ab, CONFIG.status_message, msglen);
}

void editorRefreshScreen(void) {
    editorUpdateGutter();
    editorScroll();

    /*
     * park the worker before we touch a single row; editorReadKey also pauses,
     * but relying on the caller to have done it is a trap: any code path that
     * draws twice without a keypress in between would race the prefetcher on
     * viewport rows, so pausing here makes it local;
     * costs nothing when already parked
     */
    highlightThreadPause();

    // buffer survives between frames so a steady state redraw allocates nothing
    static APPEND_BUFFER ab = ABUF_INIT;
    ab.length = 0;

    appendBufferAppend(&ab, "\x1b[?25l\x1b[H", 9);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (CONFIG.cursor_y - CONFIG.row_offset) + 1,
             (CONFIG.render_x - CONFIG.column_offset) + 1 + CONFIG.gutter);
    appendBufferAppend(&ab, buf, strlen(buf));

    appendBufferAppend(&ab, "\x1b[?25h", 6);

    ssize_t ignored = write(STDOUT_FILENO, ab.buffer, (size_t)ab.length);
    (void)ignored;

    // hand the idle window to the prefetcher, it colours rows around the
    // viewport until the next keypress parks it
    int lo = CONFIG.row_offset - PREFETCH_ROWS;
    int hi = CONFIG.row_offset + CONFIG.screen_rows + PREFETCH_ROWS;
    if (lo < 0) {
        lo = 0;
    }
    if (hi >= CONFIG.numrows) {
        hi = CONFIG.numrows - 1;
    }
    highlightThreadResume(lo, hi);
}

void setStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(CONFIG.status_message, sizeof(CONFIG.status_message), fmt, ap);
    va_end(ap);
    CONFIG.status_message_time = time(NULL);
}