#include <stdlib.h>
#include <string.h>
#include "row.h"
#include "config.h"
#include "highlight.h"


int editorRowCxToRx(ROW_DATA *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx && j < row->size; j++) {
        if (row->string[j] == '\t') {
            rx += (TAB_SPACE - 1) - (rx % TAB_SPACE);
        }
        rx++;
    }

    return rx;
}

// owner: CONFIG.row[i].string, freed by editorFreeRow, only when owned == 1
int rowMakeOwned(ROW_DATA *row) {
    if (row->owned) {
        return 1;
    }

    char *copy = malloc(row->size + 1);
    if (copy == NULL) {
        return 0;
    }

    memcpy(copy, row->string, row->size);
    copy[row->size] = '\0';
    row->string = copy;
    row->owned = 1;

    return 1;
}

// owner: row->render, freed by editorFreeRow
void editorRowEnsureRender(ROW_DATA *row) {
    if (row == NULL || row->render != NULL) {
        return;
    }
    if (row->string == NULL && row->size > 0) {
        return;
    }

    int tabs = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->string[j] == '\t') {
            tabs++;
        }
    }

    char *render = malloc(row->size + tabs * (TAB_SPACE - 1) + 1);
    if (render == NULL) {
        return;
    }

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->string[j] == '\t') {
            render[idx++] = ' ';
            while (idx % TAB_SPACE != 0) {
                render[idx++] = ' ';
            }
        }
        else {
            render[idx++] = row->string[j];
        }
    }
    render[idx] = '\0';

    row->render = render;
    row->render_size = idx;
    row->hl_valid = 0;
}

/* row text changed, drop derived buffers, fix comment chain from here down,
   colours get rebuilt lazily when the row is next drawn */
void updateRow(ROW_DATA *row) {
    if (row == NULL) {
        return;
    }

    free(row->render);
    row->render = NULL;
    row->render_size = 0;
    row->hl_valid = 0;

    editorScanCommentChain(row->index, 1);
}

int editorReserveRows(int n) {
    if (n <= CONFIG.rowcap) {
        return 1;
    }

    int cap = CONFIG.rowcap ? CONFIG.rowcap : 64;
    while (cap < n) {
        cap *= 2;
    }

    ROW_DATA *tmp = realloc(CONFIG.row, sizeof(ROW_DATA) * (size_t)cap);
    if (tmp == NULL) {
        return 0;
    }

    CONFIG.row = tmp;
    CONFIG.rowcap = cap;

    return 1;
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > CONFIG.numrows) {
        return;
    }
    if (!editorReserveRows(CONFIG.numrows + 1)) {
        return;
    }

    memmove(&CONFIG.row[at + 1], &CONFIG.row[at], sizeof(ROW_DATA) * (size_t)(CONFIG.numrows - at));
    for (int j = at + 1; j <= CONFIG.numrows; j++) {
        CONFIG.row[j].index++;
    }

    ROW_DATA *row = &CONFIG.row[at];
    row->index = at;
    row->size = (int)len;
    row->string = malloc(len + 1);
    if (row->string == NULL) {
        return;
    }
    memcpy(row->string, s, len);
    row->string[len] = '\0';
    row->owned = 1;

    row->render = NULL;
    row->render_size = 0;
    row->hl = NULL;
    row->hl_valid = 0;
    row->hl_open_comment = 0;

    CONFIG.numrows++;
    CONFIG.dirty++;

    editorScanCommentChain(at, 1);
}

void editorFreeRow(ROW_DATA *row) {
    free(row->render);
    free(row->hl);
    if (row->owned) {
        free(row->string);
    }
    row->render = NULL;
    row->hl = NULL;
    row->string = NULL;
}

void editorDelRow(int at) {
    if (at < 0 || at >= CONFIG.numrows) {
        return;
    }

    editorFreeRow(&CONFIG.row[at]);
    memmove(&CONFIG.row[at], &CONFIG.row[at + 1], sizeof(ROW_DATA) * (size_t)(CONFIG.numrows - at - 1));
    for (int j = at; j < CONFIG.numrows - 1; j++) {
        CONFIG.row[j].index--;
    }

    CONFIG.numrows--;
    CONFIG.dirty++;

    editorScanCommentChain(at, 1);
}

void rowInsertChar(ROW_DATA *row, int at, int c) {
    if (at < 0 || at > row->size) {
        at = row->size;
    }
    if (!rowMakeOwned(row)) {
        return;
    }

    char *tmp = realloc(row->string, row->size + 2);
    if (tmp == NULL) {
        return;
    }
    row->string = tmp;

    memmove(&row->string[at + 1], &row->string[at], row->size - at + 1);
    row->size++;
    row->string[at] = c;
    updateRow(row);
    CONFIG.dirty++;
}

void rowDelChar(ROW_DATA *row, int at) {
    if (at < 0 || at >= row->size) {
        return;
    }
    if (!rowMakeOwned(row)) {
        return;
    }

    memmove(&row->string[at], &row->string[at + 1], row->size - at);
    row->size--;
    updateRow(row);
    CONFIG.dirty++;
}

void rowAppendString(ROW_DATA *row, char *s, size_t len) {
    if (!rowMakeOwned(row)) {
        return;
    }

    char *tmp = realloc(row->string, row->size + len + 1);
    if (tmp == NULL) {
        return;
    }
    row->string = tmp;

    memcpy(&row->string[row->size], s, len);
    row->size += (int)len;
    row->string[row->size] = '\0';
    updateRow(row);
    CONFIG.dirty++;
}
