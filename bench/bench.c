// usage: bench <file> [reps]
//        bench --gen <lines> <file>
// measures: file open, first frame render, steady state redraw, scroll sweep

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "config.h"
#include "fileio.h"
#include "output.h"
#include "editor.h"
#include "highlight_thread.h"

EDITOR_CONFIG CONFIG;

#define BENCH_ROWS 50
#define BENCH_COLS 200

static double nowMs(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);

    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

// redirect stdout to /dev/null, render writes real bytes, we dont want them
static void muteStdout(void) {
    int fd = open("/dev/null", O_WRONLY);
    if (fd == -1) { 
        perror("open /dev/null"); exit(1);
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
}

static long fileBytes(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

static int genCorpus(const char *path, long lines) {
    static const char *pat[10] = {
        "/* block comment line %ld */",
        "#include <stdio.h>",
        "    int x_%ld = %ld; // trailing",
        "    const char *s = \"string %ld\";",
        "",
        "static unsigned long v%ld = 0x%lx;",
        "\tif (x_%ld > 3.14) { return %ld; }",
        "struct Foo%ld { size_t n; };",
        "void fn%ld(void) { for (int j=0;j<10;j++) ; }",
        "} // end %ld"
    };

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror(path);

        return 1;
    }

    for (long i = 0; i < lines; i++) {
        fprintf(f, pat[i % 10], i, i);
        fputc('\n', f);
    }

    return fclose(f) == 0 ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <file> [redraw_reps]\n", argv[0]);

        return 2;
    }
    if (strcmp(argv[1], "--gen") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: %s --gen <lines> <file>\n", argv[0]); return 2; }

        return genCorpus(argv[3], atol(argv[2]));
    }

    int reps = (argc >= 3) ? atoi(argv[2]) : 200;
    long bytes = fileBytes(argv[1]);

    CONFIG.screen_rows = BENCH_ROWS;
    CONFIG.screen_columns = BENCH_COLS;

    highlightThreadInit();

    double t0 = nowMs();
    editorOpen(argv[1]);
    double t1 = nowMs();

    muteStdout();

    editorRefreshScreen();
    double t2 = nowMs();

    // steady state: redraw same viewport; everything already materialized
    for (int i = 0; i < reps; i++) {
        editorRefreshScreen();
    }
    double t3 = nowMs();

    // scroll sweep: page down through whole file, this is the cold path, every frame touches rows never rendered before
    int pages = 0;
    CONFIG.cursor_y = 0;
    CONFIG.row_offset = 0;
    while (CONFIG.row_offset + CONFIG.screen_rows < CONFIG.numrows && pages < 2000) {
        CONFIG.cursor_y += CONFIG.screen_rows;
        if (CONFIG.cursor_y >= CONFIG.numrows) {
            CONFIG.cursor_y = CONFIG.numrows - 1;
        }
        editorRefreshScreen();
        pages++;
    }
    double t4 = nowMs();

    highlightThreadShutdown();

    fprintf(stderr,
        "file        %s\n"
        "bytes       %ld\n"
        "rows        %d\n"
        "syntax      %s\n"
        "---\n"
        "open        %8.2f ms   (%.1f MB/s)\n"
        "first frame %8.3f ms\n"
        "redraw      %8.3f ms   (avg of %d)\n"
        "scroll      %8.2f ms   (%d pages, %.3f ms/page)\n",
        argv[1], bytes, CONFIG.numrows,
        CONFIG.syntax ? CONFIG.syntax->filetype : "none",
        t1 - t0, bytes > 0 ? (bytes / 1048576.0) / ((t1 - t0) / 1000.0) : 0.0,
        t2 - t1,
        reps > 0 ? (t3 - t2) / reps : 0.0, reps,
        t4 - t3, pages, pages > 0 ? (t4 - t3) / pages : 0.0);

    return 0;
}
