#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include "config.h"


void editorUpdateSyntax(ROW_DATA *row);
int editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight(void);
int isSeparator(int c);

#endif