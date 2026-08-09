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

#ifdef _OPENMP
#include <omp.h>
#endif


typedef struct {
    char *buf;
    int len;
    int cap;
} ROW_SLAB;

static ROW_SLAB *slabs;
static int slab_count;

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

static int slabReserve(ROW_SLAB *s, int need) {
    if (need <= s->cap) {
        return 1;
    }

    int cap = s->cap ? s->cap : 256;
    while (cap < need) {
        cap *= 2;
    }

    char *tmp = realloc(s->buf, (size_t)cap);
    if (tmp == NULL) {
        return 0;
    }

    s->buf = tmp;
    s->cap = cap;

    return 1;
}

static void slabPut(ROW_SLAB *s, const char *p, int n) {
    if (n <= 0 || !slabReserve(s, s->len + n)) {
        return;
    }
    memcpy(&s->buf[s->len], p, (size_t)n);
    s->len += n;
}

static void ensureSlabs(int n) {
    if (n <= slab_count) {
        return;
    }

    ROW_SLAB *tmp = realloc(slabs, sizeof(ROW_SLAB) * (size_t)n);
    if (tmp == NULL) {
        return;
    }

    memset(&tmp[slab_count], 0, sizeof(ROW_SLAB) * (size_t)(n - slab_count));
    slabs = tmp;
    slab_count = n;
}

static void freeSlabs(void) {
    for (int i = 0; i < slab_count; i++) {
        free(slabs[i].buf);
    }
    free(slabs);
    slabs = NULL;
    slab_count = 0;
}

static void buildRowSlab(ROW_SLAB *s, int filerow) {
    char tmp[64];
    s->len = 0;

    if (filerow >= CONFIG.numrows) {
        if (CONFIG.gutter) {
            slabPut(s, "\x1b[90m", 5);
            int n = snprintf(tmp, sizeof(tmp), "%*s ", CONFIG.gutter - 1, "~");
            slabPut(s, tmp, n);
            slabPut(s, "\x1b[39m", 5);
        }
        slabPut(s, "-", 1);
        slabPut(s, "\x1b[K\r\n", 5);

        return;
    }

    ROW_DATA *row = &CONFIG.row[filerow];

    if (CONFIG.gutter) {
        slabPut(s, "\x1b[90m", 5);
        int n = snprintf(tmp, sizeof(tmp), "%*d ", CONFIG.gutter - 1, filerow + 1);
        slabPut(s, tmp, n);
        slabPut(s, "\x1b[39m", 5);
    }

    int len = row->render_size - CONFIG.column_offset;
    if (len < 0) {
        len = 0;
    }
    if (len > CONFIG.text_columns) {
        len = CONFIG.text_columns;
    }

    if (len > 0 && row->render != NULL) {
        slabReserve(s, s->len + len * 6 + 8);

        char *c = &row->render[CONFIG.column_offset];
        unsigned char *hl = (row->hl && row->hl_valid) ? &row->hl[CONFIG.column_offset] : NULL;

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
                    slabPut(s, "\x1b[39m", 5);
                    current_color = -1;
                }
            }
            else {
                int color = editorSyntaxToColor(h);
                if (color != current_color) {
                    current_color = color;
                    int n = snprintf(tmp, sizeof(tmp), "\x1b[%dm", color);
                    slabPut(s, tmp, n);
                }
            }

            slabPut(s, &c[j], k - j);
            j = k;
        }
        slabPut(s, "\x1b[39m", 5);
    }

    slabPut(s, "\x1b[K\r\n", 5);
}

static void editorDrawRows(APPEND_BUFFER *ab) {
    int rows = CONFIG.screen_rows;
    ensureSlabs(rows);
    if (slab_count < rows) {
        return;
    }

    editorSyntaxPrepare();

#ifdef _OPENMP
    int work = 0;
    for (int y = 0; y < rows; y++) {
        int filerow = y + CONFIG.row_offset;
        if (filerow >= CONFIG.numrows) {
            break;
        }
        ROW_DATA *row = &CONFIG.row[filerow];
        if (row->render == NULL || !row->hl_valid) {
            work += row->size;
        }
    }
#endif

    // the colour + render fan out, safe because main is blocked inside this
    // region and the prefetch thread is parked, so nothing mutates rows
    // each iteration owns exactly one row, iterations never overlap
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) if (work >= 32768 && rows >= 32)
#endif
    for (int y = 0; y < rows; y++) {
        int filerow = y + CONFIG.row_offset;
        if (filerow < CONFIG.numrows) {
            ROW_DATA *row = &CONFIG.row[filerow];
            editorRowEnsureRender(row);
            editorHighlightRow(row);
        }
        buildRowSlab(&slabs[y], filerow);
    }

    if (CONFIG.numrows == 0 && rows > 0) {
        int y = rows / 3;
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
            "NoteBetter editor -- version %s", NOTEBETTER_VERSION);
        if (welcomelen > CONFIG.text_columns) {
            welcomelen = CONFIG.text_columns;
        }

        ROW_SLAB *s = &slabs[y];
        s->len = 0;
        int padding = (CONFIG.text_columns - welcomelen) / 2;
        if (padding) { slabPut(s, "-", 1); padding--; }
        while (padding--) {
            slabPut(s, " ", 1);
        }
        slabPut(s, welcome, welcomelen);
        slabPut(s, "\x1b[K\r\n", 5);
    }

    int total = 0;
    for (int y = 0; y < rows; y++) {
        total += slabs[y].len;
    }
    appendBufferReserve(ab, total);
    for (int y = 0; y < rows; y++) {
        appendBufferAppend(ab, slabs[y].buf, slabs[y].len);
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

    if (len > CONFIG.screen_columns) {
        len = CONFIG.screen_columns;
    }
    appendBufferAppend(ab, status, len);

    while (len < CONFIG.screen_columns) {
        if (CONFIG.screen_columns - len == rlen) {
            appendBufferAppend(ab, rstatus, rlen);
            break;
        }
        appendBufferAppend(ab, " ", 1);
        len++;
    }

    appendBufferAppend(ab, "\x1b[m\r\n", 5);
}

static void editorDrawMessageBar(APPEND_BUFFER *ab) {
    appendBufferAppend(ab, "\x1b[K", 3);

    int msglen = (int)strlen(CONFIG.status_message);
    if (msglen > CONFIG.screen_columns) {
        msglen = CONFIG.screen_columns;
    }
    if (msglen && time(NULL) - CONFIG.status_message_time < 5) {
        appendBufferAppend(ab, CONFIG.status_message, msglen);
    }
}

void editorRefreshScreen(void) {
    // buffer survives between frames so steady state redraw allocates nothing
    static APPEND_BUFFER ab = ABUF_INIT;

    // park the worker before we touch a single row, editorReadKey also pauses,
    // but relying on the caller to have done it is a trap: any code path that
    // draws twice without a keypress in between would race the prefetcher on
    // viewport rows, tsan caught exactly that, pausing here makes it local
    // costs nothing when already parked
    highlightThreadPause();

    editorUpdateGutter();
    editorScroll();

    ab.length = 0;
    appendBufferAppend(&ab, "\x1b[?25l\x1b[H", 9);

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
             (CONFIG.cursor_y - CONFIG.row_offset) + 1,
             (CONFIG.render_x - CONFIG.column_offset) + 1 + CONFIG.gutter);
    appendBufferAppend(&ab, buf, n);
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

void editorFreeOutput(void) {
    freeSlabs();
}

void setStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(CONFIG.status_message, sizeof(CONFIG.status_message), fmt, ap);
    va_end(ap);
    CONFIG.status_message_time = time(NULL);
}
