#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "fileio.h"
#include "config.h"
#include "row.h"
#include "highlight.h"
#include "output.h"
#include "terminal.h"
#include "input.h"


void editorCloseFile(void) {
    for (int i = 0; i < CONFIG.numrows; i++) {
        editorFreeRow(&CONFIG.row[i]);
    }
    free(CONFIG.row);
    CONFIG.row = NULL;
    CONFIG.numrows = 0;
    CONFIG.rowcap = 0;

    if (CONFIG.map != NULL) {
        munmap(CONFIG.map, CONFIG.map_len);
        CONFIG.map = NULL;
        CONFIG.map_len = 0;
    }

    free(CONFIG.filename);
    CONFIG.filename = NULL;
}

/*
 * rows point straight into the mapping, no per line malloc
 * a row only gets its own buffer when it is first edited (see rowMakeOwned)
 * mapping stays alive for the whole session, editorCloseFile unmaps it
 */
static int loadMapped(const char *base, size_t len) {
    if (len == 0) {
        return 1;
    }

    // one pass to count lets us allocate the row array exactly once
    int lines = 0;
    const char *p = base;
    const char *end = base + len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        if (nl == NULL) {
            break;
        }
        lines++;
        p = nl + 1;
    }
    if (p < end) {
        lines++;
    }

    if (!editorReserveRows(lines)) {
        return 0;
    }

    int at = 0;
    p = base;
    while (p < end && at < lines) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *stop = nl ? nl : end;

        int n = (int)(stop - p);
        if (n > 0 && p[n - 1] == '\r') {
            n--;  // crlf
        }

        ROW_DATA *row = &CONFIG.row[at];
        row->index = at;
        row->size = n;
        row->string = (char *)(size_t)p;  // borrowed, owned stays 0
        row->owned = 0;
        row->render = NULL;
        row->render_size = 0;
        row->hl = NULL;
        row->hl_valid = 0;
        row->hl_open_comment = 0;

        at++;
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }

    CONFIG.numrows = at;

    return 1;
}

void editorOpen(char *filename) {
    if (filename == NULL) {
        setStatusMessage("No filename provided");

        return;
    }

    free(CONFIG.filename);
    CONFIG.filename = strdup(filename);
    if (CONFIG.filename == NULL) {
        setStatusMessage("Out of memory");

        return;
    }

    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        // pick a syntax so typing is coloured from teh start
        setStatusMessage("New file: %s (%s)", filename, strerror(errno));

        return;
    }

    struct stat st;
    if (fstat(fd, &st) == -1 || !S_ISREG(st.st_mode)) {
        close(fd);
        setStatusMessage("Not a regular file: %s", filename);

        return;
    }

    if (st.st_size > 0) {
        void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            setStatusMessage("Error mapping file: %s", strerror(errno));

            return;
        }
        CONFIG.map = map;
        CONFIG.map_len = (size_t)st.st_size;

        // we read front to back once, then randomly as the user scrolls
        madvise(map, CONFIG.map_len, MADV_WILLNEED);

        if (!loadMapped(CONFIG.map, CONFIG.map_len)) {
            setStatusMessage("Out of memory loading %s", filename);
        }
    }

    close(fd);  // mapping keeps its own reference
    CONFIG.dirty = 0;

    editorSelectSyntaxHighlight();
}

// caller frees; *buflen gets the byte count
char *editorRowsToString(int *buflen) {
    size_t totlen = 0;
    for (int i = 0; i < CONFIG.numrows; i++) {
        totlen += (size_t)CONFIG.row[i].size + 1;
    }

    *buflen = (int)totlen;

    char *buf = malloc(totlen ? totlen : 1);
    if (buf == NULL) {
        *buflen = 0;

        return NULL;
    }

    char *p = buf;
    for (int i = 0; i < CONFIG.numrows; i++) {
        memcpy(p, CONFIG.row[i].string, (size_t)CONFIG.row[i].size);
        p += CONFIG.row[i].size;
        *p++ = '\n';
    }

    return buf;
}

/*
 * write to a sibling temp file then rename for 2 rsns
 * crash mid write cannot leave a truncated original, and we must never
 * ftruncate the file we still have mapped (that is a SIGBUS waiting to happen)
 */
static int writeAtomic(const char *path, const char *buf, int len) {
    size_t tmplen = strlen(path) + 8;
    char *tmp = malloc(tmplen);
    if (tmp == NULL) {
        return -1;
    }
    snprintf(tmp, tmplen, "%s.nbtmp", path);

    mode_t mode = 0644;
    struct stat st;
    if (stat(path, &st) == 0) {
        mode = st.st_mode & 07777;
    }

    int rc = -1;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd == -1) {
        goto done;
    }

    int off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, (size_t)(len - off));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            goto close_fail;
        }
        off += (int)n;
    }

    if (fsync(fd) == -1) {
        goto close_fail;
    }
    if (close(fd) == -1) {
        fd = -1;
        goto close_fail;
    }
    fd = -1;

    if (rename(tmp, path) == -1) {
        goto close_fail;
    }
    rc = 0;
    goto done;

close_fail:
    if (fd != -1) {
        close(fd);
    }
    unlink(tmp);
done:
    free(tmp);

    return rc;
}

void editorSave(void) {
    if (CONFIG.filename == NULL) {
        CONFIG.filename = editorPrompt("Save as: %s", NULL);
        if (CONFIG.filename == NULL) {
            setStatusMessage("Save aborted");

            return;
        }
    }

    int len;
    char *buf = editorRowsToString(&len);
    if (buf == NULL) {
        setStatusMessage("Can't save! Out of memory");

        return;
    }

    if (writeAtomic(CONFIG.filename, buf, len) == 0) {
        CONFIG.dirty = 0;
        setStatusMessage("%d bytes written to disk", len);
    }
    else {
        setStatusMessage("Can't save! I/O error: %s", strerror(errno));
    }

    free(buf);
}
