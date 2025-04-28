#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
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

    char *buf = malloc(totlen);
    char *p = buf;
    
    for (int i = 0; i < CONFIG.numrows; i++) {
        memcpy(p, CONFIG.row[i].string, CONFIG.row[i].size);
        p += CONFIG.row[i].size;
        *p = '\n';
        p++;
    }

    return buf;
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

    int fd = open(CONFIG.filename, O_RDWR | O_CREAT, 0644);
    if (fd != -1) {
        if (ftruncate(fd, len) != -1) {
            if (write(fd, buf, len) == len) {
                close(fd);
                free(buf);
                CONFIG.dirty = 0;
                setStatusMessage("%d bytes written to disk", len);
                return;
            }
        }
        close(fd);
    }

    free(buf);
    setStatusMessage("Can't save! I/O error: %s", strerror(errno));
}