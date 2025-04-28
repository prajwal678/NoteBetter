#include <stdlib.h>
#include <string.h>
#include "row.h"
#include "config.h"
#include "highlight.h"
#include "highlight_thread.h"


int editorRowCxToRx(ROW_DATA *row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; j++) {
        if (row->string[j] == '\t')
            rx += (TAB_SPACE - 1) - (rx % TAB_SPACE);
        rx++;
    }
    return rx;
}

int editorRowRxToCx(ROW_DATA *row, int rx) {
    int cur_rx = 0;
    int cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->string[cx] == '\t')
            cur_rx += (TAB_SPACE - 1) - (cur_rx % TAB_SPACE);
        cur_rx++;
        if (cur_rx > rx) return cx;
    }
    return cx;
}

void updateRow(ROW_DATA *row) {
    if (row == NULL || row->string == NULL) return;
    
    int tabs = 0;
    for (int j = 0; j < row->size; j++)
        if (row->string[j] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs*(TAB_SPACE - 1) + 1);
    if (row->render == NULL) return; /* Check for allocation failure */

    int idx = 0;
    for (int j = 0; j < row->size; j++) {
        if (row->string[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % TAB_SPACE != 0) row->render[idx++] = ' ';
        }
        else {
            row->render[idx++] = row->string[j];
        }
    }
    row->render[idx] = '\0';
    row->render_size = idx;

    highlightThreadQueueRow(row);
}

void editorInsertRow(int at, char *s, size_t len) {
    if (at < 0 || at > CONFIG.numrows) return;

    CONFIG.row = realloc(CONFIG.row, sizeof(ROW_DATA) * (CONFIG.numrows + 1));
    memmove(&CONFIG.row[at + 1], &CONFIG.row[at], sizeof(ROW_DATA) * (CONFIG.numrows - at));
    for (int j = at + 1; j <= CONFIG.numrows; j++) CONFIG.row[j].index++;

    CONFIG.row[at].index = at;
    CONFIG.row[at].size = len;
    CONFIG.row[at].string = malloc(len + 1);
    memcpy(CONFIG.row[at].string, s, len);
    CONFIG.row[at].string[len] = '\0';

    CONFIG.row[at].render_size = 0;
    CONFIG.row[at].render = NULL;
    CONFIG.row[at].hl = NULL;
    CONFIG.row[at].hl_open_comment = 0;
    updateRow(&CONFIG.row[at]);

    CONFIG.numrows++;
    CONFIG.dirty++;
}

void editorFreeRow(ROW_DATA *row) {
    free(row->render);
    free(row->string);
    free(row->hl);
}

void editorDelRow(int at) {
    if (at < 0 || at >= CONFIG.numrows) return;
    editorFreeRow(&CONFIG.row[at]);
    memmove(&CONFIG.row[at], &CONFIG.row[at + 1], sizeof(ROW_DATA) * (CONFIG.numrows - at - 1));
    for (int j = at; j < CONFIG.numrows - 1; j++) CONFIG.row[j].index--;
    CONFIG.numrows--;
    CONFIG.dirty++;
}

void rowInsertChar(ROW_DATA *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->string = realloc(row->string, row->size + 2);
    memmove(&row->string[at + 1], &row->string[at], row->size - at + 1);
    row->size++;
    row->string[at] = c;
    updateRow(row);
    CONFIG.dirty++;
}

void rowDelChar(ROW_DATA *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->string[at], &row->string[at + 1], row->size - at);
    row->size--;
    updateRow(row);
    CONFIG.dirty++;
}

void rowAppendString(ROW_DATA *row, char *s, size_t len) {
    row->string = realloc(row->string, row->size + len + 1);
    memcpy(&row->string[row->size], s, len);
    row->size += len;
    row->string[row->size] = '\0';
    updateRow(row);
    CONFIG.dirty++;
}