#ifndef ROW_H
#define ROW_H

#include "config.h"


int editorRowCxToRx(ROW_DATA *row, int cx);
int editorRowRxToCx(ROW_DATA *row, int rx);
void updateRow(ROW_DATA *row);
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(ROW_DATA *row);
void editorDelRow(int at);
void rowInsertChar(ROW_DATA *row, int at, int c);
void rowDelChar(ROW_DATA *row, int at);
void rowAppendString(ROW_DATA *row, char *s, size_t len);

#endif