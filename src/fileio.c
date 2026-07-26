#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "fileio.h"
#include "config.h"
#include "row.h"
#include "highlight.h"
#include "output.h"
#include "terminal.h"
#include "input.h"


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

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        // was dying fullu, now shows error and make empty file
        setStatusMessage("Error opening file: %s", strerror(errno));
        return;
    }

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        if (line == NULL) break;
        
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
            linelen--;
            
        editorInsertRow(CONFIG.numrows, line, linelen);
    }

    free(line);
    fclose(fp);
    CONFIG.dirty = 0;
    
    editorSelectSyntaxHighlight();
}

char *editorRowsToString(int *buflen) {
    int totlen = 0;
    for (int i = 0; i < CONFIG.numrows; i++) {
        totlen += CONFIG.row[i].size + 1;
    }
    *buflen = totlen;

    char *buf = malloc(totlen ? totlen : 1);
    if (buf == NULL) {
        *buflen = 0;

        return NULL;
    }

    char *p = buf;
    
    for (int i = 0; i < CONFIG.numrows; i++) {
        memcpy(p, CONFIG.row[i].string, CONFIG.row[i].size);
        p += CONFIG.row[i].size;
        *p = '\n';
        p++;
    }

    return buf;
}

/*
 * write to a sibling temp file then rename, for 2 rsns;
 * a crash mid write cannot leave a truncated original, and later on we must
 * never ftruncate a file we still have mapped, that is a SIGBUS waiting
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
