// THE TEST FILE IS AI GENERATED, but edge cases are mine and code tweaked a little


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <stdarg.h>
#include "config.h"
#include "fileio.h"
#include "row.h"
#include "highlight.h"
#include "highlight_thread.h"
#include "output.h"
#include "editor.h"
#include "buffer.h"

#ifdef _OPENMP
#include <omp.h>
#endif

EDITOR_CONFIG CONFIG;

static int checks;
static int failures;

/* a real function instead of a do/while macro, only __LINE__ has to be
   captured at the call site, so the macro stays one line, and the format
   attribute gets us printf checking the macro version never had */
static void checkAt(int ok, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void checkAt(int ok, int line, const char *fmt, ...) {
    checks++;
    if (ok) {
        return;
    }

    failures++;
    fprintf(stderr, "FAIL %s:%d  ", __FILE__, line);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}

#define CHECK(cond, ...) checkAt((cond) != 0, __LINE__, __VA_ARGS__)

static char tmpdir[] = "/tmp/nbtestXXXXXX";

static char *tmpPath(const char *name) {
    static char path[512];
    snprintf(path, sizeof(path), "%s/%s", tmpdir, name);

    return path;
}

static char *writeTmp(const char *name, const char *data, size_t len) {
    char *path = tmpPath(name);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    assert(fd != -1);
    assert(write(fd, data, len) == (ssize_t)len);
    close(fd);

    return path;
}

static void resetEditor(void) {
    editorCloseFile();
    memset(&CONFIG, 0, sizeof(CONFIG));
    CONFIG.screen_rows = 24;
    CONFIG.screen_columns = 80;
    editorUpdateGutter();
}

// load round trip

static void checkRoundTrip(const char *name, const char *data, size_t len,
                           int want_rows, const char *want_out) {
    resetEditor();
    char *path = writeTmp(name, data, len);
    editorOpen(path);

    CHECK(CONFIG.numrows == want_rows, "%s: rows %d want %d", name, CONFIG.numrows, want_rows);

    int n = 0;
    char *out = editorRowsToString(&n);
    CHECK(out != NULL, "%s: rowsToString null", name);
    if (out) {
        CHECK(n == (int)strlen(want_out) && memcmp(out, want_out, (size_t)n) == 0,
              "%s: got %d bytes '%.*s' want '%s'", name, n, n, out, want_out);
        free(out);
    }
}

static void testLoad(void) {
    checkRoundTrip("plain.c",   "a\nb\n", 4, 2, "a\nb\n");
    checkRoundTrip("noeol.c",   "a\nb",   3, 2, "a\nb\n");
    checkRoundTrip("crlf.c",    "a\r\nb\r\n", 6, 2, "a\nb\n");
    checkRoundTrip("blank.c",   "\n",     1, 1, "\n");
    checkRoundTrip("empty.c",   "",       0, 0, "");
    checkRoundTrip("noeol1.c",  "abc",    3, 1, "abc\n");
    checkRoundTrip("blanks.c",  "\n\n\n", 3, 3, "\n\n\n");

    // one very long line, and a tab that has to expand in render
    char big[9000];
    memset(big, 'x', sizeof(big));
    big[0] = '\t';
    big[sizeof(big) - 1] = '\n';
    resetEditor();
    char *path = writeTmp("long.c", big, sizeof(big));
    editorOpen(path);
    CHECK(CONFIG.numrows == 1, "long: rows %d want 1", CONFIG.numrows);
    editorRowEnsureRender(&CONFIG.row[0]);
    CHECK(CONFIG.row[0].render_size == 8999 + (TAB_SPACE - 1),
          "long: render_size %d", CONFIG.row[0].render_size);
}


static void testCopyOnWrite(void) {
    resetEditor();
    char *path = writeTmp("cow.c", "hello\nworld\n", 12);
    editorOpen(path);

    CHECK(CONFIG.map != NULL, "cow: expected an mmap");
    CHECK(CONFIG.row[0].owned == 0, "cow: row 0 should be borrowed at load");
    CHECK(CONFIG.row[0].string >= CONFIG.map &&
          CONFIG.row[0].string < CONFIG.map + CONFIG.map_len,
          "cow: row 0 should point into the mapping");

    rowInsertChar(&CONFIG.row[0], 0, 'X');
    CHECK(CONFIG.row[0].owned == 1, "cow: row 0 should be owned after edit");
    CHECK(CONFIG.row[0].size == 6 && memcmp(CONFIG.row[0].string, "Xhello", 6) == 0,
          "cow: got '%.*s'", CONFIG.row[0].size, CONFIG.row[0].string);
    CHECK(CONFIG.row[1].owned == 0, "cow: row 1 must stay borrowed");

    // untouched row still readable through the mapping
    CHECK(memcmp(CONFIG.row[1].string, "world", 5) == 0, "cow: row 1 corrupted");
}


static unsigned char hlAt(int row, int col) {
    ROW_DATA *r = &CONFIG.row[row];
    editorRowEnsureRender(r);
    editorHighlightRow(r);
    if (r->hl == NULL || col >= r->render_size) {
        return HL_NORMAL;
    }

    return r->hl[col];
}

static void testHighlightBasics(void) {
    const char *src =
        "int x = 42;\n"  // 0 keyword2, number
        "return \"hi\";\n"  // 1 keyword1, string
        "// note\n"  // 2 line comment
        "#include <stdio.h>\n";  // 3 preprocessor
    resetEditor();
    char *path = writeTmp("hl.c", src, strlen(src));
    editorOpen(path);

    CHECK(CONFIG.syntax != NULL && strcmp(CONFIG.syntax->filetype, "c") == 0,
          "hl: syntax not selected");

    CHECK(hlAt(0, 0) == HL_KEYWORD2, "hl: 'int' should be keyword2, got %d", hlAt(0, 0));
    CHECK(hlAt(0, 2) == HL_KEYWORD2, "hl: 'int' end");
    CHECK(hlAt(0, 4) == HL_NORMAL,   "hl: 'x' should be normal");
    CHECK(hlAt(0, 8) == HL_NUMBER,   "hl: '42' should be number, got %d", hlAt(0, 8));
    CHECK(hlAt(0, 9) == HL_NUMBER,   "hl: '42' second digit");

    CHECK(hlAt(1, 0) == HL_KEYWORD1, "hl: 'return' should be keyword1, got %d", hlAt(1, 0));
    CHECK(hlAt(1, 7) == HL_STRING,   "hl: opening quote, got %d", hlAt(1, 7));
    CHECK(hlAt(1, 10) == HL_STRING,  "hl: closing quote, got %d", hlAt(1, 10));
    CHECK(hlAt(1, 11) == HL_NORMAL,  "hl: semicolon after string, got %d", hlAt(1, 11));

    CHECK(hlAt(2, 0) == HL_COMMENT,  "hl: '//' comment, got %d", hlAt(2, 0));
    CHECK(hlAt(2, 6) == HL_COMMENT,  "hl: comment runs to eol");

    CHECK(hlAt(3, 0) == HL_KEYWORD1, "hl: '#include', got %d", hlAt(3, 0));

    // python picks a different table, and '#' is a comment there not a directive
    resetEditor();
    const char *py = "def f():\n    return 1  # hi\n";
    path = writeTmp("hl.py", py, strlen(py));
    editorOpen(path);
    CHECK(CONFIG.syntax != NULL && strcmp(CONFIG.syntax->filetype, "python") == 0,
          "hl: python syntax not selected");
    CHECK(hlAt(0, 0) == HL_KEYWORD1, "hl: python 'def', got %d", hlAt(0, 0));
    CHECK(hlAt(1, 15) == HL_COMMENT, "hl: python '#' comment, got %d", hlAt(1, 15));
}

/* a block comment that spans thousands of rows, the whole point of the
   cheap sequential state pass: row 4999 must colour right without rows
   2..4998 ever being materialized */
static void testCommentChain(void) {
    resetEditor();

    size_t cap = 64 * 1024;
    char *src = malloc(cap);
    assert(src);
    int len = snprintf(src, cap, "/* open\n");
    for (int i = 0; i < 4997; i++) {
        len += snprintf(src + len, cap - (size_t)len, "line %d\n", i);
        assert((size_t)len < cap);
    }
    len += snprintf(src + len, cap - (size_t)len, "*/ done\n");
    len += snprintf(src + len, cap - (size_t)len, "int after;\n");
    assert((size_t)len < cap);

    char *path = writeTmp("chain.c", src, (size_t)len);
    free(src);
    editorOpen(path);

    CHECK(CONFIG.numrows == 5000, "chain: rows %d want 5000", CONFIG.numrows);

    CHECK(CONFIG.row[0].hl_open_comment == 1, "chain: row 0 must open a comment");
    CHECK(CONFIG.row[2500].hl_open_comment == 1, "chain: row 2500 still inside");
    CHECK(CONFIG.row[4997].hl_open_comment == 1, "chain: row 4997 still inside");
    CHECK(CONFIG.row[4998].hl_open_comment == 0, "chain: row 4998 closes it");
    CHECK(CONFIG.row[4999].hl_open_comment == 0, "chain: row 4999 outside");

    // nothing in the middle was materialized by the state pass
    CHECK(CONFIG.row[2500].render == NULL, "chain: row 2500 should still be lazy");
    CHECK(CONFIG.row[2500].hl == NULL, "chain: row 2500 hl should still be lazy");

    // colour a deep row cold, with no neighbours built
    CHECK(hlAt(4997, 0) == HL_MULTI_LINE_COMMENT,
          "chain: row 4997 should be inside comment, got %d", hlAt(4997, 0));
    CHECK(hlAt(4998, 0) == HL_MULTI_LINE_COMMENT,
          "chain: row 4998 '*/' should close, got %d", hlAt(4998, 0));
    CHECK(hlAt(4999, 0) == HL_KEYWORD2,
          "chain: row 4999 'int' should be keyword2, got %d", hlAt(4999, 0));

    // now break the chain: delete the row that opens the block comment
    // everything below
    // must flip to normal code
    editorDelRow(0);
    CHECK(CONFIG.row[0].hl_open_comment == 0, "chain: after delete, no open comment");
    CHECK(CONFIG.row[2000].hl_open_comment == 0, "chain: after delete, row 2000 clean");
}

/* the fast state only scan and the full colouring walker are two different
   loops, if they ever disagree about where a block comment ends, colours go
   wrong far away from the edit, diff them on every row, both start states */
static void testStateScanMatchesFull(void) {
    static const char *tricky[] = {
        "int x = 1;",
        "/* open",
        "*/ closed",
        "/* both */ code",
        "a /* b */ c /* d",
        "\"a string with /* inside\"",
        "'c' /* after char lit */",
        "// line comment with /* inside",
        "\"unterminated string /*",
        "s = \"escaped \\\" quote /* here\";",
        "char c = '\\'';",
        "*/",
        "/",
        "*",
        "/*/",
        "/**/",
        "///*",
        "#include \"a/*b\"",
        "",
        "   ",
        "\t/* tabbed",
        "x /* y",
        "'''",
        "a'''b",
        NULL
    };

    const char *exts[] = { "sd.c", "sd.cpp", "sd.py", NULL };

    for (int e = 0; exts[e]; e++) {
        resetEditor();
        char *path = writeTmp(exts[e], "x\n", 2);
        editorOpen(path);
        CHECK(CONFIG.syntax != NULL, "diff: no syntax for %s", exts[e]);
        editorSyntaxPrepare();

        for (int i = 0; tricky[i]; i++) {
            int len = (int)strlen(tricky[i]);
            for (int start = 0; start <= 1; start++) {
                int fast = editorRowEndState(tricky[i], len, start);
                int slow = editorRowEndStateSlow(tricky[i], len, start);
                CHECK(fast == slow, "diff[%s] start=%d '%s': fast=%d slow=%d",
                      exts[e], start, tricky[i], fast, slow);
            }
        }
    }
}


/* the whole file comment chain is computed by scanning chunks from both
   possible start states at once and stitching, so it can use every core;
   that stitching is the part that can silently go wrong, and tsan cannot
   check it because libomp is not instrumented, so compare the parallel
   result against the plain sequential walk instead */
static void testParallelChainMatchesSerial(void) {
    resetEditor();

    // needs >= 8192 rows to take the parallel path at all, and block comments
    // that straddle chunk boundaries so the stitching actually matters
    size_t cap = 1u << 20;
    char *src = malloc(cap);
    assert(src);

    int len = 0;
    for (int i = 0; i < 10000; i++) {
        const char *line;
        if (i == 500 || i == 6000 || i == 9500) {
            line = "/* open block";
        }
        else if (i == 2500 || i == 6001) {
            line = "*/ close block";
        }
        else {
            line = "int v_%d = %d;";
        }
        len += snprintf(src + len, cap - (size_t)len, line, i, i);
        len += snprintf(src + len, cap - (size_t)len, "\n");
        assert((size_t)len < cap);
    }

    char *path = writeTmp("pchain.c", src, (size_t)len);
    free(src);
    editorOpen(path);

    CHECK(CONFIG.numrows == 10000, "pchain: rows %d want 10000", CONFIG.numrows);

#ifdef _OPENMP
    // only meaningful when openmp is compiled in; the tsan build has it off
    int threads = omp_get_max_threads();
    CHECK(threads > 1, "pchain: only %d thread, parallel path never ran", threads);
#endif

    unsigned char *parallel = malloc((size_t)CONFIG.numrows);
    assert(parallel);
    for (int i = 0; i < CONFIG.numrows; i++) {
        parallel[i] = (unsigned char)CONFIG.row[i].hl_open_comment;
    }

    // one thread makes editorScanCommentChainAll fall through to the plain
    // sequential walk, which is the reference answer
#ifdef _OPENMP
    omp_set_num_threads(1);
#endif
    editorScanCommentChainAll();

    int mismatch = -1;
    for (int i = 0; i < CONFIG.numrows; i++) {
        if (parallel[i] != (unsigned char)CONFIG.row[i].hl_open_comment) {
            mismatch = i;
            break;
        }
    }
    CHECK(mismatch == -1, "pchain: row %d differs, parallel=%d serial=%d", mismatch,
          mismatch >= 0 ? parallel[mismatch] : 0,
          mismatch >= 0 ? CONFIG.row[mismatch].hl_open_comment : 0);

    // and spot check the states really are what they should be
    CHECK(CONFIG.row[499].hl_open_comment == 0, "pchain: row 499 outside");
    CHECK(CONFIG.row[500].hl_open_comment == 1, "pchain: row 500 opens");
    CHECK(CONFIG.row[1500].hl_open_comment == 1, "pchain: row 1500 inside");
    CHECK(CONFIG.row[2500].hl_open_comment == 0, "pchain: row 2500 closes");
    CHECK(CONFIG.row[6000].hl_open_comment == 1, "pchain: row 6000 opens");
    CHECK(CONFIG.row[6001].hl_open_comment == 0, "pchain: row 6001 closes");
    CHECK(CONFIG.row[9999].hl_open_comment == 1, "pchain: row 9999 still open");

#ifdef _OPENMP
    omp_set_num_threads(threads);
#endif
    free(parallel);
}


static void testGutter(void) {
    resetEditor();
    CONFIG.screen_columns = 80;

    CONFIG.numrows = 9;    editorUpdateGutter();
    CHECK(CONFIG.gutter == 2 && CONFIG.text_columns == 78, "gutter 9: %d/%d", CONFIG.gutter, CONFIG.text_columns);

    CONFIG.numrows = 10;   editorUpdateGutter();
    CHECK(CONFIG.gutter == 3 && CONFIG.text_columns == 77, "gutter 10: %d", CONFIG.gutter);

    CONFIG.numrows = 999;  editorUpdateGutter();
    CHECK(CONFIG.gutter == 4, "gutter 999: %d", CONFIG.gutter);

    CONFIG.numrows = 1000; editorUpdateGutter();
    CHECK(CONFIG.gutter == 5, "gutter 1000: %d", CONFIG.gutter);

    CONFIG.numrows = 1000000; editorUpdateGutter();
    CHECK(CONFIG.gutter == 8, "gutter 1e6: %d", CONFIG.gutter);

    // narrow terminal: gutter must give up rather than squeeze out the text
    CONFIG.screen_columns = 10; CONFIG.numrows = 1000000; editorUpdateGutter();
    CHECK(CONFIG.gutter == 0 && CONFIG.text_columns == 10, "gutter narrow: %d/%d",
          CONFIG.gutter, CONFIG.text_columns);

    CONFIG.numrows = 0;
}


static void testSearchHighlight(void) {
    resetEditor();
    const char *src = "int alpha;\nint beta;\n";
    char *path = writeTmp("find.c", src, strlen(src));
    editorOpen(path);

    ROW_DATA *r = &CONFIG.row[1];
    editorRowEnsureRender(r);
    editorHighlightRow(r);

    unsigned char before[64];
    memcpy(before, r->hl, (size_t)r->render_size);

    editorFindCallback("beta", 'b');
    CHECK(CONFIG.cursor_y == 1, "find: cursor_y %d want 1", CONFIG.cursor_y);
    CHECK(CONFIG.cursor_x == 4, "find: cursor_x %d want 4", CONFIG.cursor_x);
    CHECK(CONFIG.row[1].hl[4] == HL_MATCH, "find: match not painted, got %d", CONFIG.row[1].hl[4]);
    CHECK(CONFIG.row[1].hl[7] == HL_MATCH, "find: match end not painted");
    CHECK(CONFIG.row[1].hl[8] != HL_MATCH, "find: painted past the match");

    editorFindCallback("beta", '\x1b');
    CHECK(memcmp(CONFIG.row[1].hl, before, (size_t)r->render_size) == 0,
          "find: colours not restored after escape");
}


static void testBuffer(void) {
    APPEND_BUFFER ab = ABUF_INIT;
    for (int i = 0; i < 10000; i++) {
        appendBufferAppend(&ab, "x", 1);
    }
    CHECK(ab.length == 10000, "buffer: length %d", ab.length);
    CHECK(ab.capacity >= 10000, "buffer: capacity %d", ab.capacity);
    for (int i = 0; i < 10000; i++) {
        if (ab.buffer[i] != 'x') { CHECK(0, "buffer: corrupt at %d", i); break; }
    }
    appendBufferFree(&ab);
    CHECK(ab.buffer == NULL && ab.length == 0, "buffer: not reset after free");
}


static void testRenderAndThread(void) {
    resetEditor();

    size_t cap = 1u << 20;
    char *src = malloc(cap);
    assert(src);
    int len = 0;
    for (int i = 0; i < 20000; i++) {
        len += snprintf(src + len, cap - (size_t)len,
                        "static int v%d = %d; /* c */ // t\n", i, i);
        assert((size_t)len < cap);
    }

    char *path = writeTmp("render.c", src, (size_t)len);
    free(src);
    editorOpen(path);

    int devnull = open("/dev/null", O_WRONLY);
    int save = dup(STDOUT_FILENO);
    dup2(devnull, STDOUT_FILENO);

    highlightThreadInit();

    // scroll around while the prefetcher chases the viewport, and edit in
    // between, this is the interleaving tsan needs to see
    for (int pass = 0; pass < 40; pass++) {
        CONFIG.cursor_y = (pass * 457) % CONFIG.numrows;
        editorRefreshScreen();  // resumes the worker
        highlightThreadPause();  // what editorReadKey does
        rowInsertChar(&CONFIG.row[CONFIG.cursor_y], 0, 'z');
        editorDelRow((CONFIG.cursor_y + 1) % CONFIG.numrows);
        editorInsertRow(CONFIG.cursor_y, "int inserted;", 13);
    }
    editorRefreshScreen();
    highlightThreadPause();
    highlightThreadShutdown();

    dup2(save, STDOUT_FILENO);
    close(save);
    close(devnull);

    CHECK(CONFIG.numrows > 0, "render: rows vanished");
    CHECK(CONFIG.row[0].index == 0, "render: index chain broken");
    for (int i = 1; i < CONFIG.numrows; i += 997) {
        CHECK(CONFIG.row[i].index == i, "render: index %d wrong (%d)", i, CONFIG.row[i].index);
    }
}


static void testSave(void) {
    resetEditor();
    char *path = writeTmp("save.c", "one\ntwo\n", 8);
    editorOpen(path);

    rowInsertChar(&CONFIG.row[0], 0, 'X');
    editorSave();
    CHECK(CONFIG.dirty == 0, "save: still dirty");

    // borrowed row 1 must still be readable: rename kept the old inode alive
    CHECK(memcmp(CONFIG.row[1].string, "two", 3) == 0, "save: mapping went bad");

    char buf[64];
    int fd = open(path, O_RDONLY);
    CHECK(fd != -1, "save: cannot reopen");
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    CHECK(n == 9 && memcmp(buf, "Xone\ntwo\n", 9) == 0, "save: got '%.*s'", (int)n, buf);

    // no temp file left behind
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.nbtmp", path);
    CHECK(access(tmp, F_OK) != 0, "save: temp file left behind");
}

int main(void) {
    if (mkdtemp(tmpdir) == NULL) { perror("mkdtemp"); return 1; }

    testLoad();
    testCopyOnWrite();
    testHighlightBasics();
    testCommentChain();
    testStateScanMatchesFull();
    testParallelChainMatchesSerial();
    testGutter();
    testSearchHighlight();
    testBuffer();
    testRenderAndThread();
    testSave();

    resetEditor();
    editorFreeOutput();

    printf("%d checks, %d failures\n", checks, failures);

    return failures ? 1 : 0;
}
